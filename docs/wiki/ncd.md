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
  - apps/ncd/far.c
  - apps/ncd/far.h
  - build.py
related:
  - "[[syscall-dispatch]]"
  - "[[medit-harvac]]"
  - "[[com-executor]]"
created: 2026-07-11
updated: 2026-07-11
confidence: high
---

# NCD — Norton Commander Clone

Dual-pane file manager for Harvac OS, modeled after Norton Commander.
Runs as a .COM program from the shell.

## Architecture

### Screen Layout

80×25 VGA text mode, split into two 40-column panes with a vertical divider.
Bottom area has status line (row 21) and help line (row 22). F-key menu bar
at row 0 with F3–F8, F10 Quick reference.

### Far Memory Layout

Each process slot is 16 KB (`PROC_PARAS` = 0x400 paragraphs). NCD's DGROUP
would exceed this with inline arrays, so panel entries and the file viewer
buffer live in far segments allocated via `SYSCALL_ALLOC`:

| Segment | Size | Paragraphs | Allocated by |
|---------|------|------------|-------------|
| Left pane entries | 512 × 24 B = 12,288 B | 768 | `panel_init()` |
| Right pane entries | 512 × 24 B = 12,288 B | 768 | `panel_init()` |
| Viewer buffer | 4,096 B | 256 | `viewer_open()` |

After allocations, DGROUP ≈ 8 KB (code + BSS), leaving ~3 KB stack headroom
below the 16 KB slot limit.

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

## Source Files

| File | Role |
|------|------|
| `main.c` | Entry point, main loop, F-key handlers |
| `panel.c` / `panel.h` | Pane data model, far-segment entries, rendering |
| `viewer.c` / `viewer.h` | Full-screen file viewer |
| `fs.c` / `fs.h` | Syscall wrappers, name formatting, recursive copy/delete |
| `far.c` / `far.h` | Far-segment copy/move helpers (copied from `apps/medit/`) |
| `vid.c` / `vid.h` | VGA output: fills, puts, cursor, double-buffered flush |
| `kbd.c` / `kbd.h` | Keyboard input via `INT 0x40` |
| `dlg.c` / `dlg.h` | Input dialogs and message boxes |
| `ncd.h` | Types, layout constants, key codes, string aliases |
