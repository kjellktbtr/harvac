---
title: Harva Development Kit (lib/hdk)
type: code-map
sources:
  - lib/include/hdk_video.h
  - lib/include/hdk_keys.h
  - lib/include/hdk_box.h
  - lib/include/hdk_dialog.h
  - lib/include/harva.h
  - lib/hdk/vid.c
  - lib/hdk/kbd.c
  - lib/hdk/dlg.c
  - lib/hdk/far.c
  - lib/hdk/fmt.c
related:
  - "[[posix-api]]"
  - "[[medit-harvac]]"
  - "[[ncd]]"
  - "[[user-apps]]"
created: 2026-07-12
updated: 2026-07-12
confidence: high
---

# Harva Development Kit (HDK)

`lib/hdk/` provides TUI primitives shared between EDIT.COM and NCD.COM: a
shadow-buffer video layer, normalized keyboard input, modal dialogs, box-drawing
constants, far-memory helpers, and FAT16 name/date/time formatting.

Headers in `lib/include/`, sources in `lib/hdk/`, compiled by `build.py:build_libs()`.

## Video — `hdk_video.h` / `vid.c`

Shadow buffer (80×25 `uint16_t` cells) with per-row dirty tracking. Flushes only
changed rows to VGA VRAM (segment 0xB800) via `write_far_w`.

```c
#define ROWS  25
#define COLS  80

void vid_init(void);                                 /* clear shadow, mark all rows dirty */
void vid_done(void);                                 /* SYSCALL_CLEAR_SCREEN + restore cursor */
void vid_putat(int row, int col, uint8_t ch, uint8_t attr);
void vid_puts(int row, int col, const char *s, uint8_t attr);
void vid_fill(int row, int col, int n, uint8_t ch, uint8_t attr);
void vid_flush(void);                                /* write dirty rows to VGA VRAM */
void vid_cursor(int row, int col);                   /* show cursor at position */
void vid_cursor_hide(void);                          /* hide via CRTC register 0x3D4/0x3D5 */
```

`vid_init(void)` — no `fastmode` parameter (medit's original unused parameter was dropped).

## Keyboard — `hdk_keys.h` / `kbd.c`

```c
unsigned int kbd_get(void);   /* blocking; returns K_* code or ASCII */
extern uint8_t kbd_shift;     /* SH_SHIFT/SH_CTRL/SH_ALT after kbd_get() */
```

Key code scheme: `0x01–0xFF` = ASCII/cp865, `K_SPECIAL|scan` = extended key.

| Constant | Value |
|----------|-------|
| `K_SPECIAL` | 0x100 |
| `K_UP/DOWN/LEFT/RIGHT` | 0x148/150/14B/14D |
| `K_HOME/END/PGUP/PGDN` | 0x147/14F/149/151 |
| `K_INS/DEL` | 0x152/153 |
| `K_ESC` | 0x101 |
| `K_F1..K_F10` | 0x13B..0x144 |
| `K_ENTER/K_BS/K_TAB` | 0x0D/0x08/0x09 |
| `K_ALT_F/R/S` | 0x121/113/11F |
| `SH_SHIFT/CTRL/ALT` | 0x01/0x02/0x04 |

## Box Drawing — `hdk_box.h`

cp437 single-line box characters and shared video attribute constants:

| Constant | Char | Value |
|----------|------|-------|
| `BOX_TL/TR/BL/BR` | ┌┐└┘ | 0xDA/BF/C0/D9 |
| `BOX_H / BOX_V` | ─ │ | 0xC4 / 0xB3 |
| `BOX_TJ/BJ/LJ/RJ/CJ` | ┬┴├┤┼ | 0xC2/C1/C3/B4/C5 |
| `BOX2_*` | ╔╗╚╝═║ | double-line variants |
| `A_NORMAL` | grey on black | 0x07 |
| `A_BOLD` | bright white on black | 0x0F |
| `A_INVERSE` | black on grey | 0x70 |
| `A_DLG / A_DLG_HI` | dialog body / selected widget | 0x70 / 0x07 |
| `A_DLG_TITLE` | dialog title | 0x70 |

## Dialogs — `hdk_dialog.h` / `dlg.c`

### Language

```c
extern int hdk_lang;           /* HDK_LANG_EN (0) or HDK_LANG_NO (1) */
```

Set at app startup. Selects button labels (English or Norwegian).

### Button sets

| Constant | Value | Buttons shown |
|----------|-------|--------------|
| `DB_OK` | 0 | OK |
| `DB_YESNO` | 1 | Yes / No |
| `DB_YESNOCA` | 2 | Yes / No / Cancel |
| `DB_YESNOALL` | 3 | Yes / No / All / Cancel |
| `DB_JNA` | = DB_YESNOCA | Ja / Nei / Avbryt (Norwegian alias) |
| `DB_JNAA` | = DB_YESNOALL | Ja / Nei / Alle / Avbryt (Norwegian alias) |

### Result codes

| Constant | Value | Aliases |
|----------|-------|---------|
| `HDK_CANCEL` | 0 | `DR_AVBRYT`, `DR_CANCEL` |
| `HDK_OK / HDK_YES` | 1 | `DR_JA`, `DR_OK`, `DR_YES` |
| `HDK_NO` | 2 | `DR_NEI`, `DR_NO` |
| `HDK_ALL` | 3 | `DR_ALLE` |

### Functions

```c
int dlg_msgbox(const char *text, int buttons);      /* returns HDK_* */
int dlg_input(const char *title, char *buf, int max); /* returns HDK_OK / HDK_CANCEL */
void dlg_draw_frame(int top, int left, int h, int w);
void dlg_draw_button(int row, int col, const char *label, int selected);
void dlg_field_draw(int row, int col, int w, HdkField *f);
int  dlg_field_key(HdkField *f, unsigned k);        /* returns 1 if key consumed */
```

`HdkField` — text input state:
```c
typedef struct {
    char *buf;      /* edit buffer (caller-allocated, max+1 bytes) */
    int   max;      /* max characters */
    int   len;      /* current length */
    int   pos;      /* cursor position */
    int   uppercase; /* 1 = force uppercase on insert */
} HdkField;
```

## Far Memory & FAT Formatting — `harva.h` / `far.c` + `fmt.c`

### Far pointer macros
```c
#define MK_FP(seg, off)   /* construct far pointer */
#define FP_SEG(fp)        /* extract segment */
#define FP_OFF(fp)        /* extract offset */
```

### Far copy/move
```c
void far_copy(uint8_t __far *dst, const uint8_t __far *src, uint16_t n);
void far_move(uint8_t __far *dst, const uint8_t __far *src, uint16_t n);
```
Used by EDIT.COM's gap buffer and NCD's panel entry storage.

### FAT16 dirent type
```c
typedef struct {
    uint8_t  name[8]; ext[3]; attrs; reserved[10];
    uint16_t time, date, first_cluster;
    uint32_t file_size;
} fat_dirent_t;
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_VOLUME    0x08
#define FAT_ATTR_READONLY  0x01
```

### FAT16 name/date/time formatting
```c
void fat_format_name(const fat_dirent_t *ent, char *out);          /* out >= 13 bytes */
void fat_format_name12(const char *name, char *out);               /* "FILE     TXT" */
void fat_format_date(uint16_t date, char *out);                    /* "YYYY.MM.DD" */
void fat_format_time(uint16_t date, uint16_t time, char *out);     /* "HH:MM" */
int  fat_is_dot_entry(const fat_dirent_t *ent);                    /* 1 = "." or ".." */
```

## Migration notes

Both EDIT.COM and NCD.COM previously shipped private copies of `vid.c`, `kbd.c`,
`far.c`, `str.c`, and `dlg.c`. These have been deleted; both apps now link against
the shared HDK objects. App-specific dialogs (medit's `dlg_filebox`) remain in their
app directories but are built on HDK primitives (`dlg_draw_frame`, `HdkField`, etc.).

NCD's `ncd_format_*` / `ncd_is_dot_entry` are now `#define` macros forwarding to
`fat_format_*` / `fat_is_dot_entry` (see `apps/ncd/fs.h`).
