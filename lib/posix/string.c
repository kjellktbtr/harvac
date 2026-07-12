/* string.c -- Freestanding string/memory helpers for HarvaC userspace.
 * Based on apps/ncd/str.c (the richer superset); m_* prefixes dropped,
 * real POSIX names used throughout. */

#include "types.h"
#include "string.h"

uint16_t strlen(const char *s)
{
    uint16_t len = 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

int strncmp(const char *a, const char *b, uint16_t n)
{
    while (n > 0 && *a && (*a == *b)) { a++; b++; n--; }
    if (n == 0) return 0;
    return (uint8_t)*a - (uint8_t)*b;
}

int strcasecmp(const char *a, const char *b)
{
    uint8_t ca, cb;
    while (*a && (*a == *b)) { a++; b++; }
    ca = (uint8_t)*a;
    cb = (uint8_t)*b;
    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;
    return ca - cb;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while (*src) { *d++ = *src++; }
    *d = '\0';
    return dst;
}

char *strncpy(char *dst, const char *src, uint16_t n)
{
    char *d = dst;
    while (n > 0 && *src) { *d++ = *src++; n--; }
    while (n > 0) { *d++ = '\0'; n--; }
    return dst;
}

char *strcat(char *dst, const char *src)
{
    char *d = dst;
    while (*d) d++;
    while (*src) { *d++ = *src++; }
    *d = '\0';
    return dst;
}

char *strncat(char *dst, const char *src, uint16_t n)
{
    char *d = dst;
    while (*d) d++;
    while (n > 0 && *src) { *d++ = *src++; n--; }
    *d = '\0';
    return dst;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (char *)0;
}

void *memcpy(void *dst, const void *src, uint16_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) { *d++ = *s++; }
    return dst;
}

void *memmove(void *dst, const void *src, uint16_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) { *d++ = *s++; }
    } else {
        d += n; s += n;
        while (n--) { *--d = *--s; }
    }
    return dst;
}

void *memset(void *dst, int c, uint16_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n--) { *d++ = (uint8_t)c; }
    return dst;
}

int memcmp(const void *a, const void *b, uint16_t n)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    while (n--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}

int ends_with(const char *str, const char *suffix)
{
    uint16_t slen = strlen(str);
    uint16_t suflen = strlen(suffix);
    uint16_t i;
    char c1, c2;

    if (suflen > slen)
        return 0;

    for (i = 0; i < suflen; i++) {
        c1 = str[slen - suflen + i];
        c2 = suffix[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return 0;
    }
    return 1;
}

void utoa(uint16_t val, char *buf)
{
    uint8_t  digits[6];
    uint8_t  i = 0;
    uint16_t d;

    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }

    while (val > 0) {
        d = 0;
        while (val >= 10) { val -= 10; d++; }
        digits[i++] = (uint8_t)val;
        val = d;
    }
    {
        uint8_t j = i;
        while (j > 0)
            *buf++ = (char)(digits[--j] + '0');
        *buf = '\0';
    }
}

void utoa_pad2(uint16_t val, char *buf)
{
    buf[0] = (char)('0' + val / 10);
    buf[1] = (char)('0' + val % 10);
    buf[2] = '\0';
}

void u32toa(uint32_t val, char *buf)
{
    uint8_t  digits[12];
    uint8_t  i = 0;
    uint32_t d;

    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }

    while (val > 0) {
        d = 0;
        while (val >= 10) { val -= 10; d++; }
        digits[i++] = (uint8_t)val;
        val = d;
    }
    {
        uint8_t j = i;
        while (j > 0)
            *buf++ = (char)(digits[--j] + '0');
        *buf = '\0';
    }
}

void format_size(uint32_t val, char *buf)
{
    char tmp[12];
    uint16_t len, i;

    if (val > 99999UL) {
        u32toa(val >> 10, tmp);
        len = strlen(tmp);
        tmp[len++] = 'K';
        tmp[len] = '\0';
    } else {
        u32toa(val, tmp);
        len = strlen(tmp);
    }

    for (i = 0; i < 5; i++)
        buf[i] = ' ';
    buf[5] = '\0';
    if (len > 5) len = 5;
    memcpy(buf + 5 - len, tmp, len);
}

int atoi(const char *s)
{
    int n = 0, sign = 1;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return sign * n;
}
