#ifndef DRIVERS_KEYBOARD_H
#define DRIVERS_KEYBOARD_H

#include "types.h"

/* Maximum entries in the keyboard ring buffer */
#define KBD_BUFFER_SIZE 64

/* Initialize the keyboard (installs IRQ1 handler, unmasks IRQ1) */
void keyboard_init(void);

/* IRQ1 handler body; called from irq1_stub_ in syscall.asm only */
void keyboard_irq_handler(void);

/* Read next key event (blocking).
 * Returns uint16_t: high byte = scan code (0 for plain ASCII keys),
 * low byte = ASCII char (0 for special/Alt keys).
 * Matches BIOS INT 16h AH=00h format: AH=scan, AL=ascii. */
uint16_t keyboard_getkey(void);

/* Return current modifier state.
 * Bit layout: bits 0-1 = shift, bit 2 = ctrl, bit 3 = alt
 * (same as BIOS INT 16h AH=02h). */
uint8_t keyboard_shift_state(void);

/* Return nonzero if a key event is available (non-blocking). */
uint8_t keyboard_available(void);

#endif /* DRIVERS_KEYBOARD_H */
