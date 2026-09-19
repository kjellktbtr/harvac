/* vgatest4.c -- Test inline assembly VGA writes */
#include <dos.h>
#include <stdio.h>
#include <string.h>

static void vga_write(unsigned offs, unsigned short val)
{
    _asm {
        mov bx, offs
        mov ax, val
        push es
        mov dx, 0B800h
        mov es, dx
        mov es:[bx], ax
        pop es
    }
}

void main(void)
{
    int i, row, col;
    FILE *f;
    union REGS r;

    /* Set mode 3 (80x25 text) */
    r.h.ah = 0x00;
    r.h.al = 0x03;
    int86(0x10, &r, &r);

    /* Fill screen with spaces via inline asm */
    for (i = 0; i < 80 * 25; i++) {
        vga_write(i * 2, (0x07 << 8) | ' ');
    }

    /* Write "HELLO" at row 12, col 0 via inline asm */
    vga_write((12 * 80 + 0) * 2, (0x07 << 8) | 'H');
    vga_write((12 * 80 + 1) * 2, (0x07 << 8) | 'E');
    vga_write((12 * 80 + 2) * 2, (0x07 << 8) | 'L');
    vga_write((12 * 80 + 3) * 2, (0x07 << 8) | 'L');
    vga_write((12 * 80 + 4) * 2, (0x07 << 8) | 'O');

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
    r.h.dl = 1;
    int86(0x10, &r, &r);
    r.h.ah = 0x09;
    r.h.al = 'S';
    int86(0x10, &r, &r);
    r.h.dl = 2;
    int86(0x10, &r, &r);
    r.h.ah = 0x09;
    r.h.al = 'M';
    int86(0x10, &r, &r);
    r.h.dl = 3;
    int86(0x10, &r, &r);
    r.h.ah = 0x09;
    r.h.al = '!';
    int86(0x10, &r, &r);

    /* Dump VGA buffer to file */
    f = fopen("VGA4.TXT", "w");
    if (f) {
        for (row = 0; row < 25; row++) {
            for (col = 0; col < 80; col++) {
                unsigned short val;
                _asm {
                    mov bx, 0B800h
                    mov es, bx
                    mov bx, [row]
                    mov ax, 80
                    mul bx
                    add ax, [col]
                    shl ax, 1
                    mov bx, ax
                    mov ax, es:[bx]
                    mov [val], ax
                }
                char ch = (char)(val & 0xFF);
                if (ch < 32) ch = '.';
                fputc(ch, f);
            }
            fputc('\r', f);
            fputc('\n', f);
        }
        fclose(f);
    }
}