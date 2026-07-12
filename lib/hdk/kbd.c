/* kbd.c -- Normalized keyboard input for HarvaC HDK.
 * Uses SYSCALL_READ_CHAR (scan<<8 | ascii) and SYSCALL_GET_SHIFT.
 * Merged from apps/medit/kbd.c and apps/ncd/kbd.c (byte-for-byte identical). */

#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "hdk_keys.h"

uint8_t kbd_shift;

unsigned int kbd_get(void)
{
    uint16_t key;
    uint8_t  sc, al, sh;

    key = (uint16_t)syscall_int40(SYSCALL_READ_CHAR, 0, 0, 0, 0, 0, 0);
    sc  = (uint8_t)(key >> 8);
    al  = (uint8_t)(key & 0xFF);

    sh = (uint8_t)syscall_int40(SYSCALL_GET_SHIFT, 0, 0, 0, 0, 0, 0);
    kbd_shift = 0;
    if (sh & 0x03) kbd_shift |= SH_SHIFT;
    if (sh & 0x04) kbd_shift |= SH_CTRL;
    if (sh & 0x08) kbd_shift |= SH_ALT;

    if (al == 0)
        return K_SPECIAL | sc;

    if (al == 0x1B)
        return K_ESC;

    return al;
}
