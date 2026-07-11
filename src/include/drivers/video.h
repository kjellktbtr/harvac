#ifndef DRIVERS_VIDEO_H
#define DRIVERS_VIDEO_H

#include "types.h"

/* VGA text mode dimensions */
#define VIDEO_COLS      80
#define VIDEO_ROWS      25
#define VIDEO_BUF_SEG   0xB800
#define VIDEO_BUF_SIZE  (VIDEO_COLS * VIDEO_ROWS * 2)

/* Default attributes */
#define VIDEO_ATTR_NORMAL   0x07  /* light gray on black */
#define VIDEO_ATTR_HIGHLIGHT 0x0F /* white on black */
#define VIDEO_ATTR_ERROR     0x0C /* red on black */
#define VIDEO_ATTR_STATUS    0x02 /* green on black */
#define VIDEO_ATTR_INPUT     0x1F /* white on blue */

/* Initialize VGA text mode: clear screen, set cursor to (0,0) */
void video_init(void);

/* Write a single character at (row, col) with attribute */
void video_putchar(uint8_t row, uint8_t col, char c, uint8_t attr);

/* Write a null-terminated string at (row, col) with attribute */
void video_write(uint8_t row, uint8_t col, const char *s, uint8_t attr);

/* Scroll the screen up by one line */
void video_scroll(void);

/* Clear the entire screen (fill with spaces) */
void video_clear(void);

/* Move the hardware cursor via INT 10h */
void video_set_cursor(uint8_t row, uint8_t col);

/* Stateful terminal output: tracks cursor, handles \r \n \b, scrolls */
void video_tty_putchar(char c);
void video_tty_puts(const char *s);

#endif /* DRIVERS_VIDEO_H */