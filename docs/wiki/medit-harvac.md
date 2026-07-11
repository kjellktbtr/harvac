---
title: MEDIT Text Editor Port
type: code-map
sources:
  - apps/medit/
  - src/kernel/syscalls.c
  - src/drivers/keyboard.c
  - src/kernel/syscall.asm
related:
  - "[[com-executor]]"
  - "[[syscall-dispatch]]"
  - "[[user-apps]]"
  - "[[shell]]"
created: 2026-07-09
updated: 2026-07-11
confidence: high
---

# MEDIT Text Editor Port

MEDIT is a Norwegian full-screen text editor originally written for
PC DOS 3.3+ / IBM 5155 (CGA, 256 kB).  It is ported to Harvac as
`EDIT.COM`, placed in `BIN/`, and launched with `edit [filename]`
from the shell.

## Source origin

The original sources live in `edit/src/` (a sibling directory of
`harvac/`).  Copies are maintained in `apps/medit/` and modified for
Harvac; the originals are not touched.

## Files

| File | Status | Notes |
|------|--------|-------|
| `main.c` | Rewritten | Entry point `_main` (far call, must be first function) |
| `vid.c` | Rewritten | VGA 0xB800 writes via `write_far_w`; no BIOS INT 10h |
| `kbd.c` | Rewritten | SYSCALL_READ_CHAR (16-bit key) + SYSCALL_GET_SHIFT |
| `doc.c` | Rewritten | File I/O via SYSCALL_OPEN/READ/WRITE/CREATE/CLOSE |
| `dlg.c` | Rewritten | Dialogs; dir listing via SYSCALL_OPENDIR/READDIR/CLOSEDIR |
| `gap.c` | Rewritten | Gap buffer in far segment via SYSCALL_ALLOC |
| `clip.c` | Rewritten | Clipboard via SYSCALL_ALLOC |
| `far.c` | New | `far_copy` / `far_move` replacing `_fmemcpy`/`_fmemmove` |
| `str.c` | New | `m_strlen`, `m_strcmp`, `m_strcpy`, `m_strcat`, `m_memcpy`, `m_memmove`, `m_memset` |
| `edit.c` | Minimal | Removed `#include <string.h>` only |
| `menu.c` | Minimal | Removed `#include <string.h>` only |
| `search.c` | Minimal | Removed `#include <string.h>` only |

## Kernel changes for MEDIT

### `SYSCALL_READ_CHAR` (0x06) - extended to 16-bit
Previously returned 8-bit ASCII only.  Now returns a 16-bit key event:
- High byte (AH) = scan code; low byte (AL) = ASCII.
- Special keys (arrows, F-keys, Alt+key): AL = 0, AH = scan code.
See `src/drivers/keyboard.c` and `src/include/drivers/keyboard.h`.

### `SYSCALL_GET_SHIFT` (0x09) - new
Returns current keyboard modifier byte:
- Bit 0-1: shift, bit 2: ctrl, bit 3: alt.
See `src/kernel/syscalls.c:case SYSCALL_GET_SHIFT`.

### `SYSCALL_ALLOC` (0x50) / `SYSCALL_FREE` (0x51) - new
`SYSCALL_ALLOC(BX=paragraphs)` returns a base segment from the
`g_next_seg` allocator.  `SYSCALL_FREE` is a no-op; memory is
reclaimed when the child exits via `return_to_parent_` which restores
`g_next_seg` to `child_seg` from pctx.
Note: BX holds the paragraph count (3rd arg to `syscall_int40`).

### `SYSCALL_GET_CURSOR` (0x07) - new
Reads CRTC registers 0x0E/0x0F; returns `(row << 8) | col`.

### Child SP set to 0xFFFE (DOS COM convention)
BSS variables are NOT written to the raw binary by the linker, but
are assigned addresses immediately after the binary data.  The old
kernel code placed the stack at `load_off + 2048 - 2`, which overlaps
the BSS region and causes stack corruption (vid_init writing the
`shadow` array overwrites stack frames).

Fix: both `SYSCALL_EXEC` (`syscalls.c`) and `exec_com` (`exec.c`) now
set `child_sp = 0xFFFE` and allocate `alloc_paras = 0x1000` (64 KB
per child), matching the standard DOS COM memory model.

## Memory layout

```
child_seg:0x0000  PSP (256 bytes, 16 paragraphs)
child_seg:0x0100  EDIT.COM code + initialized data (~2.5 KB code, ~0.4 KB data)
child_seg:0x28C2  BSS start (shadow 4000 bytes, fb_name 1560 bytes, etc. ~6.5 KB)
child_seg:0x4228  BSS end
child_seg:0xFFFE  Initial SP (stack grows DOWN from here)
```

Next segment after child = `child_seg + 0x1000`.
Gap buffer allocated at that segment via `SYSCALL_ALLOC` (3000
paragraphs = 48 000 bytes = 48 KB).

## I/O architecture

### Video
MEDIT maintains a `static u16 shadow[25][80]` buffer.  `vid_flush()`
writes only dirty rows to VGA at 0xB800 via `write_far_w`.
`vid_cursor()` calls `SYSCALL_SET_CURSOR`; `vid_cursor_hide()` writes
CRTC register 0x0A bit 5.

### Keyboard
`kbd_get()` calls `SYSCALL_READ_CHAR` which blocks until a key event
is available; returns 16-bit key.  For serial input (testing) only
ASCII codes are available (no special keys).

Keys with no ASCII value arrive as `scancode << 8` and are normalized by
`kbd_get()` to `K_SPECIAL | scancode` (0x01nn). **Esc is one of them**: the
kernel scancode table maps scancode 0x01 to ASCII 0, so `K_ESC` must be
`K_SPECIAL|0x01` (0x0101), *not* the DOS ASCII 0x1B. The original port kept
`0x1B` from DOS, which made Esc dead in every menu/dialog; fixed 2026-07-11
in `apps/medit/medit.h`.

### File I/O (doc.c)
`SYSCALL_READ` / `SYSCALL_WRITE` transfer through `caller_ds:CX`, but
the gap buffer lives in a separate far segment.  Solution: static 256-
byte near buffer `tmp[256]` in process DS; kernel writes to it; then
`far_copy()` moves to the gap segment.

### Directory listing (dlg.c)
Uses `harvac_dir_t` (14 bytes opaque, matches kernel `fat16_dir_t`)
and `harvac_dirent_t` (32 bytes, matches `fat16_dirent_t`).  The
kernel treats user-space pointers as raw storage for these structs.
Drive enumeration is removed (Harvac has no drive letters).

## Known limitations

- Serial keyboard input gives ASCII only (no arrows, F-keys, Alt-keys);
  MEDIT is only fully usable with a real display and PS/2 keyboard.
- Text file limit: 47 000 bytes (gap buffer is 48 000 bytes, 1 000
  reserved as gap).
- `medit.h` aliases `strlen`/`memset`/etc. to the `m_` implementations
  via `#define`; this prevents inclusion of `<string.h>` from any file
  that includes `medit.h`.

## Key files

- `apps/medit/` - all MEDIT source files
- `src/kernel/syscalls.c` - SYSCALL_ALLOC, SYSCALL_FREE, SYSCALL_GET_SHIFT, SYSCALL_GET_CURSOR, extended READ_CHAR
- `src/drivers/keyboard.c` / `keyboard.h` - 16-bit key events, shift state
- `build.py` - `build_medit()` function, EDIT.COM injection
