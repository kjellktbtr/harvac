/* vgatest2.c -- Test VGA text with key wait */
#include <dos.h>
#include <bios.h>

#define VGA_BASE ((unsigned short __far *)(((unsigned long)0xB800 << 16) | 0))

void main(void)
{
    unsigned short __far *vp;
    int i;
    union REGS r;

    /* Set mode 3 (80x25 text) */
    r.h.ah = 0x00;
    r.h.al = 0x03;
    int86(0x10, &r, &r);

    /* Fill screen with spaces */
    vp = VGA_BASE;
    for (i = 0; i < 80 * 25; i++) {
        vp[i] = (0x07 << 8) | ' ';
    }

    /* Write "HELLO NCD!" at row 12, col 35 */
    vp = VGA_BASE + 12 * 80 + 35;
    vp[0] = (0x07 << 8) | 'H';
    vp[1] = (0x07 << 8) | 'E';
    vp[2] = (0x07 << 8) | 'L';
    vp[3] = (0x07 << 8) | 'L';
    vp[4] = (0x07 << 8) | 'O';
    vp[5] = (0x07 << 8) | ' ';
    vp[6] = (0x07 << 8) | 'N';
    vp[7] = (0x07 << 8) | 'C';
    vp[8] = (0x07 << 8) | 'D';
    vp[9] = (0x07 << 8) | '!';

    /* Position cursor at bottom */
    r.h.ah = 0x02;
    r.h.bh = 0x00;
    r.h.dh = 24;
    r.h.dl = 0;
    int86(0x10, &r, &r);

    /* Wait for keypress */
    _bios_keybrd(_KEYBRD_READ);

    /* Restore mode */
    r.h.ah = 0x00;
    r.h.al = 0x03;
    int86(0x10, &r, &r);
}