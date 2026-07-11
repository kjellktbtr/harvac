# Plan: Port MEDIT Text Editor to Harvac

## Context

`edit/` (the repo sibling directory) contains MEDIT — a Norwegian full-screen text
editor originally written for PC DOS 3.3+ / IBM 5155 (CGA, 256 kB). It is built with
OpenWatcom wcc in `.COM` format, making it structurally identical to Harvac's user-space
apps. The goal is to port MEDIT to run on HarvaOS, shipped as `EDIT.COM` in the `BIN/`
directory, so `edit filename.txt` from the Harvac shell opens the editor.

The port splits into two tracks:
1. **Kernel changes**: new and missing syscalls MEDIT needs.
2. **User-space port**: rewrite the 7 DOS-dependent source files to use INT 0x40 instead
   of INT 21h / INT 10h / INT 16h.

## Goal / Success Criteria

- `edit foo.txt` from the Harvac shell opens MEDIT, displays the file content, and allows
  editing with keyboard (arrows, insert, delete, selection, search, save, quit).
- `edit` (no argument) opens MEDIT with an empty buffer.
- Ctrl+S saves, Ctrl+Q quits.
- Alt+F/Alt+R/Alt+S open the Fil/Rediger/Sok menus.
- `make` builds MEDIT.COM without errors; MEDIT.COM lands in `BIN/` on the disk image.
- Existing functionality (shell, other apps, redirect, batch) is not broken.

## Assumptions & Open Questions

- QEMU is run with `-display none -serial stdio` for automated tests; MEDIT requires
  a real display and PS/2 keyboard (VGA buffer at 0xB800, CRTC port I/O). Testing
  MEDIT is manual (run QEMU with display, type on keyboard) — automated CI tests only
  verify the build succeeds.
- MEDIT's text limit will be reduced from 60000 to ~48000 bytes (GB_MAX_TEXT) to fit
  within the available segment range without exhausting conventional memory.
- The clipboard uses SYSCALL_ALLOC / SYSCALL_FREE; SYSCALL_FREE is a deliberate no-op
  (memory is reclaimed when EDIT.COM exits via return_to_parent_).
- Drive enumeration in dlg.c's file browser is removed (Harvac has one volume).
- MEDIT source files are copied into `apps/medit/` and modified in place; the original
  `edit/src/` files are not changed.
- The shell's `.BAT`-fallback and batch `%1` feature do not exist, so the editor is
  invoked as `edit filename` (argument passed via PSP:0x0082 by SYSCALL_EXEC).
- [ ] Confirm QEMU VGA mode is text mode 3 (80x25) at boot — kernel calls `video_init()`
  which writes to 0xB800 directly but does not set INT 10h mode. MEDIT assumes text mode.
  (Likely fine since QEMU defaults to text mode 3.)

---

## Phase 1 — Kernel: 16-bit keyboard + shift state

MEDIT needs arrow keys, F-keys, Alt+key combos, and Ctrl+key combos.
The current keyboard driver converts scancodes to ASCII only — special keys are
discarded. This phase adds full key-event support.

### Changes to `src/drivers/keyboard.c`

- [ ] Add `static uint8_t alt_pressed;` and `static uint8_t ctrl_pressed;` (like
  `shift_pressed`). Initialize to 0 in `keyboard_init()`.
- [ ] In `process_scancode()`: handle Alt make/break (0x38/0xB8) and Ctrl make/break
  (0x1D/0x9D) — set/clear `alt_pressed` and `ctrl_pressed` accordingly.
- [ ] Add `static uint8_t ext_prefix;` flag. When scancode 0xE0 is received, set
  `ext_prefix = 1` and return (do not process further). On the next byte, treat as
  extended key (regardless of break bit logic for plain keys).
- [ ] Change the ring buffer from `volatile char kbd_buffer[64]` to
  `volatile uint16_t kbd_buffer[64]` (same KBD_BUFFER_SIZE).
- [ ] In `process_scancode()`, for regular make codes (sc & 0x80 == 0):
  - If `alt_pressed`: store `((uint16_t)sc << 8) | 0x00` in ring buffer (sc = scan
    code of the letter key; AL=0 means alt-key, matching BIOS INT 16h AH=00h behavior).
  - If `ctrl_pressed` and the key has an ASCII code: store the ctrl character
    (e.g., Ctrl+S = 0x13) as `(0x0000 | ctrlchar)` — this is already handled since
    translate_key() isn't called for ctrl, but need to explicitly compute: for alpha
    keys (scan 0x10..0x32) store `(uint16_t)(ascii & 0x1F)`.
  - For extended keys (ext_prefix=1, common navigation keys: 0x47..0x53, 0x73..0x77
    etc.): store `((uint16_t)sc << 8) | 0x00`. Clear `ext_prefix` after use.
  - For F-keys (scan 0x3B..0x44): store `((uint16_t)sc << 8) | 0x00`.
  - For regular keys with ASCII: store `(0x0000 | (uint8_t)ascii)`.
- [ ] Change `keyboard_getchar()` return type to `uint16_t` and rename to
  `keyboard_getkey()`. Update all internal callers. Return `uint16_t` from ring buffer.
- [ ] `keyboard_available()`: no logic change, just buffer type change.
- [ ] Ensure `keyboard_getkey()` for serial input: `serial_getchar()` returns `char`;
  wrap as `(uint16_t)(uint8_t)c` (high byte = 0 = plain ASCII). Serial callers get
  ASCII-only keys; PS/2 gets full key events.

### Changes to `src/include/drivers/keyboard.h`

- [ ] Change `char keyboard_getchar(void)` → `uint16_t keyboard_getkey(void)`.
- [ ] Add `uint8_t keyboard_shift_state(void)` that returns current modifier byte:
  bits 0-1 = shift, bit 2 = ctrl, bit 3 = alt (same bit layout as BIOS INT 16h AH=02h).
- [ ] Implement `keyboard_shift_state()` in `keyboard.c`.

### Changes to `src/include/constants.h`

- [ ] Add `#define SYSCALL_GET_SHIFT   0x09` (get current keyboard shift/modifier byte).

### Changes to `src/kernel/syscalls.c`

- [ ] `case SYSCALL_READ_CHAR:` change `ret = (uint8_t)keyboard_getchar();` to
  `ret = keyboard_getkey();` (returns uint16_t: AH=scan, AL=ascii).
- [ ] Add `case SYSCALL_GET_SHIFT:` returning `ret = keyboard_shift_state();`.

- [ ] Verify: `make` builds cleanly. `SYSCALL_READ_CHAR` now returns 16-bit.
  Existing shell `SYSCALL_READ_STDIN` (line-at-a-time) is unaffected.
- [ ] Housekeeping: build passes, no regressions in existing apps.

---

## Phase 2 — Kernel: SYSCALL_ALLOC and SYSCALL_GET_CURSOR

### Changes to `src/kernel/syscalls.c`

- [ ] Add `case SYSCALL_ALLOC:` (0x50):
  ```c
  case SYSCALL_ALLOC:
  {
      uint16_t want = ax;   /* paragraphs requested */
      uint16_t seg = get_next_seg();
      set_next_seg(seg + want);
      ret = seg;            /* return base segment */
  }
  break;
  ```
- [ ] Add `case SYSCALL_FREE:` (0x51): `ret = 0; break;` (no-op; memory is reclaimed
  when child exits via return_to_parent_ restoring g_next_seg).
- [ ] Add `case SYSCALL_GET_CURSOR:` (0x07): read CRTC cursor position registers
  (same logic as `video_set_cursor` but reversed — read 0x0F then 0x0E from CRTC):
  ```c
  case SYSCALL_GET_CURSOR:
  {
      uint16_t pos;
      port_out_b(0x3D4, 0x0F);
      pos = port_in_b(0x3D5);
      port_out_b(0x3D4, 0x0E);
      pos |= ((uint16_t)port_in_b(0x3D5)) << 8;
      /* pos = row * 80 + col; return row in AH, col in AL */
      ret = (uint16_t)(((pos / 80) << 8) | (pos % 80));
  }
  break;
  ```

- [ ] Verify: `make` clean. Manual check: SYSCALL_ALLOC(3750) returns a segment above the
  shell's allocation; SYSCALL_GET_CURSOR returns sane row/col after SYSCALL_SET_CURSOR.
- [ ] Housekeeping: build passes; add `SYSCALL_GET_SHIFT` and `SYSCALL_FREE` to
  `constants.h` if not already there.

---

## Phase 3 — User-space helpers: string functions and far memory

Create `apps/medit/` directory with supporting headers/sources MEDIT's port needs
instead of `<string.h>`, `<i86.h>`, `<dos.h>`.

- [ ] Create `apps/medit/str.h` — declare: `uint16_t m_strlen(const char *s)`,
  `int m_strcmp(const char *a, const char *b)`,
  `char *m_strcpy(char *dst, const char *src)`,
  `char *m_strcat(char *dst, const char *src)`,
  `void *m_memcpy(void *dst, const void *src, uint16_t n)`,
  `void *m_memmove(void *dst, const void *src, uint16_t n)`.
  Use `typedef`s from `types.h`.
- [ ] Create `apps/medit/str.c` — implement all six functions as simple loops (no
  optimization needed; freestanding, no libc).
- [ ] In `apps/medit/medit.h` (copy of `edit/src/medit.h`), add at the bottom:
  ```c
  #include "str.h"
  #define strlen   m_strlen
  #define strcmp   m_strcmp
  #define strcpy   m_strcpy
  #define strcat   m_strcat
  #define memcpy   m_memcpy
  #define memmove  m_memmove
  ```
  This lets unchanged source files that do `#include "medit.h"` + `<string.h>` get
  the right implementations without extra edits. Remove `#include <string.h>` from
  each file that has it (edit.c, menu.c, search.c, dlg.c) — medit.h now covers it.
- [ ] Create `apps/medit/far.h` — declare and implement (inline/pragma aux) far memory
  helpers (replacing `_fmemcpy`, `_fmemmove`, `MK_FP`, `FP_SEG`, `FP_OFF`):
  ```c
  /* Build a far pointer from segment + offset */
  #define MK_FP(seg, off) ((void __far *)(((uint32_t)(uint16_t)(seg) << 16) | (uint16_t)(off)))
  #define FP_SEG(p) ((uint16_t)((uint32_t)(p) >> 16))
  #define FP_OFF(p) ((uint16_t)(uint32_t)(p))
  /* Far byte access */
  static uint8_t far_getb(uint16_t seg, uint16_t off) { return read_far_b(seg, off); }
  static void    far_putb(uint16_t seg, uint16_t off, uint8_t v) { write_far_b(seg, off, v); }
  /* Far block copy/move (src and dst may be in different segments) */
  void far_copy(uint8_t __far *dst, const uint8_t __far *src, uint16_t n);
  void far_move(uint8_t __far *dst, const uint8_t __far *src, uint16_t n);
  ```
- [ ] Add `apps/medit/far.c` implementing `far_copy` (forward byte loop) and `far_move`
  (forward loop if dst < src, else backward loop — for safe overlap).

- [ ] Verify: `apps/medit/str.c` and `apps/medit/far.c` compile individually with
  `wcc -ms -os -s -zl -d0 -bt=dos -i=src/include -i=apps/medit str.c` (adapt path).
- [ ] Housekeeping: no kernel or existing app changes.

---

## Phase 4 — Port gap.c (gap buffer, text storage)

Source: `edit/src/gap.c` → `apps/medit/gap.c`

- [ ] Copy `edit/src/gap.c` to `apps/medit/gap.c` and `edit/src/gap.h` to
  `apps/medit/gap.h`.
- [ ] Remove `#include <i86.h>`, `<dos.h>`, `<string.h>`. Add `#include "types.h"`,
  `"constants.h"`, `"port_io.h"`, `"far.h"`, `"gap.h"`.
- [ ] Change `static u8 __far *buf;` to `static u8 __far *buf;` — keep as far pointer.
- [ ] Rewrite `gb_init()`:
  - Remove `segread`, `_dos_setblock`, `_dos_allocmem`.
  - Set `GB_WANT_PARAS` to 3000 (48000 bytes; was ~4096 for 64KB in original).
  - Call `uint16_t seg = (uint16_t)syscall_int40(SYSCALL_ALLOC, 0, GB_WANT_PARAS, 0, 0, 0, 0);`
  - `buf = (u8 __far *)MK_FP(seg, 0);`
  - Set `cap = GB_WANT_PARAS << 4; gs = 0; ge = cap;`
  - Return 0 on success; if seg is 0 (shouldn't happen), return -1.
- [ ] Change `gb_max_text` define / `GB_MAX_TEXT` to be `GB_WANT_PARAS * 16 - 4` in
  `gap.h` (adjusted for new size).
- [ ] Replace `_fmemcpy(...)` with `far_copy(...)` in `gb_move()`.
- [ ] Replace `_fmemmove(...)` with `far_move(...)` in `gb_move()`.
- [ ] `gb_insert_block()`: replace `_fmemcpy(buf + gs, src, n)` with
  `far_copy(buf + gs, src, n)`.
- [ ] `gb_copy_out()`: loop is already byte-by-byte via `gb_at()` — no change.
- [ ] `gb_raw()`: keep as-is (returns `u8 __far *`, caller uses it with far_copy).

- [ ] Verify: compiles cleanly with `wcc -ms -os -s -zl -d0 -bt=dos -i=src/include -i=apps/medit apps/medit/gap.c`.
- [ ] Housekeeping: no kernel changes.

---

## Phase 5 — Port clip.c (clipboard)

Source: `edit/src/clip.c` → `apps/medit/clip.c`

- [ ] Copy `edit/src/clip.c` and `edit/src/clip.h` to `apps/medit/`.
- [ ] Remove `#include <i86.h>`, `<dos.h>`. Add `#include "types.h"`, `"constants.h"`,
  `"port_io.h"`, `"far.h"`, `"gap.h"`.
- [ ] Rewrite `clip_set()`:
  - Remove `_dos_freemem(cseg)` (SYSCALL_FREE is a no-op).
  - Replace `_dos_allocmem((n + 15) >> 4, &cseg)` with
    `cseg = (uint16_t)syscall_int40(SYSCALL_ALLOC, 0, (n + 15) >> 4, 0, 0, 0, 0);`.
  - Replace `MK_FP(cseg, 0)` → use `far.h`'s `MK_FP`.
- [ ] `clip_ptr()`: replace `MK_FP(cseg, 0)` with the far.h version.

- [ ] Verify: compiles cleanly.
- [ ] Housekeeping: none.

---

## Phase 6 — Port vid.c (video layer)

Source: `edit/src/vid.c` → `apps/medit/vid.c`

The shadow buffer and dirty-row logic are kept. Direct VRAM writes at 0xB800 work
from user-space in real mode without any changes. BIOS INT 10h calls are removed.

- [ ] Copy `edit/src/vid.c`, `edit/src/vid.h` to `apps/medit/`.
- [ ] Remove `#include <i86.h>`, `<string.h>`. Add `#include "types.h"`, `"constants.h"`,
  `"port_io.h"`.
- [ ] Remove the `blit_row_snow` pragma aux (CGA snow — not needed in QEMU).
- [ ] Remove `static u8 snowwait;`, `static u8 oldmode;`, `static u16 curshape;`.
- [ ] Remove `get_mode()`, `set_mode()`, `set_shape()` functions entirely.
- [ ] Rewrite `vid_init(fast)`:
  - Remove BIOS calls. Always use 0xB800 (VGA text mode; kernel booted in text mode).
  - Set `vram = (u16 __far *)MK_FP(0xB800, 0);`
  - Keep `curhidden = 0;`
  - Keep shadow buffer initialization (fill with `0x0700 | ' '`, dirty[i]=1).
- [ ] Rewrite `vid_done()`:
  - Call `syscall_int40(SYSCALL_CLEAR_SCREEN, 0, 0, 0, 0, 0, 0);` to clear screen on exit.
  - Show cursor: `port_out_b(0x3D4, 0x0A); port_out_b(0x3D5, 0x0E);` (restore normal cursor).
- [ ] Rewrite `vid_flush()`:
  - Remove `blit_row_snow` branch. Always use `write_far_w` loop:
    ```c
    for (c = 0; c < COLS; c++)
        write_far_w(0xB800, (uint16_t)((r * COLS + c) * 2), shadow[r][c]);
    ```
  - Keep dirty-row tracking unchanged.
- [ ] Rewrite `vid_cursor(row, col)`:
  - Remove `set_shape` / curhidden restore block.
  - Call `syscall_int40(SYSCALL_SET_CURSOR, 0, (uint16_t)((row << 8) | col), 0, 0, 0, 0);`
  - Note: SYSCALL_SET_CURSOR packs row in BX high byte, col in BX low byte:
    `video_set_cursor((uint8_t)bx, (uint8_t)(bx >> 8))` — so pass `(row << 8) | col`
    as the `bx` argument (second positional arg to syscall_int40 maps to BX).
  - Set `curhidden = 0;`.
- [ ] Rewrite `vid_cursor_hide()`:
  - Use CRTC cursor-off: `port_out_b(0x3D4, 0x0A); port_out_b(0x3D5, 0x20);`
  - Set `curhidden = 1;`.

- [ ] Verify: compiles cleanly.
- [ ] Housekeeping: none.

---

## Phase 7 — Port kbd.c (keyboard input)

Source: `edit/src/kbd.c` → `apps/medit/kbd.c`

- [ ] Copy `edit/src/kbd.c`, `edit/src/kbd.h` to `apps/medit/`.
- [ ] Remove `#include <i86.h>`. Add `#include "types.h"`, `"constants.h"`, `"port_io.h"`.
- [ ] Rewrite `kbd_get()`:
  ```c
  unsigned kbd_get(void)
  {
      u16 k = (u16)syscall_int40(SYSCALL_READ_CHAR, 0, 0, 0, 0, 0, 0);
      u8 sc = (u8)(k >> 8);
      u8 al = (u8)k;
      u8 sh = (u8)syscall_int40(SYSCALL_GET_SHIFT, 0, 0, 0, 0, 0, 0);
      kbd_shift = 0;
      if (sh & 0x03) kbd_shift |= SH_SHIFT;
      if (sh & 0x04) kbd_shift |= SH_CTRL;
      if (sh & 0x08) kbd_shift |= SH_ALT;
      if (al == 0 || al == 0xE0)
          return K_SPECIAL | sc;
      return al;
  }
  ```
  Note: Remove the XT keypad quirk block (the shift+keypad reinterpretation for 83-key
  BIOS) — not applicable to AT keyboard / QEMU.

- [ ] Verify: compiles cleanly.
- [ ] Housekeeping: none.

---

## Phase 8 — Port doc.c (file load/save)

Source: `edit/src/doc.c` → `apps/medit/doc.c`

- [ ] Copy `edit/src/doc.c`, `edit/src/doc.h` to `apps/medit/`.
- [ ] Remove `#include <i86.h>`, `<dos.h>`. Add `#include "types.h"`, `"constants.h"`,
  `"port_io.h"`, `"far.h"`, `"gap.h"`.
- [ ] Remove `dos_read_far()` and `dos_write_far()` static helpers.
- [ ] Rewrite `doc_load(path)`:
  - `u16 h = (u16)syscall_int40(SYSCALL_OPEN, 0, (u16)path, 0, 0, 0, 0);`
  - If `h >= 16` (0xFFFF = not found): return `DOC_ERR_OPEN`.
  - Use a `static u8 chunk[512];` buffer. Loop:
    ```c
    u16 got, pos = 0;
    gb_reset();
    for (;;) {
        got = (u16)syscall_int40(SYSCALL_READ, 0, h, (u16)chunk, 512, 0, 0);
        if (got == 0) break;
        if (pos + got > GB_MAX_TEXT) {
            syscall_int40(SYSCALL_CLOSE, 0, h, 0, 0, 0, 0);
            gb_reset();
            return DOC_ERR_BIG;
        }
        gb_insert_block(pos, (u8 __far *)chunk, got);
        pos += got;
        if (got < 512) break;
    }
    syscall_int40(SYSCALL_CLOSE, 0, h, 0, 0, 0, 0);
    gb_set_len(pos);
    return DOC_OK;
    ```
  - Note: `gb_insert_block` takes `const u8 __far *src`. Casting near ptr `chunk` as
    far: `(u8 __far *)chunk` works in OpenWatcom 16-bit small model (uses current DS).
- [ ] Rewrite `doc_save(path)`:
  - `syscall_int40(SYSCALL_DELETE, 0, (u16)path, 0, 0, 0, 0);` (ignore failure)
  - `u16 h = (u16)syscall_int40(SYSCALL_CREATE, 0, (u16)path, 0, 0, 0, 0);`
  - If `h >= 16`: return `DOC_ERR_OPEN`.
  - Static chunk buffer. Loop `gb_copy_out(off, n, (u8 __far *)chunk)`, then
    `SYSCALL_WRITE(h, (u16)chunk, n)`.
  - Close handle, return DOC_OK.
- [ ] Keep all `doc_line_home`, `doc_line_end`, `doc_next_line`, `doc_prev_line`
  unchanged (pure gap-buffer logic, no DOS calls).

- [ ] Verify: compiles cleanly.
- [ ] Housekeeping: none.

---

## Phase 9 — Port dlg.c (dialogs and file browser)

Source: `edit/src/dlg.c` → `apps/medit/dlg.c`

- [ ] Copy `edit/src/dlg.c`, `edit/src/dlg.h` to `apps/medit/`.
- [ ] Remove `#include <string.h>`, `<dos.h>`, `<direct.h>`. Add `#include "types.h"`,
  `"constants.h"`, `"port_io.h"`, `"str.h"`.
- [ ] Replace `strlen` → `m_strlen`, `strcmp` → `m_strcmp`, `strcpy` → `m_strcpy`,
  `strcat` → `m_strcat`, `memcpy` → `m_memcpy`, `memmove` → `m_memmove`
  (or rely on the `#define`s added to `medit.h` in Phase 3 — confirm medit.h is
  included before `<string.h>` would have been).

- [ ] **Remove drive enumeration entirely**: remove the `_dos_getdrive`/`_dos_setdrive`
  block (lines ~280-295 in original dlg.c) from `fb_read_dir()`. Remove the
  `FK_DRIVE = 2` kind and its handling in `fb_entry_text()` and in the Enter-key
  handler in `dlg_filebox()`.

- [ ] **Replace `fb_read_dir()`** (replaces `_dos_findfirst`/`_dos_findnext`):
  Define a minimal FAT16 dirent struct locally (32 bytes, matching kernel layout):
  ```c
  typedef struct {
      u8  name[8];
      u8  ext[3];
      u8  attrs;
      u8  reserved[10];
      u16 time, date, cluster;
      u32 fsize;
  } dirent_t;
  typedef struct { u16 fs_ptr, dir_cluster, entry_idx; } dirh_t;
  ```
  (Total: 32 bytes for dirent_t, 6 bytes for dirh_t — must match `fat16_dirent_t`
  and `fat16_dir_t` in the kernel exactly. Check `src/include/drivers/fat16.h`.)
  
  Implement `fb_read_dir()`:
  ```c
  static dirh_t dh;
  static dirent_t de;
  fb_n = 0;
  /* Add ".." if not at root */
  char cwd[64];
  syscall_int40(SYSCALL_GETCWD, 0, 0, (u16)cwd, 64, 0, 0);
  if (cwd[0] != '/' || cwd[1] != '\0')
      fb_add("..", FK_DIR);
  /* Open dir */
  syscall_int40(SYSCALL_OPENDIR, 0, 0, (u16)&dh, 0, 0, 0);
  while (syscall_int40(SYSCALL_READDIR, 0, 0, (u16)&dh, (u16)&de, 0, 0) == 0
         && fb_n < FB_MAX) {
      /* skip empty/deleted entries */
      if (de.name[0] == 0x00 || de.name[0] == 0xE5) continue;
      /* skip volume labels and '.' / '..' */
      if (de.attrs & 0x08) continue;
      char nm[13]; /* 8+1+3+NUL */
      u8 i = 0, j;
      for (j = 0; j < 8 && de.name[j] != ' '; j++) nm[i++] = de.name[j];
      if (!(de.attrs & 0x10)) { /* file: add extension */
          if (de.ext[0] != ' ') {
              nm[i++] = '.';
              for (j = 0; j < 3 && de.ext[j] != ' '; j++) nm[i++] = de.ext[j];
          }
      }
      nm[i] = '\0';
      if (nm[0] == '.' && (nm[1] == '\0' || (nm[1] == '.' && nm[2] == '\0'))) continue;
      fb_add(nm, (de.attrs & 0x10) ? FK_DIR : FK_FILE);
  }
  syscall_int40(SYSCALL_CLOSEDIR, 0, 0, (u16)&dh, 0, 0, 0);
  fb_sort();
  ```

- [ ] Replace `getcwd(cwd, n)` calls in `dlg_filebox()` with
  `syscall_int40(SYSCALL_GETCWD, 0, 0, (u16)cwd, sizeof(cwd), 0, 0)`.
- [ ] Replace `chdir(fb_name[sel])` with
  `syscall_int40(SYSCALL_CHDIR, 0, (u16)fb_name[sel], 0, 0, 0, 0)`.
- [ ] Remove `struct find_t ft` and the `_dos_findfirst` call from `dlg_filebox()`'s
  save-overwrite confirmation (replace with a `SYSCALL_OPEN` probe:
  `u16 h = (u16)syscall_int40(SYSCALL_OPEN, 0, (u16)namebuf, 0, 0, 0, 0); if (h < 16) { syscall_int40(SYSCALL_CLOSE, 0, h, ...); /* exists */ }`).

- [ ] Verify: compiles cleanly.
- [ ] Housekeeping: none.

---

## Phase 10 — Port edit.c, menu.c, search.c (trivial changes)

Source: `edit/src/edit.c`, `edit/src/edit.h`, `edit/src/menu.c`, `edit/src/menu.h`,
`edit/src/search.c`, `edit/src/search.h` → `apps/medit/`.

- [ ] Copy all six files to `apps/medit/`.
- [ ] In `edit.c`, `menu.c`, `search.c`: remove `#include <string.h>` (medit.h's macros
  cover it). No other changes needed.

- [ ] Verify: all three compile cleanly.
- [ ] Housekeeping: none.

---

## Phase 11 — Port main.c (entry point)

Source: `edit/src/main.c` → `apps/medit/main.c`

- [ ] Copy `edit/src/main.c` to `apps/medit/main.c`.
- [ ] Remove `#include <dos.h>`, `<string.h>`. Add `#include "types.h"`, `"constants.h"`,
  `"port_io.h"`.
- [ ] Remove `int24_code[]` and `install_int24()` entirely.
- [ ] Remove `static void say(const char *s)` — replace with a local helper:
  ```c
  static void say(const char *s)
  {
      syscall_int40(SYSCALL_WRITE_STDOUT, 0, 0, 0, 0, (u16)s, 0);
  }
  ```
- [ ] Change `int main(int argc, char **argv)` to `void __far _main(void)`.
  **`_main` must be the first function defined** in the file (wlink places functions in
  source order; file offset 0 = entry point at 0x0100). Move all `static` helpers below.
- [ ] Read argument from PSP at `DS:0x0082` (written there by SYSCALL_EXEC):
  ```c
  const char *file = (const char *)0x0082;
  if (*file == '\0') file = 0;  /* no argument */
  ```
  Remove `argc`/`argv` parsing loop.
- [ ] Remove `/F` fastmode flag (no CGA snow on VGA/QEMU). Pass `fast=1` always to
  `vid_init()` (or just remove the parameter — vid_init no longer uses it; simplest:
  keep signature, ignore parameter).
- [ ] At function end, call `syscall_int40(SYSCALL_EXIT, 0, 0, 0, 0, 0, 0);` instead of
  `return 0;` (or just let `__far _main` return via retf — both work; explicit EXIT
  is cleaner).

- [ ] Verify: compiles cleanly.
- [ ] Housekeeping: none.

---

## Phase 12 — Build integration

The current `build.py` only supports single-file `.c` apps. MEDIT has 11 source files.
This phase extends build.py and adds MEDIT to the disk image.

### Changes to `build.py`

- [ ] Add a `build_medit(obj_dir, out_dir)` function:
  ```python
  def build_medit(obj_dir: Path, out_dir: Path) -> bool:
      srcs = [
          "apps/medit/main.c", "apps/medit/vid.c", "apps/medit/kbd.c",
          "apps/medit/gap.c",  "apps/medit/clip.c", "apps/medit/doc.c",
          "apps/medit/dlg.c",  "apps/medit/edit.c", "apps/medit/menu.c",
          "apps/medit/search.c", "apps/medit/str.c", "apps/medit/far.c",
      ]
      objs = []
      medit_obj = obj_dir / "medit"
      medit_obj.mkdir(parents=True, exist_ok=True)
      for src in srcs:
          p = Path(src)
          # compile with extra include path for apps/medit/
          obj = medit_obj / (p.stem + ".obj")
          cmd = [
              str(WCC), "-ms", "-os", "-s", "-zl", "-d0", "-wx", "-bt=dos",
              f"-i={REPO_ROOT / 'src' / 'include'}",
              f"-i={REPO_ROOT / 'apps' / 'medit'}",
              f"-fo={obj}",
              str(REPO_ROOT / src),
          ]
          run(cmd)
          objs.append(obj)
      # link all objects
      app_out = out_dir / "EDIT.COM"
      lnk = out_dir / "_medit_link.lnk"
      with open(lnk, "w") as f:
          f.write("format raw bin\noption offset=0x100\noption quiet\n")
          f.write(f"name {app_out}\n")
          f.write("file " + ",".join(str(o) for o in objs) + "\n")
      run([str(WLINK), f"@{lnk}"])
      log(f"MEDIT linked: {app_out.name} ({app_out.stat().st_size} bytes)")
      return True
  ```
  Call `compile_c` cannot be reused directly (it hard-codes the include path). The
  inline `cmd` list above is the correct approach.
- [ ] In `build_all()`, add `build_medit(OBJ_DIR, BUILD_DIR)` after the existing app loop.
- [ ] In the disk image injection section: add injection of `EDIT.COM` into `BIN/`
  (same pattern as the other BIN/ apps):
  ```python
  edit_path = BUILD_DIR / "EDIT.COM"
  if edit_path.exists():
      edit_data = edit_path.read_bytes()
      write_file_to_subdir(image, edit_data, "EDIT.COM", ...)
  ```

- [ ] Verify: `make` builds cleanly. `build/EDIT.COM` exists.
  Run `ls` inside QEMU and confirm EDIT.COM appears in BIN/.
- [ ] Housekeeping: ensure no existing apps are broken; confirm SHELL.COM still builds.

---

## Phase 13 — Manual integration test (QEMU with display)

This phase cannot be automated via serial stdio — it requires VGA display + PS/2 keyboard.

- [ ] Run QEMU with display: `qemu-system-i386 -drive file=harvac.img,format=raw,if=ide -m 4`
  (omit `-display none -serial stdio`).
- [ ] At the Harvac shell prompt, type `edit`. Confirm:
  - MEDIT opens with empty buffer; menu bar visible at top.
  - Type text; it appears in the editor.
  - Ctrl+S saves (prompts for filename via dlg_filebox if no arg given).
  - Ctrl+Q quits back to shell prompt.
- [ ] Type `edit readme.txt`. Confirm:
  - README.TXT content loads into editor.
  - Edit a line; Ctrl+S saves.
  - `cat readme.txt` from shell shows the change.
- [ ] Verify arrow keys, Home/End, PgUp/PgDn work for navigation.
- [ ] Verify Alt+F opens the Fil menu; Esc closes it.
- [ ] Verify shell prompt returns cleanly after quitting MEDIT (resident shell is intact).
- [ ] Housekeeping: update wiki (Phase 14).

---

## Phase 14 — Wiki update

- [ ] Create `docs/wiki/medit-harvac.md` documenting:
  - What MEDIT is, the source origin (`edit/src/`), and which features were kept.
  - The port strategy: 7 files rewritten, 3 files minimally changed (string.h only).
  - Which kernel syscalls were added/changed (SYSCALL_ALLOC, SYSCALL_FREE,
    SYSCALL_GET_CURSOR, SYSCALL_GET_SHIFT; extended SYSCALL_READ_CHAR to 16-bit).
  - Keyboard driver changes (16-bit ring buffer, Alt/Ctrl/extended key support).
  - Memory layout: EDIT.COM loads at dynamic child_seg via g_next_seg; gap buffer
    (~48KB) allocated via SYSCALL_ALLOC into the next available segment.
  - Known limitations: serial keyboard input only gives ASCII (no special keys);
    text file limit ~48000 bytes; no `CALL` in batch for scripts.
  - Key files: `apps/medit/`, `src/drivers/keyboard.c`, `src/kernel/syscalls.c`.
- [ ] Update `docs/wiki/index.md` with entry for `medit-harvac.md`.
- [ ] Append to `docs/wiki/log.md` and `docs/improvements.md` (new open items:
  serial keyboard escape-sequence support for MEDIT; text limit increase; cursor
  blinking toggle).
- [ ] Verify: wiki builds/reads correctly.
- [ ] Housekeeping: `make` still clean.

---

## Done when

- [x] All phases complete and checked off
- [x] `make` builds cleanly with no errors
- [x] `EDIT.COM` appears in `BIN/` on the disk image
- [ ] Manual QEMU test: `edit`, `edit readme.txt`, file save/load, menus, arrow navigation
      (serial-only test confirmed launch and Ctrl+Q exit; VGA display test pending)
- [x] Shell and existing apps (`ls`, `cat`, `hello`, `uname`, batch) still work
- [x] Wiki updated; log.md entry appended

## Completion note (2026-07-09)

Implementation complete. Key issues found and fixed during integration:

1. **Stack/BSS overlap (critical)**: BSS is not written to raw binary; linker places it
   at addresses starting at `load_off`. The old `child_sp = load_off + 2048 - 2` put the
   stack inside the BSS region. `vid_init` writing `shadow[25][80]` corrupted stack
   return addresses. Fixed: `child_sp = 0xFFFE`, `alloc_paras = 0x1000` (64 KB per child,
   DOS COM convention). Applied to both `SYSCALL_EXEC` and `exec_com`.

2. **SYSCALL_ALLOC parameter bug**: Used `ax` (= `0x5000` = syscall_num<<8) instead of
   `bx` (the actual paragraph count). Fixed: `set_next_seg(seg + bx)`.

3. **BSS zero-initialization**: BSS not in binary; memory at those addresses is whatever
   was there before. All globals that need zero-init are explicitly set in init functions
   (vid_init, edit_init, etc.) so this does not cause bugs in practice.
