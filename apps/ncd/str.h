#ifndef STR_H
#define STR_H

#include "ncd.h"

u16  m_strlen(const char *s);
int  m_strcmp(const char *a, const char *b);
char *m_strcpy(char *dst, const char *src);
char *m_strcat(char *dst, const char *src);
void *m_memcpy(void *dst, const void *src, u16 n);
void *m_memmove(void *dst, const void *src, u16 n);
void *m_memset(void *dst, int c, u16 n);

/* Case-insensitive compare (ASCII only) */
int  m_strcmpi(const char *a, const char *b);

/* Check if string ends with a given suffix (case-insensitive) */
int  m_ends_with(const char *str, const char *suffix);

/* Integer to string helpers */
void m_itoa(u16 val, char *buf);
void m_u32toa(u32 val, char *buf);
void m_itoa_pad2(u16 val, char *buf);      /* "07" (val < 100) */
void m_format_size(u32 val, char *buf);    /* right-aligned 5 chars, K suffix */

#endif /* STR_H */
