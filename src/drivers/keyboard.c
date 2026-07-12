/* keyboard.c -- keyboard driver (IRQ1-driven, scancode set 1)
 *
 * Interrupt-driven so it works on both AT-class machines (PS/2 controller)
 * and XT-class machines (8255 PPI, IBM 5150/5155). The AT status port 0x64
 * does not exist on an XT - reads float to 0xFF - so status polling must
 * never be used. IRQ1 fires on both; the handler reads the scancode from
 * port 0x60 and pulses bit 7 of port 0x61 (XT keyboard ack, harmless on AT).
 *
 * Ring buffer stores uint16_t key events: high byte = scan code (0 for plain
 * ASCII), low byte = ASCII char (0 for special keys / Alt combos).
 * This matches the BIOS INT 16h AH=00h return format (AH=scan, AL=ascii).
 *
 * Also polls COM1 serial port for input; serial chars land as plain ASCII
 * events (high byte = 0). Special keys are keyboard-only. */

#include "kernel.h"
#include "drivers/keyboard.h"
#include "drivers/serial.h"

/* Keyboard ports */
#define KBD_DATA    0x60
#define KBD_CTRL    0x61   /* 8255 port B: bit 7 = XT keyboard ack */

/* PIC ports */
#define PIC1_CMD    0x20
#define PIC1_DATA   0x21

/* Ring buffer of uint16_t key events */
static volatile uint16_t kbd_buffer[KBD_BUFFER_SIZE];
static volatile uint8_t  kbd_head;
static volatile uint8_t  kbd_tail;

/* Modifier state */
static uint8_t shift_pressed;
static uint8_t caps_locked;
static uint8_t ctrl_pressed;
static uint8_t alt_pressed;

/* 0xE0 extended-key prefix received; next byte is an extended scan code */
static uint8_t ext_prefix;

/*
 * Scancode Set 1 to ASCII, unshifted.
 * Index by scancode; 0 = no character (special key or unhandled).
 */
static const char scancode_ascii[128] = {
    0,   0,   '1', '2', '3', '4', '5', '6', /* 00-07 */
    '7', '8', '9', '0', '-', '=', '\b', '\t',  /* 08-0F  0x0E=Backspace, 0x0F=Tab */
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', /* 10-17 */
    'o', 'p', '[', ']', '\r', 0,  'a', 's', /* 18-1F  0x1C=Enter */
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', /* 20-27 */
    39,  '`', 0,   92,  'z', 'x', 'c', 'v', /* 28-2F */
    'b', 'n', 'm', ',', '.', '/', 0,   '*', /* 30-37 */
    0,   ' ', 0,   0,   0,   0,   0,   0,   /* 38-3F */
    0,   0,   0,   0,   0,   0,   0,   0,   /* 40-47 */
    0,   0,   0,   0,   0,   0,   0,   0,   /* 48-4F */
    0,   0,   0,   0,   0,   0,   0,   0,   /* 50-57 */
    0,   0,   0,   0,   0,   0,   0,   0,   /* 58-5F */
    0,   0,   0,   0,   0,   0,   0,   0,   /* 60-67 */
    0,   0,   0,   0,   0,   0,   0,   0,   /* 68-6F */
    0,   0,   0,   0,   0,   0,   0,   0,   /* 70-77 */
    0,   0,   0,   0,   0,   0,   0,   0,   /* 78-7F */
};

/* Shifted scancode to ASCII */
static const char scancode_ascii_shift[128] = {
    0,   0,   '!', '@', '#', '$', '%', '^', /* 00-07 */
    '&', '*', '(', ')', '_', '+', '\b', '\t',  /* 08-0F  0x0E=Backspace, 0x0F=Tab */
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', /* 10-17 */
    'O', 'P', '{', '}', '\r', 0,  'A', 'S', /* 18-1F  0x1C=Enter */
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', /* 20-27 */
    '"', '~', 0,   '|', 'Z', 'X', 'C', 'V', /* 28-2F */
    'B', 'N', 'M', '<', '>', '?', 0,   '*', /* 30-37 */
    0,   ' ', 0,   0,   0,   0,   0,   0,   /* 38-3F */
};

/* Push a 16-bit key event into the ring buffer. */
static void kbd_push(uint16_t ev)
{
    uint8_t next = (uint8_t)((kbd_head + 1) % KBD_BUFFER_SIZE);
    if (next != kbd_tail) {        /* buffer not full */
        kbd_buffer[kbd_head] = ev;
        kbd_head = next;
    }
}

/* Translate scancode + modifiers to ASCII (0 if no ASCII mapping). */
static char translate_key(uint8_t scancode)
{
    char c;

    if (scancode >= 128)
        return 0;

    c = shift_pressed ? scancode_ascii_shift[scancode]
                      : scancode_ascii[scancode];

    /* Handle alpha case with caps lock */
    if (c >= 'a' && c <= 'z' && caps_locked)
        c -= 32;
    else if (c >= 'A' && c <= 'Z' && caps_locked)
        c += 32;

    return c;
}

/* Process a make/break scancode from the PS/2 port. */
static void process_scancode(uint8_t sc)
{
    /* 0xE0 = extended key prefix; remember it, wait for the actual code */
    if (sc == 0xE0) {
        ext_prefix = 1;
        return;
    }

    /* Gray/extended navigation keys emit E0 2A (make) / E0 AA (break) as
     * "fake shift" sequences around the real key code.  Discard them so
     * shift_pressed is not spuriously set during, e.g., a left/right arrow. */
    if (ext_prefix && (sc == 0x2A || sc == 0x36 || sc == 0xAA || sc == 0xB6)) {
        ext_prefix = 0;
        return;
    }

    /* Modifier make/break codes */
    if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; ext_prefix = 0; return; }
    if (sc == 0xAA || sc == 0xB6) { shift_pressed = 0; ext_prefix = 0; return; }
    if (sc == 0x1D)                { ctrl_pressed  = 1; ext_prefix = 0; return; }
    if (sc == 0x9D)                { ctrl_pressed  = 0; ext_prefix = 0; return; }
    if (sc == 0x38)                { alt_pressed   = 1; ext_prefix = 0; return; }
    if (sc == 0xB8)                { alt_pressed   = 0; ext_prefix = 0; return; }
    if (sc == 0x3A)                { caps_locked = !caps_locked; ext_prefix = 0; return; }

    /* Break codes for all other keys: ignore */
    if (sc & 0x80) { ext_prefix = 0; return; }

    if (ext_prefix) {
        /* Extended (0xE0-prefixed) navigation key: store as (scan, 0) special. */
        ext_prefix = 0;
        kbd_push((uint16_t)((uint16_t)sc << 8));
        return;
    }

    /* Alt+key: store as (scan, 0) special — same format as BIOS INT 16h AH=00h
     * when Alt is held (AL=0, AH=scan of the letter pressed). */
    if (alt_pressed) {
        kbd_push((uint16_t)((uint16_t)sc << 8));
        return;
    }

    /* F-keys (0x3B..0x44) and other zero-ASCII keys: store as (scan, 0) special. */
    {
        char ascii = translate_key(sc);

        if (ascii == 0) {
            /* Non-printable, non-modifier: store as special (scan in high byte). */
            kbd_push((uint16_t)((uint16_t)sc << 8));
            return;
        }

        /* Ctrl+alpha: generate control character (Ctrl+A=0x01, Ctrl+S=0x13, etc.) */
        if (ctrl_pressed && ascii >= 'a' && ascii <= 'z')
            kbd_push((uint16_t)(ascii - 'a' + 1));
        else if (ctrl_pressed && ascii >= 'A' && ascii <= 'Z')
            kbd_push((uint16_t)(ascii - 'A' + 1));
        else
            kbd_push((uint16_t)(uint8_t)ascii);
    }
}

/* IRQ1 handler (called from irq1_stub_ in syscall.asm) */
void keyboard_irq_handler(void)
{
    uint8_t sc, ctrl;

    sc = port_in_b(KBD_DATA);

    /* XT keyboard ack: pulse bit 7 of 8255 port B. Required on 8086/XT
     * machines or no further scancodes arrive; harmless on AT/PS2. */
    ctrl = port_in_b(KBD_CTRL);
    port_out_b(KBD_CTRL, (uint8_t)(ctrl | 0x80));
    port_out_b(KBD_CTRL, ctrl);

    process_scancode(sc);

    /* End-of-interrupt to master PIC */
    port_out_b(PIC1_CMD, 0x20);
}

void keyboard_init(void)
{
    extern void irq1_stub(void);
    uint8_t imr;

    kbd_head     = 0;
    kbd_tail     = 0;
    shift_pressed = 0;
    caps_locked   = 0;
    ctrl_pressed  = 0;
    alt_pressed   = 0;
    ext_prefix    = 0;

    /* Discard any stale scancode (single read; no status polling, since
     * the AT status port 0x64 does not exist on XT machines) */
    (void)port_in_b(KBD_DATA);

    /* Install IRQ1 handler in IVT entry 0x09. This replaces the BIOS
     * INT 09h handler, so the BIOS no longer steals scancodes. */
    write_far_w(0x0000, 0x0009 * 4, (uint16_t)&irq1_stub);        /* offset */
    write_far_w(0x0000, 0x0009 * 4 + 2, KERNEL_SEGMENT);          /* segment */

    /* Unmask IRQ1 on master PIC (clear bit 1 of IMR) */
    imr = port_in_b(PIC1_DATA);
    port_out_b(PIC1_DATA, (uint8_t)(imr & ~0x02));
}

uint16_t keyboard_getkey(void)
{
    for (;;) {
        /* Return next event from ring buffer (filled by IRQ1 handler) */
        if (kbd_head != kbd_tail) {
            uint16_t ev = kbd_buffer[kbd_tail];
            kbd_tail = (uint8_t)((kbd_tail + 1) % KBD_BUFFER_SIZE);
            return ev;
        }
        /* Poll serial port: serial chars are plain ASCII (high byte = 0) */
        if (serial_available()) {
            char c = serial_getchar();
            return (uint16_t)(uint8_t)c;
        }
    }
}

uint8_t keyboard_shift_state(void)
{
    /* Bit layout matches BIOS INT 16h AH=02h:
     * bits 0-1 = shift, bit 2 = ctrl, bit 3 = alt */
    return (uint8_t)((shift_pressed ? 0x03 : 0)
                   | (ctrl_pressed  ? 0x04 : 0)
                   | (alt_pressed   ? 0x08 : 0));
}

uint8_t keyboard_available(void)
{
    /* Ring buffer is filled by the IRQ1 handler */
    return kbd_head != kbd_tail;
}
