# Observed Issues & Improvements

## Fixed

- [x] **`fat16_read` hangs due to `__U4D` unresolved symbol** — OpenWatcom `-ms`
  (small model) lacks 32-bit division runtime helpers. `fat16_read` used
  `uint32_t / uint16_t` and `uint32_t % uint16_t` which generated `__U4D`
  references that couldn't be resolved. Fixed by replacing with right-shift
  (`>> 9`) and bitwise AND (`& 0x1FF`) since `bytes_per_sector` is always 512.
  See `src/drivers/fat16.c:322-325`.

## Open

- [ ] **builtin.c dispatch logic inverted** — The `.COM` fall-through check
  `if (args || *args == '\0')` at `src/shell/builtin.c:281` is always true when
  args is non-NULL, so commands with arguments always fall through to `.COM`
  execution instead of showing "Unknown command". Should probably check
  `*args == '\0'` to only attempt exec when there are no arguments.

- [x] **No driver for reading PS/2 keyboard from serial input** — Shell reads
  from PS/2 keyboard controller (IO port 0x60), not UART serial port. This
  makes automated QEMU testing difficult (no `-serial stdio` input).
  Resolved: keyboard driver polls both PS/2 and serial. Serial already added earlier.

- [ ] **CAT.COM has file path hardcoded to "README  TXT"** — Doesn't accept
  command-line arguments. Fine for testing but limited.

- [ ] **LS.COM uses hardcoded buffer sizes** — `dir_buf[sizeof(uint16_t) * 4 + sizeof(void *)]`
  = 12 bytes for `fat16_dir_t`. Fragile if struct layout changes.

- [x] **`.err` files in working directory** — Build produces error listing files
  (e.g. `kmain.err`, `syscalls.err`) that should be in .gitignore or cleaned up.
  Resolved: untracked and added `*.err` to `.gitignore` (2026-07-09).

- [ ] **No RTC driver** — `date` command prints "Harvac has no RTC driver yet".
  CMOS/RTC syscalls (GET_TIME, SET_TIME, GET_DATE, SET_DATE) return stubs.

- [x] **`make check` target missing** — No syntax/lint target in Makefile.
  Resolved: `make check` (build.py --check) added previously.

- [x] **Editor app not ported** — Full-screen editor (`editor.asm`) from the
  original harva is 2000+ lines of assembly. Marked as optional in plan.md.
  Resolved 2026-07-09: MEDIT ported as EDIT.COM (`apps/medit/`); see wiki.

- [ ] **Batch scripts: out-of-scope features** — Nested `CALL script.bat`,
  positional parameters (`%1`..`%9`), `goto`/`:label`, `if`/`for`, batch files
  in `BIN/` subdirectory are not supported. Single active script; no script stack.

- [x] **Single pctx slot** — Only one nesting level (shell runs child; child
  cannot spawn further children via SYSCALL_EXEC). Fixed: replaced with a
  depth-8 pctx stack in syscall.asm; children can now spawn children up to
  8 levels deep. Also replaced fixed CHILD_SEGMENT with g_next_seg dynamic
  allocator: each child gets exactly PSP + code + 2KB stack, no wasted
  64KB windows. COM_SEGMENT moved from 0x4000 to 0x3000.
- [ ] **MEDIT: serial keyboard only gives ASCII** — In `-display none -serial stdio`
  QEMU mode, special keys (arrows, F-keys, Alt+key) are not available; MEDIT
  navigation is limited. Full keyboard requires VGA display + PS/2 keyboard.

- [x] **MEDIT: `SYSCALL_SET_CURSOR` row/col swap** — `video_set_cursor` in
  `src/kernel/syscalls.c` is called with `(col, row)` but the signature is
  `(row, col)`.  Cursor appears at wrong position.  Low severity (editor still
  usable, just cursor in wrong cell).  `src/kernel/syscalls.c` SYSCALL_SET_CURSOR
  case. Fixed 2026-07-09: swapped to `video_set_cursor((uint8_t)(bx >> 8), (uint8_t)bx)`.

- [ ] **Shell not visible with QEMU VGA display** — `out_byte()` only wrote to
  serial, so the shell was invisible when running QEMU without `-display none`.
  Fixed 2026-07-09: `video_tty_putchar` added to `video.c`; `out_byte` now writes
  to both serial and VGA; `READ_STDIN` echo goes to VGA too. `make display` added.

- [x] **XFER: LIST is CWD-only (no wildcard, no path arg)** — `SYSCALL_OPENDIR`
  takes no path argument; xfer.asm serve_list always enumerates the current
  working directory. `host.py ls subdir/` would require CHDIR first, which is
  not wired in host.py. Scope cut for v0.
  FIXED 2026-07-11: serve_list now honours the directory part of the spec —
  it saves the CWD (SYSCALL_GETCWD), CHDIRs to the requested directory,
  lists, and restores the CWD before T_CLOSE. `dir DOCS` and recursive
  `dir / -r` work from the host; unknown dirs return an empty listing.
  The wildcard pattern is still ignored (all entries returned).

- [x] **XFER: protocol v0 only (no timestamps)** — VERSION ACK payload is empty;
  host.py treats this as v0. GET/PUT do not exchange file modification times.
  Requires RTC driver to implement; no RTC driver exists yet. See `No RTC driver`
  item above.
  FIXED 2026-07-11: `.h_version` now ACKs with payload 0x01 (protocol v1),
  matching the v1 T_ENTRY layout the agent already sent. With the old empty
  ACK, host.py parsed dir-entry names at offset 5 (the zeroed time/date
  words) and every `dir` name came back blank on real hardware. Timestamps
  themselves are still zero (no RTC); that part stays open under `No RTC
  driver`.

- [x] **SYSCALL_CREATE/DELETE are root-only** — SYSCALL_OPEN is CWD-aware
  since 2026-07-11 (cwd_dir_cluster helper), but CREATE and DELETE still
  operate on the root directory, so `>` redirect and `touch`/`rm` in a
  subdirectory silently act on the root. Extend them with the same helper.
  (Also gates XFER: PWRITE refuses creating outside the root, and
  DEL/RMD/REN over the mount only work on root entries.)
  *Resolved 2026-07-12:* added `resolve_user_path()` + FAT16 `*_in_dir`
  variants; OPEN/CREATE/DELETE/MKDIR/RMDIR/STAT/RENAME now honor the CWD and
  absolute subdirectory paths at any depth. See [[vfs-layer]].

- [x] **XFER: GET/PREAD/PWRITE ignored the directory part of paths** —
  mountfs.py reads files via T_PREAD with full paths (`\DOCS\MANUAL.TXT`);
  the agent passed them straight to SYSCALL_OPEN (bare-name-in-CWD only),
  the open failed and the empty reply looked like EOF — every file read
  through the FUSE mount came back empty.
  FIXED 2026-07-11: shared path_enter/path_leave helpers in apps/xfer.asm
  CHDIR to the directory part and operate on the basename (same approach
  as serve_list). Verified end-to-end with a real mountfs.py FUSE mount in
  QEMU (`--root /` is required; the default `C:\` yields an empty mount).

- [x] **XFER: no local keyboard abort** — The original DOS agent polled INT 16h
  for a 'Q' keypress during uart_getc wait loops. Harvac port removed this.
  Quit path is T_QUIT packet from host.py only. Could add port 0x60 poll for
  local abort; apps/xfer.asm uart_getc wait loop.
  FIXED 2026-07-11: uart_getc now uses SYSCALL_KEY_AVAILABLE (new, 0x0B) +
  SYSCALL_READ_CHAR; the earlier direct 0x64/0x60 poll stopped working when
  the kernel keyboard went IRQ1-driven (and 0x64 does not exist on XT).

- [ ] **Real 8086 (IBM 5155 + XTIDE + CF) boot stops at `VRS.`** — The first
  root directory read (LBA 572, INT 13h AH=02h) hangs in the VBR. Mitigation
  added (second disk reset after AH=08h geometry query, vbr.asm .geo_done);
  needs a hardware retest with the current image now that the kernel-load
  cluster bug is fixed. See [[boot-chain]] geometry notes.

- [x] **keyboard_init() flush loop can spin forever on XT machines** — No PS/2
  controller at port 0x64 on 8086/XT means reads return 0xFF, so
  `while (status & KBD_OBF)` never exits (src/kernel/keyboard.c). Bound the
  loop like serial_init(). Only matters once the 8086 gets past the VBR.
  FIXED 2026-07-11: driver rewritten as IRQ1-driven; no status polling at
  all (src/drivers/keyboard.c, irq1_stub_ in syscall.asm).

- [ ] **DBG_VGA A-I markers in kmain() are temporary debug aids** — Direct
  0xB800 writes before each driver init (src/kernel/kmain.c:19-45). Remove
  or gate behind a build flag once real-hardware bring-up is done.

- [x] **Kernel requires 386+ even though boot chain is now 8086-clean** —
  entry.asm and syscall.asm declare `.386p`, and wcc may emit 186+ code
  depending on flags (build.py uses no explicit -0). To run the kernel on
  the real 8086/8088 (IBM 5155), audit the asm for 386-only instructions,
  add `-0` to wcc flags, and rebuild. Boot chain (MBR/VBR) already carries
  `cpu 8086` and is verified 8086-only.
  FIXED 2026-07-11: wcc now runs with -0, entry.asm/syscall.asm use `.8086`
  (the two `shl ax, 5` replaced with shift-by-CL), rt.asm audited clean.

- [ ] **App BSS is not zeroed by the loader** — startup.asm does not clear
  BSS and the exec loader only writes the raw image; C statics assume
  zero-initialization. Works today because each child loads at a fresh (or
  identically reused) segment, but stale values from a previous run of the
  same app can leak into statics. Zero the region above the image up to
  the 16 KB top in startup.asm or the exec loader.

- [ ] **EDIT clipboard unavailable on 256 KB machines** — clip_set's
  SYSCALL_ALLOC fails (returns 0) once the gap buffer has consumed the
  free 32 KB above the child. Degrades gracefully (copy/paste refused).
  Could reuse a single preallocated clip segment instead of allocating
  per copy (apps/medit/clip.c).

- [ ] **mem_alloc/mem_free (src/kernel/memory.c) are dead code** — nothing
  calls them, and HEAP_START 0x21000 would overlap the kernel image if
  they were used. Remove or rework if a kernel heap is ever needed.

- [ ] **LS.COM (user-space) shares NCD's SS!=DS bug conditions** — LS.COM is
  a separate user-space .COM app that uses OPENDIR/READDIR/CLOSEDIR. The
  kernel handlers are now fixed (all static locals), but LS.COM has never
  been verified in QEMU as a standalone .COM. Add a `make test` case that
  execs a directory-listing app.

- [x] **Shell cannot exec .COM files from subdirectories** — vfs_resolve
  extracts only the filename from resolved paths, discarding the directory
  part. `bin/ncd` is not found; the user must `cd bin` first (which itself
  may fail due to CHDIR issues with subdirectories). A PATH-like search or
  proper subdirectory .COM resolution is needed.
  *Resolved 2026-07-12:* `SYSCALL_EXEC` resolves a name containing `/` as an
  explicit path and searches bare names in CWD → root → `/BIN` (each with
  `.COM` auto-append). Subdirectory CHDIR at any depth now works too. See
  [[com-executor]].

- [x] **EDIT hung on multi-line files (process slot too small)** — EDIT.COM's
  near data reaches ~0x40E8, just past the old 16 KB (`PROC_PARAS=0x400`)
  process slot. Since a program's `SYSCALL_ALLOC` far blocks start at
  `child_seg+PROC_PARAS`, EDIT's near BSS aliased its own far gap buffer;
  loading a file overwrote `menu_wrap_flag` (nonzero → word-wrap layout loop).
  Reproduced on the untouched baseline from a plain shell, so pre-existing.
  *Resolved 2026-07-12:* raised `PROC_PARAS` to 0x600 (24 KB) and `PROC_SP` to
  0x5FFE, and `SYSCALL_EXEC` now zeroes the child slot before loading so BSS
  starts clean. See [[com-executor]].
