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

int m_ends_with(const char *str, const char *suffix)
{
    u16 slen = m_strlen(str);
    u16 suflen = m_strlen(suffix);
    u16 i;

    if (suflen > slen)
        return 0;

    for (i = 0; i < suflen; i++) {
        char c1 = str[slen - suflen + i];
        char c2 = suffix[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return 0;
    }
    return 1;
}

void m_itoa(u16 val, char *buf)
{
    u8  digits[6];
    u8  i = 0;
    u16 d;

    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }

    while (val > 0) {
        d = 0;
        while (val >= 10) { val -= 10; d++; }
        digits[i++] = (u8)val;
        val = d;
    }

    /* Write digits in reverse order (right-to-left fill) */
    {
        u8 j = i;
        while (j > 0)
            *buf++ = (char)(digits[--j] + '0');
        *buf = '\0';
    }
}

/* Two-digit zero-padded decimal ("07"); val must be < 100 */
void m_itoa_pad2(u16 val, char *buf)
{
    buf[0] = (char)('0' + val / 10);
    buf[1] = (char)('0' + val % 10);
    buf[2] = '\0';
}

/* Format a byte size right-aligned in 5 chars.
 * <= 99999 in bytes, larger as kilobytes with 'K' suffix ("4096K"). */
void m_format_size(u32 val, char *buf)
{
    char tmp[12];
    u16 len, i;

    if (val > 99999UL) {
        m_u32toa(val >> 10, tmp);
        len = m_strlen(tmp);
        tmp[len++] = 'K';
        tmp[len] = '\0';
    } else {
        m_u32toa(val, tmp);
        len = m_strlen(tmp);
    }

    for (i = 0; i < 5; i++)
        buf[i] = ' ';
    buf[5] = '\0';
    if (len > 5)
        len = 5;
    m_memcpy(buf + 5 - len, tmp, len);
}

void m_u32toa(u32 val, char *buf)
{
    u8  digits[12];
    u8  i = 0;
    u32 d;

    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }

    while (val > 0) {
        d = 0;
        while (val >= 10) { val -= 10; d++; }
        digits[i++] = (u8)val;
        val = d;
    }

    /* Write digits in reverse order (right-to-left fill) */
    {
        u8 j = i;
        while (j > 0)
            *buf++ = (char)(digits[--j] + '0');
        *buf = '\0';
    }
}
