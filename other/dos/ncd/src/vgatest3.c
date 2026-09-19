/* vgatest3.c -- Test VGA via INT 10h AH=09h and AH=0Eh */
#include <dos.h>
#include <bios.h>
#include <stdio.h>

#define VGA_BASE ((unsigned short __far *)(((unsigned long)0xB800 << 16) | 0))

void write_char(int row, int col, char ch, char attr)
{
    union REGS r;

    /* Set cursor position */
    r.h.ah = 0x02;
    r.h.bh = 0x00;
    r.h.dh = (unsigned char)row;
    r.h.dl = (unsigned char)col;
    int86(0x10, &r, &r);

    /* Write char with attribute */
    r.h.ah = 0x09;
    r.h.al = (unsigned char)ch;
    r.h.bh = 0x00;
    r.h.bl = (unsigned char)(attr & 0xFF);
    r.x.cx = 1;
    int86(0x10, &r, &r);
}

void main(void)
{
    int i;
    unsigned short __far *vp;
    FILE *f;
    union REGS r;

    /* Set mode 3 (80x25 text) */
    r.h.ah = 0x00;
    r.h.al = 0x03;
    int86(0x10, &r, &r);

    /* Write via INT 10h */
    write_char(10, 10, 'A', 0x07);
    write_char(10, 11, 'B', 0x0F);
    write_char(10, 12, 'C', 0x70);

    /* Write directly to VGA */
    vp = VGA_BASE + 10 * 80 + 30;
    vp[0] = (0x07 << 8) | 'X';
    vp[1] = (0x0F << 8) | 'Y';
    vp[2] = (0x70 << 8) | 'Z';

    /* Dump VGA buffer to file */
    f = fopen("VGA3.TXT", "w");
    if (f) {
        vp = VGA_BASE;
        for (i = 0; i < 25 * 80; i++) {
            unsigned short val = vp[i];
            char ch = (char)(val & 0xFF);
            if (ch < 32) ch = '.';
            fputc(ch, f);
        }
        fclose(f);
    }

    /* Restore mode */
    r.h.ah = 0x00;
    r.h.al = 0x03;
    int86(0x10, &r, &r);
}