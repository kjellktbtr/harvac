#ifndef KBD_H
#define KBD_H
#include "medit.h"

/* Blocking read; returns normalized key code (see medit.h).
 * Updates kbd_shift with SH_* flags sampled at the keystroke. */
unsigned kbd_get(void);

extern u8 kbd_shift;

#endif
