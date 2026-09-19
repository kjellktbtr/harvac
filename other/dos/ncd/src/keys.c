/* keys.c -- Keyboard input handling for DOS NCD
 * Uses Watcom _bios_keybrd() for keyboard input.
 */

#include "dos_ncd.h"
#include <bios.h>

/* Global shift state */
u8 kbd_shift = 0;

/* Wait for a key press and return scan code + ASCII */
u16 kbd_get(void)
{
    u16 key;

    /* _KEYBRD_READ (0): read next character */
    key = _bios_keybrd(_KEYBRD_READ);

    /* Update shift state */
    kbd_shift = _bios_keybrd(_KEYBRD_SHIFTSTATUS);

    /* Return scan code in high byte, ASCII in low byte */
    return key;
}

/* Check if a key is available (non-blocking) */
int kbd_kbhit(void)
{
    /* _KEYBRD_READY (1): check for keystroke
     * Returns 0 if no key, non-zero if key available */
    return _bios_keybrd(_KEYBRD_READY) != 0;
}

/* Get shift state directly from BIOS */
u8 kbd_get_shift(void)
{
    return _bios_keybrd(_KEYBRD_SHIFTSTATUS);
}

/* Flush keyboard buffer */
void kbd_flush(void)
{
    while (kbd_kbhit()) {
        kbd_get();
    }
}