/* vid.c -- Text video layer for Harvac port of MEDIT.
 * Shadow buffer with dirty-row tracking; flushes to VGA VRAM (0xB800) via
 * write_far_w. No CGA snow wait needed on VGA/QEMU.
 * Cursor positioned via SYSCALL_SET_CURSOR; hidden/shown via CRTC port I/O. */

#include "medit.h"
#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "vid.h"

/* VGA text buffer segment */
#define VGA_SEG  0xB800u

static u16 shadow[ROWS][COLS];
static u8  dirty[ROWS];
static u8  curhidden;

void vid_init(int fastmode)
{
    int i, j;
    (void)fastmode;   /* not used: no CGA snow on VGA */

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
    /* Restore normal cursor */
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
    int r, c;
    for (r = 0; r < ROWS; r++) {
        if (!dirty[r])
            continue;
        for (c = 0; c < COLS; c++)
            write_far_w(VGA_SEG, (u16)((r * COLS + c) * 2), shadow[r][c]);
        dirty[r] = 0;
    }
}

void vid_cursor(int row, int col)
{
    if (curhidden) {
        /* Restore normal cursor shape (scan lines 14-15 on VGA) */
        port_out_b(0x3D4, 0x0A);
        port_out_b(0x3D5, 0x0E);
        curhidden = 0;
    }
    /* SYSCALL_SET_CURSOR: BX = (row << 8) | col */
    syscall_int40(SYSCALL_SET_CURSOR, 0,
                  (u16)(((u16)(u8)row << 8) | (u8)col),
                  0, 0, 0, 0);
}

void vid_cursor_hide(void)
{
    if (!curhidden) {
        port_out_b(0x3D4, 0x0A);
        port_out_b(0x3D5, 0x20);   /* bit 5 of cursor start = cursor off */
        curhidden = 1;
    }
}
