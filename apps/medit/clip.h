#ifndef CLIP_H
#define CLIP_H
#include "medit.h"

/* Internal clipboard in a dynamically allocated far block. */
int  clip_set(u16 pos, u16 n);      /* copy n bytes at text pos; 0 = ok */
u16  clip_len(void);
u8 __far *clip_ptr(void);           /* 0 when empty */

#endif
