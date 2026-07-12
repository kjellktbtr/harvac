/* MEDIT - Norwegian text editor for Harvac OS.
 * Common types, layout constants, and shared library includes.
 * Key codes, string helpers, and dialogs come from lib/include/.
 */
#ifndef MEDIT_H
#define MEDIT_H

/* Local type aliases (compatible with types.h uint*_t) */
typedef unsigned char  u8;
typedef unsigned int   u16;
typedef unsigned long  u32;

/* cp865 (Nordic) codes for aa/ae/oe - same values as cp437 for these six */
#define S_aa "\x86"   /* a-ring lower  */
#define S_AA "\x8F"   /* A-ring upper  */
#define S_ae "\x91"   /* ae lower      */
#define S_AE "\x92"   /* AE upper      */
#define S_oe "\x9B"   /* o-slash lower */
#define S_OE "\x9D"   /* O-slash upper */

/* ─── Shared library headers ─── */
#include "types.h"
#include "string.h"
#include "harva.h"
#include "hdk_video.h"    /* ROWS, COLS, vid_* */
#include "hdk_keys.h"     /* K_*, SH_*, kbd_get, kbd_shift */
#include "hdk_box.h"      /* BOX_*, A_NORMAL, A_INVERSE, A_DLG, A_DLG_HI */
#include "hdk_dialog.h"   /* dlg_msgbox, dlg_input, DB_*, DR_*, HDK_* */

/* Screen layout — ROWS=25 / COLS=80 from hdk_video.h */
#define TEXT_TOP    1          /* first text row (row 0 = menu bar)     */
#define TEXT_ROWS   23         /* rows 1..23 text, row 24 = status line */
#define STATUS_ROW  24

/* Attributes - mono-friendly (5155 composite / MDA safe) */
#define A_TEXT      0x07
#define A_BAR       0x70       /* menu bar / status line: inverse */
#define A_SEL       0x70       /* selection: inverse */

#define TABSTOP     8

/* Menu action ids (0 = none) */
#define MI_NY        1
#define MI_APNE      2
#define MI_LAGRE     3
#define MI_LAGRESOM  4
#define MI_AVSLUTT   5
#define MI_KLIPP     6
#define MI_KOPIER    7
#define MI_LIM       8
#define MI_BRYT      9
#define MI_FINN     10
#define MI_FINNNESTE 11
#define MI_ERSTATT  12

#endif /* MEDIT_H */
