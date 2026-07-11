#ifndef GAP_H
#define GAP_H
#include "medit.h"

/* Gap buffer in a dedicated far segment (outside the 64 kB COM image).
 * Logical text = [0, gb_len()); the gap is invisible to callers.
 * All offsets are 16-bit; capacity is at most GB_WANT_PARAS*16 bytes. */

#define GB_WANT_PARAS 0x0BB8    /* 48000 bytes requested (fits Harvac memory) */
#define GB_MAX_TEXT   47000U    /* refuse larger files */

int  gb_init(void);             /* 0 on success */
void gb_reset(void);            /* empty the buffer */
u16  gb_len(void);
u16  gb_cap(void);
u8   gb_at(u16 pos);
int  gb_insert(u16 pos, u8 c);  /* 0 on success, -1 if full */
int  gb_insert_block(u16 pos, const u8 __far *src, u16 n);
void gb_del(u16 pos, u16 n);
void gb_copy_out(u16 pos, u16 n, u8 __far *dst);

/* Raw access for whole-buffer load/save (doc.c only): gb_raw moves
 * the gap to the end so the text is contiguous at the returned base;
 * gb_set_len declares the new text length after a raw fill. */
u8 __far *gb_raw(void);
void gb_set_len(u16 n);

#endif
