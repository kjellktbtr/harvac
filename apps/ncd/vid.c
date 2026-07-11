/* vid.c -- Text video layer for NCD.
 * Shadow buffer with dirty-row tracking; flushes to VGA VRAM (0xB800) via
 * write_far_w. Cursor positioned via SYSCALL_SET_CURSOR. */

#include "ncd.h"
#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "vid.h"

#define VGA_SEG  0xB800u

static u16 shadow[ROWS][COLS];
static u8  dirty[ROWS];
static u8  curhidden;

void vid_init(void)
{
    int i, j;
    curhidden = 0;
    for (i = 0; i < ROWS; i++) {
        for (j = 0; j < COLS; j++)
            shadow[i][j] = 0x0700 | ' ';
        dirty[i] = 1;
    }
}

void vid_done(void)
{
    syscall_int40(SYSCALL_CLEAR_SCREEN, 0, 0, 0, 0, 0, 0);
    port_out_b(0x3D4, 0x0A);
    port_out_b(0x3D5, 0x0E);
}

void vid_putat(int row, int col, u8 ch, u8 attr)
{
    u16 v = ((u16)attr << 8) | ch;
    if ((unsigned)row >= ROWS || (unsigned)col >= COLS)
        return;
    if (shadow[row][col] != v) {
        shadow[row][col] = v;
        dirty[row] = 1;
    }
}

void vid_puts(int row, int col, const char *s, u8 attr)
{
    while (*s)
        vid_putat(row, col++, (u8)*s++, attr);
}

void vid_fill(int row, int col, int n, u8 ch, u8 attr)
{
    while (n-- > 0)
        vid_putat(row, col++, ch, attr);
}

void vid_flush(void)
{
    int i, j;
    for (i = 0; i < ROWS; i++) {
        if (!dirty[i]) continue;
        dirty[i] = 0;
        for (j = 0; j < COLS; j++)
            write_far_w(VGA_SEG, (u16)(i * COLS + j) * 2, shadow[i][j]);
    }
}

void vid_cursor(int row, int col)
{
    if (curhidden) {
        port_out_b(0x3D4, 0x0A);
        port_out_b(0x3D5, 0x0E);
        curhidden = 0;
    }
    syscall_int40(SYSCALL_SET_CURSOR, 0, (u16)col | ((u16)row << 8), 0, 0, 0, 0);
}

void vid_cursor_hide(void)
{
    if (!curhidden) {
        port_out_b(0x3D4, 0x0A);
        port_out_b(0x3D5, 0x20);
        curhidden = 1;
    }
}
