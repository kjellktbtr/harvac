/* hdk_keys.h -- Normalized keyboard input for HarvaC HDK.
 * Shared key codes and shift flags used by medit, ncd, and any HDK app. */
#ifndef HDK_KEYS_H
#define HDK_KEYS_H

#include "types.h"

/* Normalized key code scheme:
 * 0x0001-0x00FF = ASCII / cp865 byte value
 * 0x0100+       = K_SPECIAL | scan_code  (extended / special keys) */
#define K_SPECIAL   0x100

#define K_UP        (K_SPECIAL|0x48)
#define K_DOWN      (K_SPECIAL|0x50)
#define K_LEFT      (K_SPECIAL|0x4B)
#define K_RIGHT     (K_SPECIAL|0x4D)
#define K_HOME      (K_SPECIAL|0x47)
#define K_END       (K_SPECIAL|0x4F)
#define K_PGUP      (K_SPECIAL|0x49)
#define K_PGDN      (K_SPECIAL|0x51)
#define K_INS       (K_SPECIAL|0x52)
#define K_DEL       (K_SPECIAL|0x53)
#define K_ESC       (K_SPECIAL|0x01)

/* Function keys */
#define K_F1        (K_SPECIAL|0x3B)
#define K_F2        (K_SPECIAL|0x3C)
#define K_F3        (K_SPECIAL|0x3D)
#define K_F4        (K_SPECIAL|0x3E)
#define K_F5        (K_SPECIAL|0x3F)
#define K_F6        (K_SPECIAL|0x40)
#define K_F7        (K_SPECIAL|0x41)
#define K_F8        (K_SPECIAL|0x42)
#define K_F9        (K_SPECIAL|0x43)
#define K_F10       (K_SPECIAL|0x44)

/* Ctrl + cursor keys */
#define K_CHOME     (K_SPECIAL|0x77)
#define K_CEND      (K_SPECIAL|0x75)
#define K_CPGUP     (K_SPECIAL|0x84)
#define K_CPGDN     (K_SPECIAL|0x76)
#define K_CLEFT     (K_SPECIAL|0x73)
#define K_CRIGHT    (K_SPECIAL|0x74)

/* Alt + letter keys (medit menu shortcuts) */
#define K_ALT_F     (K_SPECIAL|0x21)
#define K_ALT_R     (K_SPECIAL|0x13)
#define K_ALT_S     (K_SPECIAL|0x1F)

/* ASCII control codes (returned as plain values, not K_SPECIAL) */
#define K_ENTER     0x0D
#define K_BS        0x08
#define K_TAB       0x09

/* Shift modifier bits (stored in kbd_shift after kbd_get()) */
#define SH_SHIFT    0x01
#define SH_CTRL     0x02
#define SH_ALT      0x04

/* Read one key event (blocking). Returns a normalized key code (K_* or ASCII).
 * After the call, kbd_shift holds the modifier bitmask (SH_*). */
unsigned int kbd_get(void);

/* Modifier state from the last kbd_get() call */
extern uint8_t kbd_shift;

#endif /* HDK_KEYS_H */
