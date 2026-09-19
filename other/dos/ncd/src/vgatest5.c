/* vgatest5.c -- Test vid_init + inline asm flush */
#include <dos.h>
#include <stdio.h>

#define ROWS 25
#define COLS 80

static unsigned short shadow[ROWS * COLS];

static void vid_init(void)
{
    union REGS r;
    r.h.ah = 0x00;
    r.h.al = 0x03;
    int86(0x10, &r, &r);
}

static void vid_flush(void)
{
    unsigned row;
    for (row = 0; row < ROWS; row++) {
        unsigned base = row * COLS;
        unsigned byte_offs = base * 2;
        unsigned i;
        for (i = 0; i < COLS; i++) {
            unsigned val = shadow[base + i];
            _asm {
                mov ax, val
                mov bx, byte_offs
                push es
                mov dx, 0B800h
                mov es, dx
                mov es:[bx], ax
                pop es
            }
            byte_offs += 2;
        }
    }
}

void main(void)
{
    unsigned row, col;
    FILE *f;
    union REGS r;

    vid_init();

    /* Fill shadow buffer */
    for (row = 0; row < ROWS; row++) {
        unsigned i;
        for (i = 0; i < COLS; i++) {
            shadow[row * COLS + i] = (0x07 << 8) | ' ';
        }
    }

    /* Write "HELLO" at row 12, col 0 */
    shadow[12 * COLS + 0] = (0x07 << 8) | 'H';
    shadow[12 * COLS + 1] = (0x07 << 8) | 'E';
    shadow[12 * COLS + 2] = (0x07 << 8) | 'L';
    shadow[12 * COLS + 3] = (0x07 << 8) | 'L';
    shadow[12 * COLS + 4] = (0x07 << 8) | 'O';

    /* Write "ASM!" at row 13, col 0 via INT 10h for comparison */
    r.h.ah = 0x02;
    r.h.bh = 0x00;
    r.h.dh = 13;
    r.h.dl = 0;
    int86(0x10, &r, &r);
    r.h.ah = 0x09;
    r.h.al = 'A';
    r.h.bh = 0x00;
    r.h.bl = 0x07;
    r.x.cx = 1;
    int86(0x10, &r, &r);

    /* Flush shadow to VGA */
    vid_flush();

    /* Dump VGA to file */
    f = fopen("VGA5.TXT", "w");
    if (f) {
        for (row = 0; row < ROWS; row++) {
            for (col = 0; col < COLS; col++) {
                char ch;
                unsigned short val;
                unsigned offs = (row * COLS + col) * 2;
                _asm {
                    mov bx, offs
                    push es
                    mov dx, 0B800h
                    mov es, dx
                    mov ax, es:[bx]
                    pop es
                    mov val, ax
                }
                ch = (char)(val & 0xFF);
                if (ch < 32) ch = '.';
                fputc(ch, f);
            }
            fputc('\r', f);
            fputc('\n', f);
        }
        fclose(f);
    }
}