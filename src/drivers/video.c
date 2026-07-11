/* video.c -- VGA text mode 80x25 driver */
/* Writes directly to segment 0xB800 via far-memory pragma-aux helpers. */

#include "kernel.h"
#include "drivers/video.h"

/* TTY cursor state for sequential (terminal-style) output */
static uint8_t tty_row;
static uint8_t tty_col;

void video_init(void)
{
    uint8_t row, col;
    /* Clear screen */
    for (row = 0; row < VIDEO_ROWS; row++) {
        for (col = 0; col < VIDEO_COLS; col++) {
            write_far_w(VIDEO_BUF_SEG, (uint16_t)(row * VIDEO_COLS + col) * 2,
                        (uint16_t)(' ' | (VIDEO_ATTR_NORMAL << 8)));
        }
    }
    tty_row = 0;
    tty_col = 0;
    video_set_cursor(0, 0);
}

void video_putchar(uint8_t row, uint8_t col, char c, uint8_t attr)
{
    uint16_t offset;

    if (row >= VIDEO_ROWS || col >= VIDEO_COLS)
        return;

    offset = (uint16_t)(row * VIDEO_COLS + col) * 2;
    write_far_w(VIDEO_BUF_SEG, offset, (uint16_t)((uint8_t)c | ((uint16_t)attr << 8)));
}

void video_write(uint8_t row, uint8_t col, const char *s, uint8_t attr)
{
    uint8_t r, c;

    r = row;
    c = col;
    while (*s != '\0') {
        if (*s == '\n') {
            r++;
            c = 0;
        } else {
            video_putchar(r, c, *s, attr);
            c++;
            if (c >= VIDEO_COLS) {
                r++;
                c = 0;
            }
        }
        s++;
    }
}

void video_scroll(void)
{
    uint16_t src_off, dst_off;
    uint16_t word;

    /* Copy row 1..24 to row 0..23 */
    for (src_off = VIDEO_COLS * 2; src_off < VIDEO_BUF_SIZE; src_off += 2) {
        dst_off = src_off - VIDEO_COLS * 2;
        word = read_far_w(VIDEO_BUF_SEG, src_off);
        write_far_w(VIDEO_BUF_SEG, dst_off, word);
    }
    /* Clear last row */
    dst_off = 24 * VIDEO_COLS * 2;
    for (src_off = 0; src_off < VIDEO_COLS; src_off++) {
        write_far_w(VIDEO_BUF_SEG, dst_off + src_off * 2,
                    (uint16_t)(' ' | (VIDEO_ATTR_NORMAL << 8)));
    }
}

void video_clear(void)
{
    uint16_t i;

    for (i = 0; i < VIDEO_COLS * VIDEO_ROWS; i++) {
        write_far_w(VIDEO_BUF_SEG, i * 2,
                    (uint16_t)(' ' | (VIDEO_ATTR_NORMAL << 8)));
    }
    tty_row = 0;
    tty_col = 0;
    video_set_cursor(0, 0);
}

void video_tty_putchar(char c)
{
    if (c == '\r') {
        tty_col = 0;
    } else if (c == '\n') {
        tty_col = 0;
        if (tty_row < VIDEO_ROWS - 1) {
            tty_row++;
        } else {
            video_scroll();
        }
    } else if (c == '\b') {
        if (tty_col > 0) {
            tty_col--;
            video_putchar(tty_row, tty_col, ' ', VIDEO_ATTR_NORMAL);
        }
    } else if (c != 0) {
        video_putchar(tty_row, tty_col, c, VIDEO_ATTR_NORMAL);
        tty_col++;
        if (tty_col >= VIDEO_COLS) {
            tty_col = 0;
            if (tty_row < VIDEO_ROWS - 1) {
                tty_row++;
            } else {
                video_scroll();
            }
        }
    }
    video_set_cursor(tty_row, tty_col);
}

void video_tty_puts(const char *s)
{
    while (*s)
        video_tty_putchar(*s++);
}

void video_set_cursor(uint8_t row, uint8_t col)
{
    uint16_t pos;

    pos = (uint16_t)row * VIDEO_COLS + col;

    /* INT 10h AH=02h: set cursor position */
    /* Use inline assembly via pragma aux */
    port_out_b(0x3D4, 0x0F);   /* Cursor Location Low Register */
    port_out_b(0x3D5, (uint8_t)(pos & 0xFF));
    port_out_b(0x3D4, 0x0E);   /* Cursor Location High Register */
    port_out_b(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}