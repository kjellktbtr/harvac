#ifndef STR_H
#define STR_H

#include "types.h"

uint16_t m_strlen(const char *s);
int      m_strcmp(const char *a, const char *b);
char    *m_strcpy(char *dst, const char *src);
char    *m_strcat(char *dst, const char *src);
void    *m_memcpy(void *dst, const void *src, uint16_t n);
void    *m_memmove(void *dst, const void *src, uint16_t n);
void    *m_memset(void *dst, int c, uint16_t n);

#endif /* STR_H */
