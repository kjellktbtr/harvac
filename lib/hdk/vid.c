/* vid.c -- Text video layer for HarvaC HDK.
 * Shadow buffer with dirty-row tracking; flushes to VGA VRAM (0xB800) via
 * write_far_w. Cursor positioned via SYSCALL_SET_CURSOR. Cursor shown/hidden
 * via CRTC registers (port 0x3D4/0x3D5).
 *
 * Merged from apps/medit/vid.c and apps/ncd/vid.c (near-identical).
 * Unified on vid_init(void) — medit's unused fastmode parameter dropped. */

#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "hdk_video.h"

#define VGA_SEG  0xB800u

static uint16_t shadow[ROWS][COLS];
static uint8_t  dirty[ROWS];
static uint8_t  curhidden;

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
    /* Restore normal cursor shape (scan lines 14-15 on VGA) */
    port_out_b(0x3D4, 0x0A);
    port_out_b(0x3D5, 0x0E);
}

void vid_putat(int row, int col, uint8_t ch, uint8_t attr)
{
    uint16_t v = ((uint16_t)attr << 8) | ch;
    if ((unsigned)row >= ROWS || (unsigned)col >= COLS)
        return;
    if (shadow[row][col] != v) {
        shadow[row][col] = v;
        dirty[row] = 1;
    }
}

void vid_puts(int row, int col, const char *s, uint8_t attr)
{
    while (*s)
        vid_putat(row, col++, (uint8_t)*s++, attr);
}

void vid_fill(int row, int col, int n, uint8_t ch, uint8_t attr)
{
    while (n-- > 0)
        vid_putat(row, col++, ch, attr);
}

void vid_flush(void)
{
    int r, c;
    for (r = 0; r < ROWS; r++) {
        if (!dirty[r]) continue;
        dirty[r] = 0;
        for (c = 0; c < COLS; c++)
            write_far_w(VGA_SEG, (uint16_t)((r * COLS + c) * 2), shadow[r][c]);
    }
}

void vid_cursor(int row, int col)
{
    if (curhidden) {
        port_out_b(0x3D4, 0x0A);
        port_out_b(0x3D5, 0x0E);
        curhidden = 0;
    }
    /* SYSCALL_SET_CURSOR: BX = (row << 8) | col */
    syscall_int40(SYSCALL_SET_CURSOR, 0,
                  (uint16_t)(((uint16_t)(uint8_t)row << 8) | (uint8_t)col),
                  0, 0, 0, 0);
}

void vid_cursor_hide(void)
{
    if (!curhidden) {
        port_out_b(0x3D4, 0x0A);
        port_out_b(0x3D5, 0x20);   /* bit 5 of cursor start register = off */
        curhidden = 1;
    }
}
