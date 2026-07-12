/* hdk_video.h -- Text-mode video layer for HarvaC HDK.
 * Shadow buffer with dirty-row tracking. Flushes to VGA VRAM (0xB800)
 * via write_far_w. Cursor controlled via SYSCALL_SET_CURSOR and CRTC ports. */
#ifndef HDK_VIDEO_H
#define HDK_VIDEO_H

#include "types.h"

/* Screen dimensions */
#define ROWS  25
#define COLS  80

/* Initialise the shadow buffer (marks all rows dirty for a full repaint). */
void vid_init(void);

/* Done: clear the screen and restore the cursor. */
void vid_done(void);

/* Write a character+attribute at (row, col) into the shadow buffer.
 * No-op if out of bounds. Marks the row dirty. */
void vid_putat(int row, int col, uint8_t ch, uint8_t attr);

/* Write a NUL-terminated string at (row, col) with the given attribute. */
void vid_puts(int row, int col, const char *s, uint8_t attr);

/* Fill n cells starting at (row, col) with ch/attr. */
void vid_fill(int row, int col, int n, uint8_t ch, uint8_t attr);

/* Flush all dirty rows to VGA VRAM. */
void vid_flush(void);

/* Show the hardware cursor at (row, col). Restores cursor shape if hidden. */
void vid_cursor(int row, int col);

/* Hide the hardware cursor via CRTC. */
void vid_cursor_hide(void);

#endif /* HDK_VIDEO_H */
