---
title: NCD File Manager
type: code-map
sources:
  - apps/ncd/main.c
  - apps/ncd/panel.c
  - apps/ncd/panel.h
  - apps/ncd/viewer.c
  - apps/ncd/fs.c
  - apps/ncd/fs.h
  - build.py
related:
  - "[[syscall-dispatch]]"
  - "[[medit-harvac]]"
  - "[[com-executor]]"
created: 2026-07-11
updated: 2026-07-12
confidence: high
---

# NCD — Norton Commander Clone

Dual-pane file manager for Harvac OS, modeled after Norton Commander.
Runs as a .COM program from the shell.

## Architecture

### Screen Layout

80×25 VGA text mode, split into two 40-column panes with a vertical divider.
Row map (`apps/ncd/ncd.h`, reworked 2026-07-12):

| Rows | Content |
|------|---------|
| 0 | Top pane border |
| 1 | Directory path per pane |
| 2 | Column header `\|NAME\|SIZE\|DATE\|TIME\|` |
| 3–19 | File entries (17 rows) |
| 20 | Bottom pane border |
| 21 | Status line (`[Left]/[Right]`, selection count) |
| 22 | Transient messages (e.g. "2 copied") |
| 23 | Command line (`<cwd>>`) |
| 24 | F-key bar, inverted video (`render_fkey_bar()`) |

Per pane, the 37-column table is `|NAME 12|SIZE 5|DATE 10|TIME 5|`. Files show
the name as `NNNNNNNN EXT` (`ncd_format_name12`), size right-aligned with a `K`
suffix above 99999 (`m_format_size`), date `YYYY.MM.DD` (`ncd_format_date`) and
time `HH:MM` (`ncd_format_time`); zero FAT timestamps render blank. The cursor
bar (`A_SELECTED`) is drawn **only in the active pane** so focus is obvious;
`Ins`-marked entries use `A_MARKER` in both panes.

The old top F-key menu bar (row 0) was removed — it was drawn under the pane
border and never visible.

### Far Memory Layout

Each process slot is 24 KB (`PROC_PARAS` = 0x600 paragraphs, raised from
0x400 on 2026-07-12 — see [[com-executor]]). NCD's DGROUP would exceed a slot
with inline arrays, so panel entries and the file viewer buffer live in far
segments allocated via `SYSCALL_ALLOC`:

| Segment | Size | Paragraphs | Allocated by |
|---------|------|------------|-------------|
| Left pane entries | 512 × 24 B = 12,288 B | 768 | `panel_init()` |
| Right pane entries | 512 × 24 B = 12,288 B | 768 | `panel_init()` |
| Viewer buffer | 4,096 B | 256 | `viewer_open()` |

After allocations, DGROUP ≈ 8 KB (code + BSS), well within the 24 KB slot.

### Entry Accessors

`panel_entry_get(p, i, &out)` and `panel_entry_set(p, i, &in)` wrap all
access to far-segment panel entries. They use `far_copy()` from `far.h` /
`far.c` (copied from `apps/medit/`) to move `panel_entry_t` structs (24 B)
between the near stack and the far segment.

## Syscall Usage

NCD wraps `INT 0x40` syscalls in `fs.c` using `syscall_int40()` from
`port_io.h`. All filesystem operations go through the kernel VFS layer.

Directory listing uses `OPENDIR` / `READDIR` / `CLOSEDIR` (0x20–0x22).
Panel navigation calls `CHDIR` (0x25) and `GETCWD` (0x26). File operations
use `OPEN` / `READ` / `WRITE` / `CREATE` / `DELETE` / `RENAME` / `STAT`.

## Bugs Fixed

### Bug 1: SS != DS in Syscall Directory Handlers

The kernel's `INT 0x40` handler runs with `SS = caller segment, DS = kernel
segment`. Local auto variables (stack-relative) whose addresses were passed to
callees (`fat16_readdir`, `name_to_83`, `vfs_getcwd`, …) were written through
DS (kernel segment) but read via SS (caller segment). Two different physical
addresses → garbage data → infinite `ncd_readdir` loop in `panel_refresh()`.

**Fix:** All handler locals in `src/kernel/syscalls.c` are declared `static`
(already done before NCD was built). See [[syscall-dispatch]] for the full rule.

### Bug 2: DGROUP Overflow (24 KB Panels)

`panel_t.entries[512]` at 24 B each = 12 KB per panel × 2 = 24 KB, exceeding
the 16 KB process slot. The stack at 0x3FFE sat inside the left panel's array,
and the right panel and `viewer_buf[4096]` overlapped the next child slot.

**Fix:** Panel entries moved to far segments via `SYSCALL_ALLOC`. Viewer buffer
also moved to a far segment. Follows the same pattern as MEDIT's gap buffer
(`apps/medit/gap.c`).

### Bug 3: Entry Point Ordering

`_main` was not the first function in `main.c`'s TEXT segment. OpenWatcom
wlink places functions in source order; for .COM files the first function at
offset 0x0100 is the entry point. `full_render()` executed before `panel_init()`,
accessing uninitialized far segments → crash.

**Fix:** Moved `_main` to be the first function definition with forward
declarations for all static helpers.

### Bug 4: Scroll-clamp underflow made initial render blank (`panel.c`)

`panel_refresh()` clamped scroll with `p->scroll = p->count - vis` which
underflows when `p->count < vis` (e.g. 10 < 16 → 65530). Replaced with
`panel_clamp_scroll()` which handles the `count <= vis` case.

### Bug 5: Tab key didn't switch panes (`keyboard.c`)

Scancode 0x0F (Tab) had no ASCII translation in the kernel keyboard driver.
Changed both scancode tables from 0 to `'\t'` (0x09).

### Bug 6: Garbled cp437 chars in DATE/TIME column (`str.c`)

`m_itoa()` and `m_u32toa()` left uninitialized bytes in the output buffer
(stack garbage → smileys, card suits). Rewritten with clean right-to-left
fill.

### Bug 7: Date/time always 0-0-0 (`build.py`, `fs.c`)

FAT16 image writer left time/date fields zero. Added `_fat16_datetime()`
helper and writes entry[22:26] from mtime or current time. `ncd_format_datetime()`
shows blank when date=time=0.

### Bug 8: F3 viewer had no scroll support (`viewer.c`)

Added `viewer_scroll`, `count_lines()`, and Up/Down/PgUp/PgDn/Home/End
handlers. Status line shows "Ln N/M".

### Bug 9: F3 on folders was a no-op (`main.c`)

`handle_f3()` now calls `panel_enter_dir()` for directories.

### Bug 10: Enter on .BAT/.COM did nothing; couldn't type commands (`main.c`, `str.c`)

Added `m_ends_with()`. K_ENTER launches .BAT via SHELL.COM and .COM
directly. Printable characters open the shell prompt pre-filled.

## Round 2 fixes (2026-07-12)

A second test pass found the earlier "F4 in a subdirectory opens an empty
file" and "copy makes /TMP/DEM.BAT" symptoms traced mostly to the **kernel**,
not NCD. See [[vfs-layer]] and [[com-executor]] for the kernel side.

| # | Symptom | Fix |
|---|---------|-----|
| 1 | F4 in `/DOCS` opened an empty file | Panels now `panel_sync_cwd()` before every op so the kernel CWD matches the pane; kernel resolves subdir paths (see [[vfs-layer]]) |
| 2 | Copy `DEMO.BAT` → `/TMP/DEM.BAT` (corrupt) | Kernel `name_to_83`/`resolve_user_path` fix; `_in_dir` FAT16 ops write to the right directory |
| 3 | F-key bar not visible | Row map reworked; `render_fkey_bar()` on row 24 (inverted) |
| 4 | No NCD manual | `os-docs/NCD.md` → `DOCS/NCD.TXT` (see [[on-image-docs]]) |
| 5 | F3 on a folder did nothing | `handle_f3` enters the directory |
| 6 | Copy needed confirmation | `dlg_msgbox("Copy … to …?", DB_JNA)` before F5 copies |
| 7 | Second copy froze | `SYSCALL_READ/WRITE` bounce buffers 32 B → 512 B (see [[com-executor]]) |
| 8 | Cursor bar shown in both panes | `panel_render` draws `A_SELECTED` only in the active pane |
| 9 | No "new file" command | Shift-F4: `dlg_input` name → `ncd_create` → open EDIT |
| 10 | DATE/TIME overflowed the pane | New `\|NAME\|SIZE\|DATE\|TIME\|` column layout; `ncd_format_name12/date/time`, `m_format_size`, `m_itoa_pad2` |
| 11 | Typed commands did nothing | `SHELL.COM` gained one-shot mode (runs its PSP command tail then exits); NCD command line and Enter-launch clear the screen, run, and wait for a key |
| 12 | Enter on `.COM`/`.BAT` didn't run | Kernel `SYSCALL_EXEC` searches the CWD (see [[com-executor]]); `.BAT` runs via `SHELL.COM <name>` |

The most subtle fix was unrelated to NCD proper: EDIT hung on multi-line files
because the 16 KB process slot was too small for its near BSS, which then
collided with its far gap buffer — see the `PROC_PARAS` note in
[[com-executor]].

## Round 3 fixes (2026-07-12)

Interactive defects fixed after Phase 12 refactor:

| # | Symptom | Fix |
|---|---------|-----|
| 1 | Stale VGA output after running a child | `vid_dirty_all()` in `launch_child()` forces full repaint (shadow was stale after child wrote directly to VGA) |
| 2 | Ins didn't advance cursor | `panel_down()` called after `panel_toggle_select()` — standard Norton behaviour |
| 3 | Left arrow never navigated to parent | Kernel keyboard driver: gray nav keys emit E0 2A/AA "fake shift"; added guard in `process_scancode()` to discard them when `ext_prefix` is set |
| 4 | Copy showed no progress | New `dlg_progress()` in HDK; called before each `ncd_copy_recursive()` in `handle_f5()`; shows cur/total, % bar, and middle-truncated filename |
| 5 | Copied files had zeroed date/time | New `SYSCALL_UTIME` (0x1A) sets a file's FAT16 timestamp by path; `ncd_copy_file()` stats the source first and stamps the copy |

## Round 4 — POSIX API migration (2026-07-12)

All direct `syscall_int40()` calls in NCD (≈18 calls scattered across four
files) were replaced with POSIX wrappers from `lib/posix/`:

| Old | New | Header |
|-----|-----|--------|
| `ncd_chdir(path)` | `chdir(path)` | `unistd.h` |
| `ncd_getcwd(buf, n)` | `getcwd(buf, n)` | `unistd.h` |
| `ncd_opendir(&dir)` | `opendir(".")` → `DIR *` | `dirent.h` |
| `ncd_readdir(&dir, &ent)` | `readdir(dp)` → `struct dirent *` | `dirent.h` |
| `ncd_closedir(&dir)` | `closedir(dp)` | `dirent.h` |
| `ncd_open(path)` | `open(path, O_RDONLY)` | `fcntl.h` / `unistd.h` |
| `ncd_read(h, buf, n)` | `read(fd, buf, n)` | `unistd.h` |
| `ncd_close(h)` | `close(fd)` | `unistd.h` |
| `ncd_create(path)` | `open(path, O_WRONLY\|O_CREAT)` | `fcntl.h` |
| `ncd_mkdir(path)` | `mkdir(path)` | `unistd.h` |
| `ncd_rmdir(path)` | `rmdir(path)` | `unistd.h` |
| `ncd_rename(old, new)` | `rename(old, new)` | `unistd.h` |
| `ncd_delete(path)` | `unlink(path)` | `unistd.h` |
| `ncd_stat(path, &ent)` | `stat(path, &st)` | `sys/stat.h` |
| `ncd_exec(cmd, args)` | `exec(cmd, args)` | `unistd.h` |
| `ncd_exit()` | `_exit(0)` | `unistd.h` |
| `ncd_copy_recursive()` | `copy_tree()` | `fileops.h` |
| `ncd_delete_recursive()` | `remove_tree()` | `fileops.h` |
| `ncd_format_date()` | `fat_format_date()` | `harva.h` |
| `ncd_format_time()` | `fat_format_time()` | `harva.h` |

`fs.c` / `fs.h` now contain only two thin wrappers (`ncd_copy_recursive` /
`ncd_delete_recursive`) that call `copy_tree` / `remove_tree` from
`lib/posix/fileops.c`. `panel.c` uses POSIX `DIR *` / `struct dirent *`
directly; `viewer.c` uses POSIX `open` / `read` / `close`.

Two intentional raw `syscall_int40` calls remain in `main.c`:
- `SYSCALL_CLEAR_SCREEN` — no POSIX equivalent (VGA-specific).
- `SYSCALL_WRITE_VGA` — likewise VGA-only.

`panel.c` and `viewer.c` also keep `syscall_int40(SYSCALL_ALLOC, ...)` for
far-segment allocation; there is no POSIX analog for OS-specific far memory.

A `utime()` wrapper was added to `lib/posix/unistd.c` and declared in
`lib/include/unistd.h` (wraps `SYSCALL_UTIME`, 0x1A):
```c
int utime(const char *path, uint16_t date, uint16_t time);
```
`lib/posix/fileops.c` now uses it instead of the raw syscall.

## Source Files

| File | Role |
|------|------|
| `main.c` | Entry point, main loop, F-key handlers |
| `panel.c` / `panel.h` | Pane data model, far-segment entries, rendering |
| `viewer.c` / `viewer.h` | Full-screen file viewer |
| `fs.c` / `fs.h` | Thin wrappers for `copy_tree` / `remove_tree` |
| `ncd.h` | Types, layout constants |
