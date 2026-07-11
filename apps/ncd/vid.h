#ifndef VID_H
#define VID_H

#include "ncd.h"

void vid_init(void);
void vid_done(void);
void vid_putat(int row, int col, u8 ch, u8 attr);
void vid_puts(int row, int col, const char *s, u8 attr);
void vid_fill(int row, int col, int n, u8 ch, u8 attr);
void vid_flush(void);
void vid_cursor(int row, int col);
void vid_cursor_hide(void);

#endif /* VID_H */
