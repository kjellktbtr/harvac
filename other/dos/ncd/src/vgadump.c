/* vgadump.c -- Dump VGA text buffer to file */
#include <dos.h>
#include <stdio.h>

#define VGA_BASE ((unsigned short __far *)(((unsigned long)0xB800 << 16) | 0))

void main(void)
{
    unsigned short __far *vp;
    FILE *f;
    int row;
    int col;
    union REGS r;

    /* Set mode 3 (80x25 text) */
    r.h.ah = 0x00;
    r.h.al = 0x03;
    int86(0x10, &r, &r);

    /* Write "HELLO NCD!" to VGA at row 12, col 35 */
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

    /* Also set cursor to bottom */
    r.h.ah = 0x02;
    r.h.bh = 0x00;
    r.h.dh = 24;
    r.h.dl = 0;
    int86(0x10, &r, &r);

    /* Dump VGA text buffer to file */
    f = fopen("VGADUMP.TXT", "w");
    if (f) {
        vp = VGA_BASE;
        for (row = 0; row < 25; row++) {
            for (col = 0; col < 80; col++) {
                unsigned short val = vp[row * 80 + col];
                char ch = (char)(val & 0xFF);
                if (ch < 32 || ch > 126) ch = '.';
                fputc(ch, f);
            }
            fputc('\r', f);
            fputc('\n', f);
        }
        fclose(f);
    }

    /* Restore mode */
    r.h.ah = 0x00;
    r.h.al = 0x03;
    int86(0x10, &r, &r);
}