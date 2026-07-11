# Fix NCD: empty panels + hang

## Context

NCD.COM (dual-pane file manager) launches but shows no files and hangs. Investigation found two real bugs:

**Bug 1 — kernel (the visible failure).** The directory syscall handlers in `src/kernel/syscalls.c` use auto (stack) locals whose addresses are passed to callees:

- `SYSCALL_OPENDIR` (~line 517): `fat16_dir_t dir`, `char cwd_path[]`, inner `name_83`/`dirent`
- `SYSCALL_READDIR` (~575): `fat16_dir_t dir`, `fat16_dirent_t dirent`
- `SYSCALL_CLOSEDIR` (~603): `fat16_dir_t dir`
- `SYSCALL_STAT` (~492): `name_83`, `dirent`
- `SYSCALL_MKDIR`/`RMDIR` (~619/628): `name_83`
- `SYSCALL_CHDIR` (~637): `char path[]`
- `SYSCALL_GETCWD` (~654): `char cwd_buf[]`
- `SYSCALL_RENAME` (~478): `old_83`, `new_83`
- `SYSCALL_MOUNT` (~670): `mount_point` (not used by NCD, fix for consistency)

Per the documented rule in `docs/wiki/syscall-dispatch.md` ("Critical: SS != DS during the handler"): during INT 0x40 from a user app, SS = caller segment, DS = kernel segment. The handler accesses its locals BP/SS-relative, but callees (`fat16_readdir`, `vfs_getcwd`, `name_to_83`, ...) dereference the passed near pointer DS-relative — two different physical locations. Result: `fat16_readdir` sees a garbage dir struct and its updates never reach the handler's copy, so the dir cursor never advances → NCD's `while (ncd_readdir(...) == 0)` loop in `panel_refresh()` (apps/ncd/panel.c:76) spins forever before the first render. Empty screen + hang.

Why nothing else ever hit this: interactive `ls`/`cd`/`pwd` are kernel-shell builtins (SS==DS, autos work); MEDIT/CAT/XFER use OPEN/READ/WRITE handlers already fixed with statics (see `g_ob`, EXEC's `static uint8_t tmp[512]` at syscalls.c:845). NCD is the first user app driving OPENDIR/READDIR/GETCWD/CHDIR/STAT from a child segment. (LS.COM would be equally broken; the builtin masks it.)

**Bug 2 — NCD memory footprint (a landmine even after Bug 1).** Each process gets a 16 KB slot (`PROC_PARAS`=0x400, `PROC_SP`=0x3FFE, src/include/constants.h:129). NCD's DGROUP: 7.4 KB image + ~33 KB BSS — `panel_t` = cwd[64] + 512 entries × 24 B ≈ 12.1 KB **× 2 panels** (apps/ncd/panel.h, `MAX_ENTRIES` 512 in ncd.h:118), + 4 KB vid shadow + 4 KB `viewer_buf` + 512 B `io_buf`. The stack at 0x3FFE sits *inside* `panel_left.entries[]` (any directory with ≳150 entries smashes the live stack), and panel_right/viewer_buf overlap the next child slot (F4→EDIT.COM, shell-command→SHELL.COM load right on top of them). MEDIT solved the same problem with `SYSCALL_ALLOC` far segments (apps/medit/gap.c:28) + `__far` helpers (apps/medit/far.c/far.h) — reuse that pattern.

## Phase 0 — Reproduce (confirm diagnosis)

- `make` then boot headless with a monitor socket:
  `qemu-system-i386 -display none -serial stdio -monitor unix:/tmp/ncd-mon.sock,server,nowait -m 4 -drive file=harvac.img,format=raw`
- Type `ncd\n` via serial stdin; capture VGA with monitor `screendump` (PPM → view with Read); confirm empty/hung UI. `info registers` to confirm CS:IP spinning in NCD's readdir loop.

## Phase 1 — Kernel fix (resolves the symptom)

- `src/kernel/syscalls.c`: convert the auto locals listed above to `static`, with the same style of comment used at line 845 (`/* must be static: SS!=DS ... */`). No reentrancy concern (single-threaded kernel).
- Rebuild, rerun Phase-0 repro: both panes should now list root (KERNEL.COM, SHELL.COM, BIN, HOME, TMP, DOCS, README.TXT, DEMO.BAT) and keys should respond (verify with monitor `sendkey down`, `sendkey tab` + screendump — SYSCALL_READ_CHAR is PS/2-only, sendkey injects PS/2).

## Phase 2 — NCD memory fix (fit the 16 KB slot)

- Copy `far.h`/`far.c` from apps/medit into apps/ncd; add `"far"` to `NCD_SOURCES` in build.py.
- `panel.c`/`panel.h`: replace `panel_entry_t entries[MAX_ENTRIES]` inside `panel_t` with a far segment: one `SYSCALL_ALLOC` per panel at startup (512 × 24 B = 768 paras), fields `u16 ent_seg` + accessors `panel_entry_get(p, i, &tmp)` / `panel_entry_set(p, i, &tmp)` using `far_copy`/MK_FP (app-side SS==DS, so near→far casts of stack tmps are safe). Update `panel_sort`, `panel_refresh`, `panel_render`, `panel_toggle_select`, `panel_get_name`, `panel_enter_dir` and the users in `main.c` (handle_f3/f4/f5/f8 read entries) to go through the accessors. If ALLOC returns 0, degrade gracefully (count=0 + status message).
- `viewer.c`: move `viewer_buf[4096]` to a far segment too (alloc once; fill via chunked 512-byte `ncd_read` through a small static bounce buffer, or reuse `io_buf`).
- Resulting DGROUP ≈ 8 KB image+code-growth + ~4.7 KB BSS ≈ 13 KB, leaving ~3 KB stack headroom below 0x3FFE. Sanity-check with the wlink map (add `option map` temporarily or inspect `_ncd_link.lnk` output) that `_BSS` end + ~2 KB stack < 0x4000.
- Note: allocations survive child exec/exit correctly — `g_next_seg` is bumped past NCD's blocks, and child exit restores it to the child's base, not NCD's.

## Phase 3 — End-to-end verification

- `make test` (existing smoke tests still 2/2).
- Headless QEMU walk with monitor `sendkey` + `screendump` after each step:
  1. `ncd` → both panes list root, path headers `/`
  2. `sendkey down` ×2, `sendkey ret` on BIN → pane lists the 7 apps
  3. `sendkey tab` → right pane active (status line `[Right]`)
  4. F3 on README.TXT (root) → viewer shows content; Esc closes
  5. `sendkey f10` → clean exit back to shell prompt (verify prompt echoes on serial)
- Regression: `ls`, `cd DOCS` + `cat MANUAL.TXT` from the shell still work (builtins + CWD-aware OPEN paths touch the same handlers).

## Phase 4 — Docs (mandatory wiki workflow)

- Create `docs/wiki/ncd.md` (the index already links `[[ncd]]` as pending): code-map page — architecture, far-memory layout, syscall usage, the two bugs fixed.
- Update `docs/wiki/syscall-dispatch.md`: extend the SS!=DS section — directory/path handlers now static, note that handler-local autos are forbidden in `syscall_handler_c`.
- Update `docs/wiki/index.md` (mark [[ncd]] complete) and append one line to `docs/wiki/log.md`.
- `docs/improvements.md`: add unchecked item — LS.COM (user-space) shared Bug 1's conditions and deserves its own QEMU verification; also consider a `make test` case that execs a directory-listing app.

## Files to modify

- `src/kernel/syscalls.c` — statics in 9 handlers
- `apps/ncd/panel.h`, `panel.c`, `main.c`, `viewer.c`, new `far.c`/`far.h` (copied from apps/medit)
- `build.py` — `NCD_SOURCES` += "far"
- `docs/wiki/{ncd.md,syscall-dispatch.md,index.md,log.md}`, `docs/improvements.md`
