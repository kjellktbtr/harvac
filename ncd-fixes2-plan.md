# Plan: NCD Fixes Round 2 — paths, CWD sync, layout, exec (12 issues)

## Context

NCD (Norton-Commander clone, `apps/ncd/`) mostly works, but a second test round
found 12 issues. Root-cause analysis shows most stem from the kernel:

1. `name_to_83()` (`src/kernel/syscalls.c`) treats `/` as an ordinary char, so
   `CREATE("/TMP/DEMO.BAT")` creates a root-dir file literally named
   `/TMP/DEM` + `.BAT` — the reported `DEM.BAT` / `REA.TXT` corruption.
2. `SYSCALL_CREATE/DELETE/MKDIR/RMDIR/STAT/RENAME` always operate on the root
   directory; only `OPEN`/`OPENDIR` honor the CWD (one level). `SYSCALL_EXEC`
   never searches the CWD.
3. Kernel CWD is a single global, but NCD has two panels; `panel_refresh()`
   reads the *current* CWD instead of chdir-ing to the panel's own dir first.
   Ops resolve names against the wrong directory (EDIT opens empty files).
4. SHELL.COM ignores its PSP argument — NCD's command line and .BAT launch
   do nothing.
5. The F-key bar is drawn on row 0 and immediately overwritten by the pane
   border.
6. `SYSCALL_READ/WRITE` bounce 32 bytes/chunk → sector read-modify-write per
   32 bytes → the copy "freeze".
7. Selection bar shown in both panels; date/time column overflows the pane;
   `handle_f5` "copied" message buffer bug.

## Goal / Success Criteria

- All 12 reported issues fixed (see verification list in Phase 8).
- File ops (open/create/delete/mkdir/rename/stat/exec) work with paths and
  in subdirectories, from both NCD and the shell.
- `make clean && make` and `make test` pass; NCD.COM stays well under 16 KB.
- Wiki updated per project policy.

## Assumptions & Open Questions

- New files keep zero FAT timestamps (no RTC integration) → shown blank.
- Sizes > 99999 bytes display as KB with `K` suffix (per user's example).
- F-bar on row 24, command line on row 23, status on row 22.

## Phase 1 — Kernel: path-aware file syscalls

- [x] Add `vfs_resolve_dir(const char *abs_path, uint16_t *dir_cluster_out)`
      to `src/fs/vfs.c` (+ decl in `src/include/fs/vfs.h`): walk components
      from root via `fat16_find` / `fat16_find_in_dir`; 0 = root.
- [x] Rewrite `vfs_chdir` validation to use `vfs_resolve_dir` (multi-level).
- [x] Fix `name_to_83()` in `src/kernel/syscalls.c` to start at the last `/`.
- [x] Add `resolve_user_path(caller_ds, off, *dir_cluster, name83)` helper in
      `syscalls.c`: copy user path to static kernel buffer, resolve dir part
      (absolute or CWD-relative), 8.3-pack last component.
- [x] Add `_in_dir` variants to `src/drivers/fat16.c` (+ header):
      `fat16_create_in_dir`, `fat16_delete_in_dir`, `fat16_mkdir_in_dir`
      (writes real parent cluster into `..`), `fat16_rmdir_in_dir`,
      `fat16_rename_in_dir`; dir_cluster 0 = root; existing root functions
      become wrappers.
- [x] Rewire syscall handlers OPEN, CREATE, DELETE, MKDIR, RMDIR, STAT,
      RENAME to `resolve_user_path` + `_in_dir` calls.
- [x] `SYSCALL_EXEC`: name with `/` → resolve exact path; else search CWD,
      then root, then `/BIN`, each with `.COM` auto-append.
- [x] Verify: `make check`; boot QEMU; from shell: `cd DOCS`, `cat MANUAL.TXT`,
      `echo hi > /TMP/T.TXT`, `cat /TMP/T.TXT`, `mkdir /TMP/SUB`, `cd /TMP/SUB`,
      exec a .COM from a subdir CWD.
- [x] Housekeeping: build, test; wiki notes on path resolution.

## Phase 2 — Kernel: copy performance

- [x] Bump SYSCALL_READ / SYSCALL_WRITE static bounce buffers from 32 to 512
      bytes in `src/kernel/syscalls.c`.
- [x] Verify: copy a multi-KB file in NCD/shell without long stalls.
- [x] Housekeeping: build, test.

## Phase 3 — SHELL.COM: one-shot command mode

- [x] In `apps/shell.c` `_main`: read PSP tail at `0x0082`; if non-empty,
      seed `line`, set `oneshot`, skip prompt/read, dispatch once; if a .BAT
      was started, loop until batch EOF; then exit.
- [x] Verify: from NCD/shell, `SHELL.COM` with arg `echo hello` prints and
      returns; with arg `DEMO.BAT` runs the batch and returns.
- [x] Housekeeping: build, test.

## Phase 4 — NCD: panels own their CWD

- [x] `panel_refresh(p)` (`apps/ncd/panel.c`): `ncd_chdir(p->cwd)` first when
      `p->cwd[0] != '\0'`.
- [x] Add `panel_sync_cwd(p)`; call for active panel before F3, F4, Shift-F4,
      F5, F6, F7, F8, Enter, and in `shell_prompt` (`apps/ncd/main.c`).
- [x] Verify: two panels in different dirs; F4 in /DOCS opens real content;
      F3 enters folders; ops target the right dirs.
- [x] Housekeeping: build, test.

## Phase 5 — NCD: screen layout rework

- [x] New row map in `apps/ncd/ncd.h`: files 3–19, bottom border 20,
      status 21, spare/messages 22, command line 23, F-bar 24; drop top menu
      bar / separator / help line.
- [x] `render_fkey_bar()` on row 24: number normal, label inverted
      (`3 View 4 Edit 5 Copy 6 RenMov 7 MkDir 8 Del 10 Quit`).
- [x] Column layout `|NAME 12|SIZE 5|DATE 10|TIME 5|` in `panel_render`:
      name as `NNNNNNNN EXT`, dirs plain; SIZE right-aligned 5 (`<DIR>`,
      `K` suffix > 99999); DATE `YYYY.MM.DD`; TIME `HH:MM`; `|` separators.
- [x] New helpers in `apps/ncd/str.c`: `m_itoa_pad2`, `m_format_size`;
      split `ncd_format_datetime` into date/time formatters (`fs.c`).
- [x] Selection bar only in active panel (`panel_render` gets active flag).
- [x] Fix `handle_f5` message: `m_u32toa(copied, msg)` directly.
- [x] Verify: `make run` — F-bar visible inverted at bottom, columns fit
      panes per spec, selector visible only in active panel.
- [x] Housekeeping: build, test.

## Phase 6 — NCD: F5 confirm, Shift-F4, launch polish

- [x] F5: `dlg_msgbox("Copy <name> to <dst>?", DB_JNA)` before copying.
- [x] Shift-F4 (`kbd_shift & SH_SHIFT` on K_F4): `dlg_input` for name →
      sync CWD → `ncd_create`+`ncd_close` → `ncd_exec("EDIT.COM", name)` →
      refresh.
- [x] Command line / Enter-launch: clear screen before `ncd_exec`, show
      "Press any key" + `kbd_get()` after, then refresh + render.
- [x] Verify: F5 asks confirmation and copies correctly named file; Shift-F4
      creates + edits new file; typed commands and Enter on .COM/.BAT run
      with visible output.
- [x] Housekeeping: build, test.

## Phase 7 — NCD user manual

- [x] Write `os-docs/NCD.md` (style of EDIT.md/MANUAL.md): panels, keys,
      F-keys incl. Shift-F4, command line, viewer.
- [x] Add `("NCD.md", "NCD.TXT")` to docs tuple in `build.py`; mention
      NCD.TXT in the injected README text.
- [x] Verify: rebuilt image contains DOCS/NCD.TXT (`cat` it in QEMU or
      mtools/loopback listing).
- [x] Housekeeping: build, test.

## Phase 8 — Final integration, verification, docs

- [x] `make check`, `make clean && make`, `make test` all green;
      NCD.COM < 16 KB.
- [x] QEMU verification of all 12 items:
      1. F4 on /DOCS file opens real content; 2. copy / → /TMP keeps name,
      no freeze on second copy; 3. F-bar visible; 4. NCD.TXT in DOCS;
      5. F3 enters folders; 6. F5 confirmation; 7. second copy fast;
      8. selector only in active panel; 9. Shift-F4 flow; 10. new column
      layout; 11. command line executes; 12. Enter launches .COM/.BAT.
- [x] Update `docs/wiki/ncd.md`, syscalls/vfs wiki page, `docs/wiki/index.md`,
      append `docs/wiki/log.md`; note round-2 fixes in `improvements.md`.
- [x] Housekeeping: final build; ask user about committing.

## Done when

- [x] All phases complete and checked off
- [x] `make clean && make` + `make test` green
- [x] All 12 issues verified fixed in QEMU
- [x] Wiki/docs updated
