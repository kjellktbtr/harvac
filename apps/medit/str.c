#include "str.h"

uint16_t m_strlen(const char *s)
{
    uint16_t n = 0;
    while (s[n]) n++;
    return n;
}

int m_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

char *m_strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++) != '\0')
        ;
    return dst;
}

char *m_strcat(char *dst, const char *src)
{
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++) != '\0')
        ;
    return dst;
}

void *m_memcpy(void *dst, const void *src, uint16_t n)
{
    char *d = (char *)dst;
    const char *s = (const char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *m_memset(void *dst, int c, uint16_t n)
{
    char *d = (char *)dst;
    while (n--) *d++ = (char)c;
    return dst;
}

void *m_memmove(void *dst, const void *src, uint16_t n)
{
    char *d = (char *)dst;
    const char *s = (const char *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}
