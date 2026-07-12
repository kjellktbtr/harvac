/* far.c -- Far-pointer memory operations for HarvaC HDK.
 * Identical to apps/medit/far.c and apps/ncd/far.c (merged into one copy). */

#include "types.h"
#include "harva.h"

void far_copy(uint8_t __far *dst, const uint8_t __far *src, uint16_t n)
{
    while (n--) *dst++ = *src++;
}

void far_move(uint8_t __far *dst, const uint8_t __far *src, uint16_t n)
{
    if (dst < (uint8_t __far *)src) {
        while (n--) *dst++ = *src++;
    } else {
        dst += n; src += n;
        while (n--) *--dst = *--src;
    }
}
