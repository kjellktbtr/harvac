/* video.c -- VGA text mode video driver for DOS NCD
 * Direct access to 0xB8000 video memory, 80x25 text mode.
 */

#include "dos_ncd.h"
#include <dos.h>
#include <string.h>

/* Shadow screen buffer */
static u16 shadow[ROWS * COLS];
static u8  dirty[ROWS];  /* Per-row dirty flags */
static u8  cursor_visible = 1;

/* --- Low-level video operations --- */

static u16 make_attr_char(u8 attr, u8 ch)
{
    return ((u16)attr << 8) | ch;
}

void vid_init(void)
{
    union REGS regs;
    regs.h.ah = 0x00;
    regs.h.al = 0x03;  /* 80x25 text mode */
    int86(0x10, &regs, &regs);

    /* Clear shadow buffer */
    memset(shadow, 0, sizeof(shadow));
    memset(dirty, 1, sizeof(dirty));  /* Mark all rows dirty initially */
    vid_cursor_hide();
}

void vid_done(void)
{
    union REGS regs;
    regs.h.ah = 0x00;
    regs.h.al = 0x03;  /* Reset to 80x25 text mode */
    int86(0x10, &regs, &regs);
    vid_cursor_show();
}

void vid_putat(u8 row, u8 col, u8 attr, u8 ch)
{
    u16 val;
    if (row >= ROWS || col >= COLS) return;
    val = make_attr_char(attr, ch);
    if (shadow[row * COLS + col] != val) {
        shadow[row * COLS + col] = val;
        dirty[row] = 1;
    }
}

void vid_puts(u8 row, u8 col, const char *str, u8 attr)
{
    while (*str && col < COLS) {
        vid_putat(row, col, attr, *str++);
        col++;
    }
}

void vid_fill(u8 row, u8 col, u8 width, u8 attr, u8 ch)
{
    u8 end = col + width;
    u8 c;
    if (end > COLS) end = COLS;
    for (c = col; c < end; c++) {
        vid_putat(row, c, attr, ch);
    }
}

void vid_fill_attr(u8 row, u8 col, u8 width, u8 attr)
{
    u8 end = col + width;
    u8 c;
    if (end > COLS) end = COLS;
    for (c = col; c < end; c++) {
        u16 idx = row * COLS + c;
        u16 val = (shadow[idx] & 0xFF) | ((u16)attr << 8);
        if (shadow[idx] != val) {
            shadow[idx] = val;
            dirty[row] = 1;
        }
    }
}

void vid_flush(void)
{
    u8 row;
    for (row = 0; row < ROWS; row++) {
        if (dirty[row]) {
            u16 base = row * COLS;
            u16 byte_offs = row * COLS * 2;  /* byte offset in VGA buffer */
            u16 i;
            for (i = 0; i < COLS; i++) {
                u16 val = shadow[base + i];
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
            dirty[row] = 0;
        }
    }
}

void vid_cursor(u8 row, u8 col)
{
    union REGS regs;
    regs.h.ah = 0x02;
    regs.h.bh = 0x00;
    regs.h.dh = row;
    regs.h.dl = col;
    int86(0x10, &regs, &regs);
}

void vid_cursor_hide(void)
{
    union REGS regs;
    regs.h.ah = 0x01;
    regs.x.cx = 0x2000;  /* Cursor off */
    int86(0x10, &regs, &regs);
    cursor_visible = 0;
}

void vid_cursor_show(void)
{
    union REGS regs;
    regs.h.ah = 0x01;
    regs.x.cx = 0x0607;  /* Normal cursor shape */
    int86(0x10, &regs, &regs);
    cursor_visible = 1;
}

void vid_dirty_all(void)
{
    memset(dirty, 1, sizeof(dirty));
}

void vid_clear(u8 attr)
{
    u16 val = make_attr_char(attr, ' ');
    int i;
    for (i = 0; i < ROWS * COLS; i++) {
        if (shadow[i] != val) {
            shadow[i] = val;
        }
    }
    memset(dirty, 1, sizeof(dirty));
}

/* --- Box drawing helpers --- */

void vid_draw_box(u8 top, u8 left, u8 height, u8 width, u8 attr)
{
    u8 bottom = top + height - 1;
    u8 right  = left + width - 1;
    u8 c, r;

    if (bottom >= ROWS) bottom = ROWS - 1;
    if (right >= COLS)  right  = COLS - 1;

    /* Corners */
    vid_putat(top, left, attr, BOX_TL);
    vid_putat(top, right, attr, BOX_TR);
    vid_putat(bottom, left, attr, BOX_BL);
    vid_putat(bottom, right, attr, BOX_BR);

    /* Top/bottom edges */
    for (c = left + 1; c < right; c++) {
        vid_putat(top, c, attr, BOX_H);
        vid_putat(bottom, c, attr, BOX_H);
    }

    /* Left/right edges */
    for (r = top + 1; r < bottom; r++) {
        vid_putat(r, left, attr, BOX_V);
        vid_putat(r, right, attr, BOX_V);
    }
}

/* Draw horizontal line */
void vid_hline(u8 row, u8 col, u8 len, u8 attr, u8 ch)
{
    u8 i;
    for (i = 0; i < len && col + i < COLS; i++) {
        vid_putat(row, col + i, attr, ch);
    }
}

/* Draw vertical line */
void vid_vline(u8 row, u8 col, u8 len, u8 attr, u8 ch)
{
    u8 i;
    for (i = 0; i < len && row + i < ROWS; i++) {
        vid_putat(row + i, col, attr, ch);
    }
}