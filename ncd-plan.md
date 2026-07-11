# Plan: NCD — Norton Commander Clone for Harvac

## Context

Harvac OS has a shell, a text editor (EDIT.COM), and basic file utilities (LS, CAT), but no
full-screen file manager. A Norton Commander / Midnight Commander style dual-pane file manager
would provide a much more usable interactive experience: browse directories side by side,
preview files, copy/move/rename with dialogs, and execute shell commands from a prompt line —
all in a single fullscreen TUI.

The app will be built as `NCD.COM` (a multi-file .COM app in `apps/ncd/`), following the same
pattern as MEDIT (`apps/medit/`): OpenWatcom `-ms` small model, direct VGA buffer access via
shadow buffer, keyboard via `SYSCALL_READ_CHAR`, filesystem via `INT 0x40` syscalls.

## Goal / Success Criteria

- `NCD.COM` builds as a flat .COM binary under 64 KB via `make` / `build.py`.
- Dual-pane file browser renders on screen with directory paths, 3-column file listings
  (Name, Size/DIR, Modify time), and a function-key menu bar.
- Arrow keys navigate within a pane; Enter opens directories; Tab switches active pane;
  Insert toggles file selection markers.
- F3 opens a built-in fullscreen file viewer (press F3 again to return).
- F4 launches `EDIT.COM` on the selected file via `SYSCALL_EXEC`.
- F5 copies selected file(s) or directory(ies) recursively from active pane to the other
  pane's directory.
- F6 shows a rename/move dialog; renames in-place or moves to other pane's directory.
- F7 shows a mkdir dialog to create a subdirectory in the active pane's directory.
- F8 deletes selected file(s) with a confirmation dialog.
- F10 quits NCD and returns to the shell.
- Second-to-last line is an interactive shell prompt: typing a command and pressing Enter
  executes it via `SYSCALL_EXEC`, then refreshes both panes.
- Dialog boxes use the same box-drawing characters and inverse attributes as MEDIT dialogs.
- Status line shows active pane indicator, selection count, and context-sensitive help.

## Assumptions & Open Questions

- NCD uses the same VGA text mode (80x25) and cp437 box-drawing chars as MEDIT.
- NCD directly writes to VGA VRAM via `write_far_w` (same as MEDIT's shadow buffer).
- Recursive copy/move is implemented in user space by walking directory entries via
  OPENDIR/READDIR/CLOSEDIR syscalls — no new kernel syscalls needed.
- SYSCALL_STAT returns file size and date/time for the 3-column display.
- SYSCALL_RENAME can move files across directories (same as rename on POSIX).
- The RTC driver is not yet available; date/time display will show FAT16 raw date/time
  words or a formatted version if SYSCALL_GET_DATE works.
- [x] Confirm SYSCALL_STAT returns date/time fields — yes, returns full `fat16_dirent_t`
  with `time`, `date`, `file_size`, `attrs` fields.
- [x] Confirm SYSCALL_RENAME works across directories — no, it's in-place 8.3 rename only.
  Cross-directory moves must use copy+delete in user space.
- [x] Note: SYSCALL_STAT and SYSCALL_RENAME are root-directory only. Subdirectory operations
  must use OPENDIR/READDIR to find entries, and CHDIR + root-relative operations.

## Phase 1 — Core UI: Dual Panes, Navigation, Rendering

Build the skeleton: menu bar, two file panes with 3-column listings, directory path headers,
keyboard navigation, and pane switching.

- [ ] Create `apps/ncd/ncd.h` — common types (`u8`, `u16`, `u32`), screen layout constants
  (ROWS=25, COLS=80, pane row/col boundaries, menu/status rows), key codes (reuse from
  medit.h: K_UP, K_DOWN, K_LEFT, K_RIGHT, K_HOME, K_END, K_PGUP, K_PGDN, K_INS, K_DEL,
  K_TAB, K_ESC, K_ENTER, K_F3..K_F10), and string function aliases (`m_strlen`, etc.).
- [ ] Create `apps/ncd/vid.h` and `apps/ncd/vid.c` — shadow buffer video layer copied from
  MEDIT pattern: `vid_init()`, `vid_done()`, `vid_putat()`, `vid_puts()`, `vid_fill()`,
  `vid_flush()`, `vid_cursor()`, `vid_cursor_hide()`. Uses `write_far_w` to VGA segment
  0xB800.
- [ ] Create `apps/ncd/kbd.h` and `apps/ncd/kbd.c` — keyboard input via `SYSCALL_READ_CHAR`
  and `SYSCALL_GET_SHIFT`, same as MEDIT's kbd.c. Returns normalized key codes.
- [ ] Create `apps/ncd/str.h` and `apps/ncd/str.c` — string/memory helpers (`m_strlen`,
  `m_strcmp`, `m_strcpy`, `m_strcat`, `m_memcpy`, `m_memmove`, `m_memset`). Same as MEDIT.
- [ ] Create `apps/ncd/fs.h` and `apps/ncd/fs.c` — filesystem syscall wrappers:
  `ncd_opendir()`, `ncd_readdir()` (returns name, attrs, size, date, time), `ncd_closedir()`,
  `ncd_chdir()`, `ncd_getcwd()`, `ncd_open()`, `ncd_close()`, `ncd_read()`, `ncd_delete()`,
  `ncd_mkdir()`, `ncd_rmdir()`, `ncd_rename()`, `ncd_stat()`. All wrap `syscall_int40()`.
- [ ] Create `apps/ncd/panel.h` and `apps/ncd/panel.c` — file pane data model and rendering:
  - `ncd_panel_t` struct: cwd buffer, file list (name, attrs, size, date, time, selected flag),
    entry count, scroll offset, selected index.
  - `panel_refresh()` — read directory via OPENDIR/READDIR, populate file list, sort
    directories first then by name.
  - `panel_render()` — draw pane border (box chars), directory path header, column headers
    ("NAME", "SIZE", "DATE/TIME"), and file entries with 3 columns. Directories show
    "DIR" in size column. Highlight selected entry with inverse attribute. Show selection
    marker (`*` or `>`) for Insert-toggled files.
  - `panel_up()`, `panel_down()`, `panel_page_up()`, `panel_page_down()`, `panel_home()`,
    `panel_end()` — navigation with scroll clamping.
  - `panel_toggle_select()` — toggle selection marker on current entry.
  - `panel_enter_dir()` — if current entry is a directory, chdir into it and refresh.
  - `panel_parent_dir()` — chdir to ".." and refresh.
- [ ] Create `apps/ncd/main.c` — entry point `_main()`:
  - Initialize video, clear screen.
  - Initialize two panels (left/right), both start at root directory.
  - Main render loop: draw menu bar (row 0 with F-key labels), render both panes,
    render status line (row 24 with active pane indicator, selection count, help text).
  - Main input loop: handle arrow keys (navigate active pane), Tab (switch active pane),
    Insert (toggle selection), Enter (open directory), Backspace (parent directory),
    PageUp/PageDown (scroll), Home/End (scroll to top/bottom).
  - F10 calls `ncd_quit()` — cleanup video, call `SYSCALL_EXIT`.
- [ ] Update `build.py` — add `build_ncd()` function (like `build_medit()`):
  - Define `NCD_DIR = APPS_DIR / "ncd"` and `NCD_SOURCES` list.
  - Compile all sources with `-i={NCD_DIR}` include path.
  - Link as `NCD.COM` with `offset=0x100`.
  - Call `build_ncd()` in `build_all()`.
- [ ] Verify: `make` builds successfully, producing `build/NCD.COM`. Binary size < 64 KB.
- [ ] Verify: `make run` launches QEMU; NCD.COM shows dual panes with file listings,
  navigation works, Tab switches panes, Enter/Backspace change directories.
- [ ] Housekeeping: format, build, tests pass; update `docs/wiki/` with NCD page;
  update `docs/wiki/index.md`; append to `docs/wiki/log.md`; update `docs/improvements.md`.

## Phase 2 — Dialogs, File Viewer (F3), and Editor Launch (F4)

Add modal dialogs (reusing MEDIT box-drawing pattern), built-in file viewer, and EDIT launch.

- [ ] Create `apps/ncd/dlg.h` and `apps/ncd/dlg.c` — modal dialog system:
  - `draw_frame()` — draw rectangular dialog with box chars (same as MEDIT's dlg.c).
  - `draw_button()` — render `<label>` style buttons.
  - `dlg_msgbox()` — modal message box with OK / Yes-No-Cancel buttons.
  - `dlg_input()` — single-line text input dialog with cursor navigation.
  - Reuse box chars: `BOX_TL` (0xDA), `BOX_TR` (0xBF), `BOX_BL` (0xC0), `BOX_BR` (0xD9),
    `BOX_H` (0xC4), `BOX_V` (0xB3). Attribute 0x70 (inverse) for dialog body.
- [ ] Create `apps/ncd/viewer.h` and `apps/ncd/viewer.c` — fullscreen file viewer:
  - `viewer_open(path)` — open file, read into buffer, display fullscreen with title bar
    showing filename. Support Up/Down/PageUp/PageDown navigation, F3/Esc to close.
  - Render file content in rows 1-23, with status line at row 24 showing line offset
    and filename.
  - Handle files larger than viewport by tracking scroll offset.
- [ ] Wire F3 in `main.c` input loop — if current entry is a file, call `viewer_open()`.
  If already in viewer, F3 closes viewer and returns to file manager.
- [ ] Wire F4 in `main.c` input loop — if current entry is a `.COM` or editable file,
  construct command line for EDIT.COM (`"EDIT <filename>"`), call `SYSCALL_EXEC`.
  After EDIT returns, refresh both panels to pick up file changes.
- [ ] Verify: `make` builds; F3 opens file viewer with scrollable content; F4 launches
  EDIT.COM; both return to NCD with refreshed panels.
- [ ] Housekeeping: build, docs, log.

## Phase 3 — File Operations: Copy (F5), Rename/Move (F6), Mkdir (F7), Delete (F8)

Implement all file operations with confirmation dialogs and recursive support.

- [ ] Implement recursive copy in `fs.c`:
  - `ncd_copy_file(src_path, dst_path)` — open src, create dst, read/write loop, close both.
  - `ncd_copy_recursive(src_path, dst_path)` — if src is directory, create dst directory,
    then recurse into entries. If src is file, copy single file.
  - Use `SYSCALL_ALLOC` for read/write buffers if needed, or use a static buffer.
- [ ] Wire F5 in `main.c` — copy selected file(s)/dir(s) from active pane to other pane's
  directory. If nothing selected, copy current file. Show progress or confirmation dialog.
- [ ] Wire F6 in `main.c` — show `dlg_input()` with current filename pre-filled. On OK:
  - If active pane == other pane: rename in place via `ncd_rename()`.
  - If different panes: move to other pane's directory via `ncd_rename()` with full path.
  - For directories, rename/move recursively (rename dir, then refresh both panes).
- [ ] Wire F7 in `main.c` — show `dlg_input()` for new directory name. On OK, call
  `ncd_mkdir()` in active pane's directory. Refresh active pane on success.
- [ ] Wire F8 in `main.c` — show `dlg_msgbox()` confirmation ("Delete selected?"). On Yes:
  - Delete selected files via `ncd_delete()`. For directories, delete contents recursively
    first, then `ncd_rmdir()`. Refresh both panes.
- [ ] Verify: `make` builds; F5 copies files/dirs recursively; F6 renames/moves;
  F7 creates directories; F8 deletes with confirmation. All operations refresh panels.
- [ ] Housekeeping: build, docs, log.

## Phase 4 — Shell Prompt Line and Final Polish

Add the interactive shell prompt on second-to-last line and polish the UI.

- [ ] Add shell prompt line at row 19 (between panes ending at row 18 and status at row 21):
  - Re-layout: panes rows 3-18 (16 rows), shell prompt row 19, separator row 20,
    status row 21, help row 22.
  - Render shell prompt with CWD: `/current/dir> ` with cursor for typing.
  - Buffer keystrokes until Enter, then execute via `SYSCALL_EXEC` with the typed command.
  - After command completes, refresh both panels to show any changes.
  - Show command output briefly or show "Command executed" / error message on status line.
- [ ] Finalize screen layout constants in `ncd.h`:
  - Row 0: Menu bar `[F3 View] [F4 Edit] [F5 Copy] [F6 Move] [F7 Mkdir] [F8 Del] [F10 Quit]`
  - Row 1: Pane path headers (left: `/dir`, right: `/dir`)
  - Row 2: Column headers (`NAME` `SIZE` `DATE/TIME` per pane)
  - Rows 3-18: File entries (16 rows per pane)
  - Row 19: Shell prompt `/cwd> `
  - Row 20: Separator line (box chars across full width)
  - Row 21: Status line `[Left/Right] Selected: N | Context help`
  - Row 22: Help/shortcut hints
  - Row 23-24: Reserved or merged into status
- [ ] Add date/time formatting helper in `fs.c` or `str.c` — convert FAT16 date/time
  words to human-readable strings (`YYYY-MM-DD HH:MM`).
- [ ] Add file size formatting — show bytes, or `DIR` for directories.
- [ ] Polish pane rendering: truncate long filenames, align columns, pad size/date fields.
- [ ] Add Escape key handling in shell prompt to cancel command and return to panel focus.
- [ ] Verify: `make` builds; shell prompt accepts commands and executes them; layout is
  clean and all rows are used; date/time and size display correctly.
- [ ] Verify: Full feature test — navigate dirs, select files, copy/move/mkdir/delete,
  view/edit files, execute shell commands, quit.
- [ ] Housekeeping: format, build, tests pass; update `docs/wiki/ncd.md` with full docs;
  update `docs/wiki/index.md`; append to `docs/wiki/log.md`; update `docs/improvements.md`.
  Update `CLAUDE.md` to mention NCD.COM.

## Done when

- [ ] All phases complete and checked off
- [ ] Full build (`make`) produces `NCD.COM` under 64 KB
- [ ] `make run` shows working dual-pane file manager in QEMU
- [ ] All F-keys (F3-F10) functional as specified
- [ ] Shell prompt executes commands and refreshes panels
- [ ] Docs / wiki updated (`docs/wiki/ncd.md`, `index.md`, `log.md`)
- [ ] `docs/improvements.md` updated with any discovered improvements
- [ ] `CLAUDE.md` mentions NCD.COM in apps list
