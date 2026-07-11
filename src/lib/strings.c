/* strings.c -- String and memory utility functions */

#include "kernel.h"
#include "lib/strings.h"

uint16_t strlen(const char *s)
{
    uint16_t len = 0;
    while (*s++) len++;
    return len;
}

int16_t strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int16_t)((uint8_t)*a - (uint8_t)*b);
}

int16_t strncmp(const char *a, const char *b, uint16_t n)
{
    while (n > 0 && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0)
        return 0;
    return (int16_t)((uint8_t)*a - (uint8_t)*b);
}

void strcpy(char *dst, const char *src)
{
    while (*src)
        *dst++ = *src++;
    *dst = '\0';
}

void memset(void *dst, uint8_t val, uint16_t len)
{
    uint8_t *p = (uint8_t *)dst;
    while (len--)
        *p++ = val;
}

void memcpy(void *dst, const void *src, uint16_t len)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (len--)
        *d++ = *s++;
}

void itoa(int16_t val, char *buf, uint8_t base)
{
    char tmp[33];
    int16_t i = 0;
    uint8_t negative = 0;

    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    if (val < 0 && base == 10) {
        negative = 1;
        val = -val;
    }

    while (val > 0) {
        uint8_t digit = (uint8_t)(val % base);
        tmp[i++] = (digit < 10) ? (char)('0' + digit)
                                : (char)('A' + digit - 10);
        val /= base;
    }

    if (negative)
        tmp[i++] = '-';

    {
        int16_t j;
        for (j = 0; j < i; j++)
            buf[j] = tmp[i - 1 - j];
        buf[i] = '\0';
    }
}

void hex_to_str(uint32_t val, char *buf)
{
    uint8_t i;
    uint8_t nibble;
    uint8_t leading;
    int8_t shift;

    buf[0] = '0';
    buf[1] = 'x';
    i = 2;
    leading = 1;

    for (shift = 28; shift >= 0; shift -= 4) {
        nibble = (uint8_t)((val >> shift) & 0x0F);
        if (nibble != 0 || !leading || shift == 0) {
            leading = 0;
            buf[i++] = (nibble < 10) ? (char)('0' + nibble)
                                     : (char)('A' + nibble - 10);
        }
    }
    buf[i] = '\0';
}