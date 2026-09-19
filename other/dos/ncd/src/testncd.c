/* testncd.c -- Test NCD video functions */
#include "dos_ncd.h"
#include <dos.h>
#include <stdio.h>

void main(void)
{
    FILE *f;
    unsigned row, col;
    int i;

    /* Initialize NCD video */
    vid_init();

    /* Draw a simple test pattern */
    vid_clear(A_NORMAL);
    vid_puts(5, 10, "TEST PATTERN", A_BOLD);
    vid_puts(7, 10, "Normal text", A_NORMAL);
    vid_puts(8, 10, "Inverse text", A_INVERSE);
    vid_puts(9, 10, "Directory", A_DIR);

    /* Draw a box */
    vid_draw_box(3, 5, 10, 50, A_DLG);
    vid_puts(3, 14, " DIALOG BOX ", A_DLG);

    vid_flush();

    /* Dump VGA to file */
    f = fopen("NCDTEST.TXT", "w");
    if (f) {
        for (row = 0; row < ROWS; row++) {
            for (col = 0; col < COLS; col++) {
                unsigned short val;
                unsigned offs = (row * COLS + col) * 2;
                char ch;
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

    vid_done();
}