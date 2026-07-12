/* string.h -- freestanding string/memory helpers for HarvaC userspace.
 * Real POSIX names; no standard library dependency. */
#ifndef STRING_H
#define STRING_H

#include "types.h"

/* --- Length / comparison --- */
uint16_t strlen(const char *s);
int      strcmp(const char *a, const char *b);
int      strncmp(const char *a, const char *b, uint16_t n);
int      strcasecmp(const char *a, const char *b);   /* case-insensitive */

/* --- Copy / concatenate --- */
char    *strcpy(char *dst, const char *src);
char    *strncpy(char *dst, const char *src, uint16_t n);
char    *strcat(char *dst, const char *src);
char    *strncat(char *dst, const char *src, uint16_t n);

/* --- Search --- */
char    *strchr(const char *s, int c);

/* --- Memory --- */
void    *memcpy(void *dst, const void *src, uint16_t n);
void    *memmove(void *dst, const void *src, uint16_t n);
void    *memset(void *dst, int c, uint16_t n);
int      memcmp(const void *a, const void *b, uint16_t n);

/* --- Harvac extensions (no POSIX equivalent) --- */
/* Case-insensitive suffix check (returns 1 if str ends with suffix). */
int      ends_with(const char *str, const char *suffix);

/* Convert uint16 to decimal string (null-terminated, buf >= 6 bytes). */
void     utoa(uint16_t val, char *buf);

/* Convert uint32 to decimal string (null-terminated, buf >= 11 bytes). */
void     u32toa(uint32_t val, char *buf);

/* Convert uint16 to 2-digit zero-padded decimal (buf >= 3 bytes). */
void     utoa_pad2(uint16_t val, char *buf);

/* Format byte size right-aligned in 5 chars (buf >= 6 bytes). */
void     format_size(uint32_t val, char *buf);

/* Simple decimal string to int (no error handling). */
int      atoi(const char *s);

#endif /* STRING_H */
