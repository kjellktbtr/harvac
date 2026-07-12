/* harva.h -- HarvaC OS-specific helpers: far-pointer macros, far copy/move,
 * FAT16 name/date/time formatting. Not part of the POSIX libc. */
#ifndef HARVA_H
#define HARVA_H

#include "types.h"

/* ─── Far pointer construction ─── */
/* These work with OpenWatcom's __far qualifier and seg_t / off_t types. */
#define MK_FP(seg, off)  ((void __far *)(((uint32_t)(seg) << 16) | (uint16_t)(off)))
#define FP_SEG(fp)       ((uint16_t)((uint32_t)(fp) >> 16))
#define FP_OFF(fp)       ((uint16_t)(fp))

/* ─── Far memory copy / move ─── */

/* Copy n bytes from far src to far dst (no overlap-safe version). */
void far_copy(uint8_t __far *dst, const uint8_t __far *src, uint16_t n);

/* Move n bytes (handles overlapping regions). */
void far_move(uint8_t __far *dst, const uint8_t __far *src, uint16_t n);

/* ─── FAT16 name formatting ─── */

/* A raw FAT16 directory entry (as seen in the kernel struct). */
typedef struct {
    uint8_t  name[8];
    uint8_t  ext[3];
    uint8_t  attrs;
    uint8_t  reserved[10];
    uint16_t time;
    uint16_t date;
    uint16_t first_cluster;
    uint32_t file_size;
} fat_dirent_t;

/* Build null-terminated 8.3 name from a raw fat_dirent_t.
 * out must be at least 13 bytes. */
void fat_format_name(const fat_dirent_t *ent, char *out);

/* Format "FILE.TXT" as "FILE     TXT" (8-char name, 1 space, 3-char ext).
 * Dirs without extension are left-aligned. out >= 13 bytes. */
void fat_format_name12(const char *name, char *out);

/* Format FAT16 date as "YYYY.MM.DD" (10 chars + NUL). out >= 11 bytes.
 * All spaces when date == 0. */
void fat_format_date(uint16_t date, char *out);

/* Format FAT16 time as "HH:MM" (5 chars + NUL). out >= 6 bytes.
 * All spaces when both date and time == 0. */
void fat_format_time(uint16_t date, uint16_t time, char *out);

/* Return 1 if ent is a "." or ".." directory entry (dot entry). */
int fat_is_dot_entry(const fat_dirent_t *ent);

/* Write `src` middle-truncated to exactly `width` chars into `out`.
 * If src fits, it is copied verbatim (NUL-terminated).
 * If it is too long, the result is "head...tail" where head and tail
 * are chosen so the total is `width`.  `out` must be width+1 bytes.
 * Minimum useful width is 5 (1 head + "..." + 1 tail). */
void fmt_ellipsis_mid(char *out, const char *src, uint16_t width);

/* Format a u32 integer right-aligned in a field of `width` ASCII digits.
 * Pads with spaces on the left.  out must be width+1 bytes. */
void fmt_uint_field(char *out, uint32_t val, uint16_t width);

/* FAT16 attribute bit (subset shared with constants.h DIR_ATTR_*) */
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_VOLUME     0x08
#define FAT_ATTR_READONLY   0x01

#endif /* HARVA_H */
