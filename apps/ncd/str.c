/* str.c -- String and memory helpers for NCD.
 * Freestanding implementations (no stdlib). */

#include "str.h"

u16 m_strlen(const char *s)
{
    u16 len = 0;
    while (s[len]) len++;
    return len;
}

int m_strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) { a++; b++; }
    return (u8)*a - (u8)*b;
}

char *m_strcpy(char *dst, const char *src)
{
    char *d = dst;
    while (*src) { *d++ = *src++; }
    *d = '\0';
    return dst;
}

char *m_strcat(char *dst, const char *src)
{
    char *d = dst;
    while (*d) d++;
    while (*src) { *d++ = *src++; }
    *d = '\0';
    return dst;
}

void *m_memcpy(void *dst, const void *src, u16 n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (n--) { *d++ = *s++; }
    return dst;
}

void *m_memmove(void *dst, const void *src, u16 n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    if (d < s) {
        while (n--) { *d++ = *s++; }
    } else {
        d += n; s += n;
        while (n--) { *--d = *--s; }
    }
    return dst;
}

void *m_memset(void *dst, int c, u16 n)
{
    u8 *d = (u8 *)dst;
    while (n--) { *d++ = (u8)c; }
    return dst;
}

int m_strcmpi(const char *a, const char *b)
{
    u8 ca, cb;
    while (*a && (*a == *b)) { a++; b++; }
    ca = (u8)*a;
    cb = (u8)*b;
    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;
    return ca - cb;
}

void m_itoa(u16 val, char *buf)
{
    u16 digits[6];
    u16 i = 0;
    u16 d;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    while (val > 0) {
        /* val % 10 and val / 10 via repeated subtraction */
        d = 0;
        while (val >= 10) { val -= 10; d++; }
        digits[i++] = (u8)val;
        val = d;
    }
    while (i > 0) { buf[6 - i] = (char)(digits[--i] + '0'); }
    buf[6] = '\0';
    /* Shift left to remove leading zeros */
    i = 0;
    while (buf[i] == '0' && buf[i+1]) i++;
    if (i > 0) {
        u16 j = 0;
        while (buf[i]) { buf[j++] = buf[i++]; }
        buf[j] = '\0';
    }
}

void m_u32toa(u32 val, char *buf)
{
    u8 digits[12];
    u8 i = 0;
    u32 d;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    while (val > 0) {
        /* val % 10 and val / 10 via repeated subtraction */
        d = 0;
        while (val >= 10) { val -= 10; d++; }
        digits[i++] = (u8)val;
        val = d;
    }
    while (i > 0) { buf[12 - i] = (char)(digits[--i] + '0'); }
    buf[12] = '\0';
    /* Shift left */
    i = 0;
    while (buf[i] == '0' && buf[i+1]) i++;
    if (i > 0) {
        u8 j = 0;
        while (buf[i]) { buf[j++] = buf[i++]; }
        buf[j] = '\0';
    }
}
