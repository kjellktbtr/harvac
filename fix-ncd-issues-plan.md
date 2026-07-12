# Plan: Fix NCD Issues

## Context

NCD (Norton Commander Clone) is a dual-pane file manager for Harvac OS. It has
9 user-reported issues that need fixing, spanning the app layer
(`apps/ncd/`), the kernel keyboard driver (`src/drivers/keyboard.c`), and
the kernel syscall layer (`src/kernel/syscalls.c`).

## Goal / Success Criteria

1. Files render immediately after launch and directory changes (no need to
   press arrow keys first).
2. Tab switches between left and right panes.
3. F3 viewer supports PageUp/PageDown/arrow keys for scrolling.
4. File sizes display correctly in the SIZE column.
5. Date/time shows actual timestamps from FAT16, not 0-0-0 0:0.
6. No garbled characters (smileys, card suits) in the DATE/TIME column.
7. F3 on a directory enters it (instead of being a no-op).
8. Enter on .BAT and .COM files launches them; command-line execution works
   (typing characters opens shell prompt, Enter executes).
9. `make` builds cleanly; `make run` launches QEMU with NCD functional.

## Assumptions & Open Questions

- The FAT16 image built by `build.py` may write zero timestamps — this needs
  to be checked and possibly fixed.
- The kernel `ncd_opendir`/`ncd_readdir` syscalls read directory entries
  from the kernel-side FAT16 driver and copy them back to user space (the
  caller's DS segment). This path must work because LS.COM and MEDIT's
  file-open dialog already use OPENDIR/READDIR.
- [ ] Confirm that OPENDIR/READDIR work correctly from NCD's user segment
  (0x3000). Test by adding debug output to panel_refresh.
- [ ] Confirm whether the kernel SYSCALL_EXEC path supports launching .BAT
  files via SHELL.COM (shell already handles .BAT in its REPL by passing
  them to SHELL.COM with the .BAT name as argument).

## Phase 1 — Fix Tab key (kernel keyboard driver)

Tab scancode (0x0F) is mapped to 0 (no character) in the keyboard driver's
scancode-to-ASCII tables. It gets returned as `K_SPECIAL | 0x0F`, which
doesn't match NCD's `K_TAB` (0x09).

- [x] Edit `src/drivers/keyboard.c`:
  - Change `scancode_ascii[0x0F]` from `0` to `'\t'` (ASCII 9).
  - Change `scancode_ascii_shift[0x0F]` from `0` to `'\t'` (Tab shouldn't
    be affected by Shift; could also add Shift+Tab = back-tab later).
- [x] Verify: `make && make run` — press Tab in NCD; active pane switches.
- [x] Housekeeping: build, test; update `docs/wiki/log.md`,
  `docs/wiki/ncd.md`.

## Phase 2 — Fix files not rendering until cursor moved

Files don't appear in pane until cursor moves. Root cause: `panel_refresh()`
clamp logic underflows when `p->count < vis` (e.g. 10 < 16 → `10 - 16 = 65530`
as u16), setting scroll to a huge value that renders zero entries. Moving
cursor calls `panel_clamp_scroll()` which correctly handles this case.

- [x] Replace the inline clamp in `panel_refresh()` with a call to
  `panel_clamp_scroll()` (already handles `p->count <= vis` → scroll=0).
- [x] Add forward declaration of `panel_clamp_scroll()` before `panel_refresh`.
- [x] Remove unused `vis` variable from `panel_refresh()`.
- [x] Verify: `make` builds cleanly; `make run` — files appear immediately.
- [x] Housekeeping: build, test; update docs.

## Phase 3 — Fix garbled characters in DATE/TIME column

`m_itoa()` in `apps/ncd/str.c` writes digits into a fixed-size buffer
backwards starting at offset 6, leaving leading bytes uninitialized. The
shift-left loop then reads these uninitialized bytes (stack garbage,
manifesting as smileys/card suits in cp437) before finding the actual
digits.

- [x] Rewrite `m_itoa()` in `apps/ncd/str.c`:
  - Use a simpler right-to-left fill approach that doesn't leave
    uninitialized bytes.
- [x] Do the same for `m_u32toa()` (same bug pattern).
- [x] Verify: build and check that date/time columns show clean numeric
  output with no garbage characters.
- [x] Housekeeping: build, test; update docs.

## Phase 4 — Fix date/time showing 0-0-0 0:0

The FAT16 image builder (`build.py`) writes zero timestamps (line 671:
"# Time: use zero"). Both root directory and subdirectory entry writers
need to set the date/time fields.

- [x] Add `_fat16_datetime()` helper to convert host mtime to FAT16 format.
- [x] Fix `write_file_to_fat16()` to set entry[22:26] (time, date) from
  mtime or current time as fallback.
- [x] Fix `write_file_to_subdir()` to set entry[22:26] to current time.
- [x] Add guard in `ncd_format_datetime()` to show blank when date=0, time=0.
- [x] Verify: `make && make run` — DATE/TIME column shows actual timestamps.
- [x] Housekeeping: build, test; update docs.

## Phase 5 — Fix file size not showing

Size column was showing 0 or garbage because `m_itoa`/`m_u32toa` had
uninitialized bytes. Fixed by the Phase 3 rewrite of both functions.

- [x] Already handled by Phase 3 string fix.
- [x] Verify: `make run` — files show sizes in bytes.
- [x] Housekeeping: build, test; update docs.

## Phase 6 — Add scroll support to F3 viewer

The viewer in `apps/ncd/viewer.c` only handled F3/Esc to exit. Added
page up/down and arrow key support.

- [x] Add `viewer_scroll` variable tracking current scroll offset.
- [x] Add `count_lines()` helper to count total lines in far buffer.
- [x] Rewrite `viewer_render()` to render only visible lines from scroll.
- [x] Handle `K_UP`/`K_DOWN`/`K_PGUP`/`K_PGDN`/`K_HOME`/`K_END` in input
  loop, calling `viewer_render()` after each scroll.
- [x] Show scroll position in status line ("Ln 45/200").
- [x] Verify: `make run` — open a long file with F3, scroll with arrows.
- [x] Housekeeping: build, test; update docs.

## Phase 7 — F3 on folders enters them

- [x] Modify `handle_f3()` in `apps/ncd/main.c:233-245`: if `e.is_dir`,
  call `panel_enter_dir(p)` instead of skipping.
- [x] Verify: `make run` — F3 on a folder enters it.
- [x] Housekeeping: build, test; update docs.

## Phase 8 — Enter on .BAT/.COM launches them; shell prompt on typing

- [x] Add `m_ends_with()` helper to `apps/ncd/str.h`/`str.c`.
- [x] In the `K_ENTER` handler: if file is `.BAT`, launch via SHELL.COM;
  if `.COM`, launch directly. Refresh panels after execution.
- [x] In the `default:` case: printable characters call `shell_prompt()`
  with the character pre-filled.
- [x] Fix `shell_prompt()` signature to accept `const char *prefill`.
- [x] Remove the redundant `cmd_len = 0` reset after initial render (it
  was clearing the pre-filled command).
- [x] Verify: `make run` — Enter on HELLO.COM launches it; typing a
  character opens the shell prompt with it pre-filled.
- [x] Housekeeping: build, test; update docs.

## Phase 9 — Final integration, build, and test

- [x] Full `make clean && make` build.
- [x] Verify NCD.COM under 64 KB (7438 bytes ✓).
- [x] Check binary size: `NCD.COM` must stay under 64 KB.
- [x] Update `docs/wiki/ncd.md` with fix descriptions.
- [x] Update `docs/wiki/log.md` with dated entries.
- [x] Update `docs/wiki/index.md` if needed.
- [x] Update `docs/improvements.md` — remove/resolve NCD-related items.
- [x] Update `ncd-plan.md` if needed (add checkboxes or notes about
  completed fixes).

## Done when

- [x] All 9 phases complete and checked off
- [x] `make clean && make` builds without errors
- [x] `make run` shows NCD with all fixes working
- [x] NCD.COM < 64 KB
- [x] Docs/wiki updated
