#ifndef KBD_H
#define KBD_H

#include "ncd.h"

/* Blocking read; returns normalized key code (see ncd.h).
 * Updates kbd_shift with SH_* flags sampled at the keystroke. */
unsigned kbd_get(void);

extern u8 kbd_shift;

#endif /* KBD_H */
