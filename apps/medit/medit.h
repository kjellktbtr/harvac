/* MEDIT - Norwegian text editor for PC DOS 3.3+ / IBM 5155 (256 kB)
 * Common types, key codes, cp865 character macros.
 * All sources are plain ASCII; Norwegian letters use cp865 escapes.
 */
#ifndef MEDIT_H
#define MEDIT_H

typedef unsigned char  u8;
typedef unsigned int   u16;   /* 16-bit on OpenWatcom 16-bit targets */
typedef unsigned long  u32;

/* cp865 (Nordic) codes for aa/ae/oe - same values as cp437 for these six */
#define S_aa "\x86"   /* a-ring lower  */
#define S_AA "\x8F"   /* A-ring upper  */
#define S_ae "\x91"   /* ae lower      */
#define S_AE "\x92"   /* AE upper      */
#define S_oe "\x9B"   /* o-slash lower */
#define S_OE "\x9D"   /* O-slash upper */

/* Screen layout */
#define ROWS        25
#define COLS        80
#define TEXT_TOP    1          /* first text row (row 0 = menu bar)     */
#define TEXT_ROWS   23         /* rows 1..23 text, row 24 = status line */
#define STATUS_ROW  24

/* Attributes - mono-friendly (5155 composite / MDA safe) */
#define A_TEXT      0x07
#define A_BAR       0x70       /* menu bar / status line: inverse */
#define A_SEL       0x70       /* selection: inverse */

/* Normalized key codes from kbd_get():
 * 0x0000-0x00FF = ASCII / cp865 byte; 0x01nn = special (nn = scan code) */
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
#define K_CHOME     (K_SPECIAL|0x77)
#define K_CEND      (K_SPECIAL|0x75)
#define K_CPGUP     (K_SPECIAL|0x84)
#define K_CPGDN     (K_SPECIAL|0x76)
#define K_CLEFT     (K_SPECIAL|0x73)
#define K_CRIGHT    (K_SPECIAL|0x74)
#define K_ALT_F     (K_SPECIAL|0x21)
#define K_ALT_R     (K_SPECIAL|0x13)
#define K_ALT_S     (K_SPECIAL|0x1F)
#define K_F1        (K_SPECIAL|0x3B)
#define K_F3        (K_SPECIAL|0x3D)
#define K_ESC       (K_SPECIAL|0x01) /* kernel gives ESC as scancode, ASCII 0 */
#define K_ENTER     0x0D
#define K_BS        0x08
#define K_TAB       0x09

/* kbd_shift bits (valid after kbd_get) */
#define SH_SHIFT    0x01
#define SH_CTRL     0x02
#define SH_ALT      0x04

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

/* --- Harvac port: string function aliases via str.h --- */
#include "str.h"
#define strlen  m_strlen
#define strcmp  m_strcmp
#define strcpy  m_strcpy
#define strcat  m_strcat
#define memcpy  m_memcpy
#define memmove m_memmove
#define memset  m_memset

#endif
