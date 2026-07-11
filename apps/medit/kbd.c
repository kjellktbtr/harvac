/* kbd.c -- Keyboard input for Harvac port of MEDIT.
 * Uses SYSCALL_READ_CHAR (returns 16-bit: scan<<8 | ascii) and
 * SYSCALL_GET_SHIFT (returns shift/ctrl/alt bitmask). */

#include "medit.h"
#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "kbd.h"

u8 kbd_shift;

unsigned kbd_get(void)
{
    u16 key;
    u8  sc, al, sh;

    key = (u16)syscall_int40(SYSCALL_READ_CHAR, 0, 0, 0, 0, 0, 0);
    sc  = (u8)(key >> 8);
    al  = (u8)(key & 0xFF);

    sh = (u8)syscall_int40(SYSCALL_GET_SHIFT, 0, 0, 0, 0, 0, 0);
    kbd_shift = 0;
    if (sh & 0x03) kbd_shift |= SH_SHIFT;
    if (sh & 0x04) kbd_shift |= SH_CTRL;
    if (sh & 0x08) kbd_shift |= SH_ALT;

    if (al == 0)
        return K_SPECIAL | sc;

    if (al == 0x1B)          /* serial console sends Esc as bare ASCII 27 */
        return K_ESC;

    return al;
}
