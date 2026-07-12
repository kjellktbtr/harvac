# Operation Log

2026-07-12 — **Phase 12 complete:** Extracted shared POSIX-like libc (lib/posix/) and Harva Development Kit (lib/hdk/). Deleted duplicate vid/kbd/far/str/dlg sources from medit and ncd. Kernel extended with WRITE_STDERR, serial device fd, lseek whence, TELL, GET_PID. All apps (hello, cat, ls, uname, shell, EDIT, NCD) build cleanly; 2/2 smoke tests pass.

2025-07-05 — Bootstrapped wiki structure (CLAUDE.md, index.md, log.md)
2025-07-05 — Created initial wiki pages: toolchain.md, architecture.md
2025-07-05 — **Phase 0 complete:** Toolchain verified (wcc/wasm/wlink/nasm). Kernel compiles,
  links to flat binary, boots in QEMU via MBR → VBR → kernel chain.
  - Fixed bug: FAT16 directory entry first-cluster field was at offset 22 instead of 26 (0x1A)
    so VBR read cluster 0 and loaded garbage. Kernel is now executed and outputs
    "Harvac kernel booted (OpenWatcom C) !" over serial and VGA.
  - Cleaned up debug chars from bootloaders.
  - Key insight: `push cs; push offset; retf` works for far jump from real-mode 16-bit code.
2025-07-05 — **Phase 2/3 started:** Video driver (VGA text mode 80x25), serial driver
  (COM1 115200 8N1), keyboard driver (PS/2 polling, scancode set 1, shift/caps),
  INT 0x40 syscall dispatcher (assembly stub calling C dispatch table).
  - Fixed KERNEL_SEGMENT constant (0x1000 → 0x2000) to match actual load address.
  - Drivers compile clean with OpenWatcom's pragma-aux port I/O functions.
  - Build pipeline now compiles 5 C files + 2 ASM files → 1520 byte kernel.
2026-07-05 — **Phase 4 FAT16 driver + file syscalls complete.**
  - Added opendir/readdir/closedir to `fat16.c` and `fat16.h` with `fat16_dir_t` type
  - Wired SYSCALL_OPEN/CLOSE/READ/SEEK/STAT with open file table (16 slots)
2026-07-11 — **NCD (Norton Commander clone) build complete.**
  - Fixed C89 violations in `apps/ncd/`: moved variable declarations to top of scopes
    (fs.c, panel.c), removed unused variables, fixed `render_pane_borders` static/extern mismatch
  - Replaced `/` and `%` operators in `m_itoa`/`m_u32toa` with repeated subtraction
    to avoid OpenWatcom runtime lib dependency (`__U4D` undefined reference)
  - NCD.COM links as 7460-byte flat binary, builds cleanly via `make`
  - Wired SYSCALL_OPENDIR/READDIR/CLOSEDIR
  - Mount root FAT16 partition in syscall_init
  - Fixed `fat16_read` hang: replaced 32-bit division with shifts to avoid
    `__U4D` unresolved symbol (OpenWatcom `-ms` lacks 32-bit division helpers)
  - Verified in QEMU: root directory listing, fat16_open, fat16_read all work
  - Documented: fat16-driver.md, syscall-dispatch.md, improvements.md
2026-07-05 — **Phase 4 write/create/delete complete.**
  - Added fat16_write, fat16_create, fat16_delete to fat16.c
  - Added helpers: fat16_write_fat, fat16_find_free_cluster, fat16_write_cluster,
    fat16_free_chain, fat16_update_fsize
  - Updated fat16_find to return directory entry location (for updating file size)
  - Wired SYSCALL_WRITE, SYSCALL_CREATE, SYSCALL_DELETE in syscalls.c
  - Verified in QEMU: create TEST.TXT, write 64 bytes, seek to 0, readback matches, delete OK
2026-07-05 — **Phase 6 .COM execution complete.**
  - Fixed syscall handler to save AX before overwriting with KERNEL_SEG
  - Fixed handler to use OpenWatcom register calling convention (params in AX/BX/CX/DX)
  - Passed caller's DS to C handler for far memory access (read_far_b/write_far_b)
  - Fixed name_to_83 to accept (seg, off) instead of direct pointer
  - Fixed exec_stub.asm to restore DS=ES=KERNEL_SEG before calling shell_run_
  - Verified: HELLO.COM executes, prints "Hello from .COM!", returns to shell
  - Documented in com-executor.md; marked plan.md Phase 6 items complete
2026-07-06 — **Phase 7 complete: User .COM apps and documentation.**
  - Created wiki pages: user-apps.md, disk-driver.md, vfs-layer.md, shell.md
  - Updated fat16-driver.md (mkdir/rmdir), syscall-dispatch.md (all syscalls), index.md
  - Wired MOUNT/UNMOUNT/LIST_MOUNTS syscalls (were stubs returning ERR_NOT_IMPLEMENTED)
  - Marked all Phase 4/5 items done in plan.md (code was already implemented)
2026-07-07 — **Bug fix session: SYSCALL_EXEC BIN/ launch working end-to-end.**
  - Fixed OpenWatcom __watcall 16-bit register mismatch in syscall.asm: 2nd param
    goes in DX not BX; had `mov bx,[bp-2]` (BX->BX) but needed `mov dx,[bp-2]`
    (BX->DX). This caused SYSCALL_EXEC filename pointer to always be 0.
  - Fixed SS!=DS segment mismatch bug in fat16_open and fat16_open_in_dir:
    local fat16_dirent_t dirent was on the stack (SS=COM_SEGMENT) but callees
    wrote through near pointer using DS=KERNEL_SEGMENT, so the write landed at
    the wrong physical address. Made dirent, sector_lba, entry_offset static.
  - Made fat16_file_t file, uint8_t name_83[], fat16_dirent_t bin_dirent,
    uint8_t with_ext[], uint8_t tmp[512] all static in SYSCALL_EXEC case for
    same SS!=DS reason.
  - Added static dap_t dap in disk_read_sectors/disk_write_sectors (BIOS
    INT 13h reads the DAP at DS:SI; stack DAP at SS!=DS would be invisible).
  - Fixed build.py: BIN/ directory entry extension field was NUL (00 00 00)
    instead of space-padded, causing name_match to fail.
  - Fixed name_to_83: now upper-cases each byte so lowercase "hello" finds
    the FAT "HELLO   COM" entry.
  - Verified: hello, ls (lowercase and uppercase), uname all execute correctly
    from BIN/ and return to shell via exec_reentry/boot_shell.
  - Documented SS!=DS rule in fat16-driver.md and register convention in
    syscall-dispatch.md.
2026-07-09 — **pctx stack + variable child allocation + bat fallback fix.**
  - Replaced single pctx slot with depth-8 pctx stack in syscall.asm (each entry
    32 bytes: child_seg + 14 save-area fields). exec_save_parent_ctx_ indexes
    by pctx_depth * 32; return_to_parent_ dec depth, restores entry.
  - Removed fixed CHILD_SEGMENT (0x6000); added g_next_seg CS-relative DW with
    get_next_seg_ / set_next_seg_ accessors. Each child allocs
    `16 + ((filesize+15)>>4) + 128` paras; g_next_seg reset to child_seg on exit.
  - Moved COM_SEGMENT from 0x4000 to 0x3000 (fits in 256KB; children start
    above shell at first g_next_seg after shell load).
  - Added exec_far_jump_sp 5-param pragma aux (SI=sp_init) so each child gets
    its own initial SP = load_off + 2048 - 2 (top of 2KB stack above code).
  - Fixed C UB in shell.c bat fallback: `bat_name[blen] = cmd[blen++]` was
    undefined behavior; OpenWatcom increments SI before writing, so bat_name[0]
    was never set -> SYSCALL_OPEN saw empty filename -> "Unknown command".
    Split into `bat_name[blen] = cmd[blen]; blen++;`. Removed debug putstr lines.
  - Verified: `demo` (no extension) now runs DEMO.BAT correctly. Full regression
    pass: ls, hello, uname, redirect, cat, demo.bat, demo, foo.bat error, exit.
  - Updated wiki: com-executor.md (full rewrite), user-apps.md, shell.md.
  - Checked off single-pctx-slot item in improvements.md.
2026-07-09 — **DOS-model resident shell + batch scripts (.BAT) complete.**
  - Refactored SYSCALL_EXEC to return: children load at CHILD_SEGMENT 0x6000;
    shell stays resident at COM_SEGMENT 0x4000 (non-overlapping 64 KB windows).
  - Added pctx machinery to syscall.asm: exec_save_parent_ctx_ copies the save
    area to a pctx buffer before the child far-jump; child_return_ is the retf
    trampoline; return_to_parent_ restores pctx and irets back to the shell's
    SYSCALL_EXEC call site (AX=0). Single nesting level (one pctx slot).
  - root shell exit (pctx_valid=0): return_to_parent_ falls back to
    exec_reentry_ -> boot_shell, same behavior as before.
  - Replaced SYSCALL_EXIT's exec_far_jump with return_to_parent() call.
  - Rewrote apps/shell.c: EXEC result inverted (0=success, ERR_NOT_FOUND=fail),
    redirect cleanup unified (runs after EXEC returns for external commands),
    batch loop with read_bat_line(), @/echo on/off support, .BAT extension
    detection, no-extension .BAT fallback after failed EXEC, exit closes batch.
  - Added DEMO.BAT to disk image (injected by build.py).
  - Headline proof: ls/uname/hello no longer reprint "Booting SHELL.COM..."
    between runs; the shell prompt returns directly.
  - Verified: demo.bat runs @echo off + ls + cat readme.txt + echo Done in order;
    scripts with external commands work because EXEC returns; echo/redirect
    regressions pass; exit restarts shell correctly.
  - Updated wiki: com-executor.md (full rewrite), shell.md (batch section),
    user-apps.md, log.md.
2026-07-08 — **Phase 8+: stdout redirection + DOS-style ls complete.**
  - Added SYSCALL_SET_STDOUT (0x08): BX = open-file handle or 0xFFFF to revert to serial.
    All WRITE_CHAR and WRITE_STDOUT output routes through out_byte() in syscalls.c which
    writes to the redirect file or serial depending on g_redirect.
  - Added syscall_finalize_redirect() called by boot_shell() on every exec re-entry to
    close the redirect file after external .COM commands.
  - Fixed SYSCALL_CREATE error return: was ERR_NO_MEMORY=5/ERR_TOO_MANY_FILES=8 which
    collide with valid handle range 0-15. Changed to return 0xFFFF on all failures.
  - Rewrote user-space SHELL.COM (apps/shell.c) to parse '>' redirect operator,
    DELETE+CREATE the target file, SET_STDOUT before dispatch, and cleanup after.
  - Rewrote apps/ls.c as a two-pass DOS-style listing: directories first, then files
    with 8+1+3 column alignment and right-justified byte sizes. Footer: "N file(s)".
  - Root cause of ls garbage output: wlink system com places functions in source order;
    _main must be the FIRST function defined. Helpers had been placed before _main,
    causing putch to execute instead of _main on entry at offset 0x100.
  - Verified all four scenarios in QEMU: ls (dirs first, sizes), echo hello > test.txt,
    cat test.txt (prints "hello"), ls > out.txt, cat out.txt (shows ls listing in file).
  - Updated wiki: syscall-dispatch.md, user-apps.md, shell.md.
2026-07-11 — **NCD memory fix: far segments for panels and viewer buffer.**
  - Copied far.h/far.c from apps/medit to apps/ncd; added to NCD_SOURCES in build.py
  - Replaced inline panel_entry_t entries[512] (12 KB × 2 panels) with SYSCALL_ALLOC
    far segments; accessors panel_entry_get/set use far_copy from far.h
  - Moved viewer_buf[4096] to far segment allocated in viewer_open, filled via
    512-byte bounce buffer through ncd_read
  - Fixed C89 mid-block declarations in panel_sort (int swap -> declared at top)
  - Moved _main to be the first function in main.c (wlink source-order rule for
    .COM entry point at 0x0100)
  - Verified: kernel boots, NCD launches via `ncd` from root shell
  - Created docs/wiki/ncd.md; updated index.md (mark complete) and
    syscall-dispatch.md (note all handlers use static locals)
2026-07-08 — **Bug fix session: ls and cat working end-to-end.**
  - Fixed SYSCALL_READ and SYSCALL_WRITE: uint8_t tmp[32] was auto (SS-relative)
    but fat16_read/fat16_write write through near ptr (DS=KERNEL_SEGMENT).
    Made tmp static so both sides agree on the physical address.
  - Fixed SYSCALL_OPEN error return: returned ERR_NOT_FOUND=1 on failure, which
    falls inside the valid handle range 0-15. Changed to return 0xFFFF so
    user apps can detect failure with `handle >= 16`.
  - Implemented argument passing: SYSCALL_EXEC now reads arg string from cx
    (caller CX, 4th arg) and writes it to COM_SEGMENT:0x0082 before the far
    jump, matching PSP command-line convention.
  - Updated shell.c: SYSCALL_EXEC call now passes (line+j) in CX as the
    argument string (line+j is argv[1] or empty string if no argument).
  - Rewrote cat.c: reads filename from DS:0x0082 (=COM_SEGMENT:0x0082, written
    by kernel), falls back to README.TXT if empty. Removed hardcoded pre-packed
    8.3 name; passes dotted filename (e.g. "readme.txt") to SYSCALL_OPEN and
    lets name_to_83 convert it.
  - Added fat16_opendir_cluster (open subdir by cluster) and extended
    fat16_readdir to handle subdirectory iteration (dir_cluster != 0).
  - Extended SYSCALL_OPENDIR to resolve CWD and open subdirectory if not root.
  - Verified: ls lists 6 root entries (KERNEL.COM, README.TXT, SHELL.COM,
    BIN/HOME/TMP dirs); cat readme.txt and cat with no arg both print the file.
2026-07-09 — **MEDIT text editor ported as EDIT.COM (`apps/medit/`).**
  - Ported 12 source files: main, vid, kbd, doc, dlg, gap, clip (rewritten),
    edit, menu, search (string.h removed only), str and far (new helpers).
  - Replaced DOS INT 10h/16h/21h with Harvac INT 0x40 syscalls throughout.
  - Added kernel syscalls: SYSCALL_ALLOC (0x50), SYSCALL_FREE (0x51),
    SYSCALL_GET_SHIFT (0x09), SYSCALL_GET_CURSOR (0x07).
  - Extended SYSCALL_READ_CHAR to return 16-bit key events (AH=scan, AL=ascii).
  - Extended keyboard driver: Alt/Ctrl tracking, extended-key (0xE0) prefix,
    F-key support; ring buffer widened from char to uint16_t.
  - Fixed critical stack/BSS overlap: BSS is not written to raw binary but
    linker assigns addresses starting at load_off; the old 2KB stack placed
    at load_off overlapped BSS.  Fixed by setting child_sp=0xFFFE and
    alloc_paras=0x1000 (64 KB per child, DOS COM convention) in both
    SYSCALL_EXEC (syscalls.c) and exec_com (exec.c).
  - Fixed SYSCALL_ALLOC using AX (= 0x5000 = syscall_num<<8) for paragraph
    count instead of BX (the actual parameter). Now correctly uses `bx`.
  - EDIT.COM built as 9862-byte flat binary; injected into BIN/ on disk image.
  - Verified: `edit`, `edit readme.txt` launch MEDIT; Ctrl+Q exits cleanly to
    resident shell; `ls`, `cat`, batch all continue to work.
  - Created docs/wiki/medit-harvac.md; updated index.md.
2026-07-09 — **VGA TTY output + QEMU display mode working.**
  - Added `video_tty_putchar(char c)` and `video_tty_puts(const char *s)` to
    `src/drivers/video.c`: stateful terminal output tracking row/col, handling
    \r \n \b, auto-scrolling, updating hardware cursor after each char.
  - `video_init()` and `video_clear()` now reset tty_row=0, tty_col=0.
  - `out_byte()` in `src/kernel/syscalls.c` now calls `video_tty_putchar` in
    addition to `serial_putchar` (when not redirecting to file).
  - `SYSCALL_READ_STDIN` echo now also echoes to VGA via `video_tty_putchar`.
  - `kmain.c` boot banner uses `video_tty_puts` instead of `video_write`.
  - Fixed `SYSCALL_SET_CURSOR` row/col swap: now correctly calls
    `video_set_cursor((uint8_t)(bx >> 8), (uint8_t)bx)`.
  - Added `make display` / `build.py --display` to launch QEMU with VGA display
    and PS/2 keyboard (no serial stdio).
  - Verified: serial mode unchanged; display mode shows shell output on screen.
2026-07-09 — **Real hardware boot fix: VBR CHS read compatibility + debug output.**
  - MBR: added disk reset at startup + 3-attempt retry with reset between
    attempts. Fixes intermittent "Disk err" on first read (real hardware needs
    controller reset before reads are reliable).
  - VBR: replaced INT 13h AH=42h (Extended LBA Read) with standard CHS read
    (AH=02h). Extended INT 13h is not available on 8086 BIOSes or most
    386-era BIOSes; was causing silent failure (carry set) and VBR hang.
  - VBR: removed `setnc` (386+ instruction; on 8086 opcode 0F = POP CS,
    corrupting execution). Replaced with jc/mov/jmp pattern.
  - VBR: removed `mov dword` (386+ instruction). No longer needed.
  - VBR: removed INT 13h AH=08h geometry query (too risky: old BIOSes may
    return CF=clear with garbage CL=0, causing divide-by-zero in read_sector).
    Geometry hardcoded to 63 SPT / 255 heads matching the disk format.
  - VBR read_sector: now sets ES=0x0900 explicitly before INT 13h to guard
    against ES corruption by earlier BIOS calls.
  - VBR debug chars: dbg_char now writes to both COM1 (0x3F8) and VGA (INT 10h
    AH=0Eh); restores DS=ES=0x0900 after INT 10h to guard against BIOS corruption.
  - VBR AX corruption fix: `mov al, 'X'; call dbg_char` in both .search and
    .loop was clobbering AL (LBA/cluster low byte) before the value was consumed.
    Fixed by wrapping each debug call with push ax / pop ax around it.
    Root cause: .loop reads LBA from AX after dbg_char returns; the 'mov al'
    changed 0x023C (572) to 0x022E (558), causing read_sector to read the wrong
    root-dir sectors so KERNEL.COM was never found.
  - Debug sequence visible on real hardware screen: V(VBR), R(reset), S(search),
    .(per root sector), K(found), L(loading), *(per kernel sector), J(jump).
  - Verified in QEMU: output VRS.KL*******************J then kernel runs.
2026-07-09 — **Phase 10: XFER.COM serial file-transfer agent complete.**
  - Ported `xfercom.asm` (DOS NASM agent) to Harvac as `apps/xfer.asm`.
    Protocol core (crc16, crc32, COBS, read_frame, send_packet, wait_ack) kept
    verbatim; OS-boundary routines rewired to INT 0x40.
  - Added `SYSCALL_WRITE_VGA` (0x0A): writes NUL-terminated string from
    caller_ds:SI to VGA only (video_tty_putchar loop), never to serial.
    Used by XFER to display banner and MSG/RAW host text without protocol
    corruption. Wired in src/kernel/syscalls.c; constant in src/include/constants.h.
  - Added `SYSCALL_RENAME` (0x18): SI=old-name offset, DI=new-name offset,
    both in caller_ds; calls fat16_rename(old, new). Wired in syscalls.c.
  - `parse_args` fix: reads from PSP+0x82 (Harvac EXEC writes arg string there;
    was +0x81 which is always 0x00 in QEMU) and treats NUL as end-of-args at
    every check (original only checked 0x0D).
  - `make xfer` / `build.py --xfer-run`: QEMU with VGA display + COM1 on
    Unix socket /tmp/harvac-xfer.sock + QMP on /tmp/harvac-qmp.sock.
  - XFER.COM built as flat .COM (nasm -f bin), injected into BIN/ at ~2.6 KB.
  - Verified end-to-end via QMP keyboard injection + Unix socket: VERSION
    handshake (empty ACK = v0) and LIST (all 7 root entries + T_CLOSE) correct.
  - Scope cuts: LIST is CWD-only (no wildcard), protocol v0 (no timestamps),
    no local keyboard abort (quit via T_QUIT from host.py).
  - Created docs/wiki/xfer.md; updated syscall-dispatch.md (WRITE_VGA, RENAME),
    index.md, improvements.md.

2026-07-10 — **Critical VBR bug fixed: kernel loaded from wrong cluster.**
  - Symptom: full `VRS.KL*...*J` boot sequence but kernel never reached
    kmain() (QEMU and real 386). Physical 0x20000 held garbage.
  - Root cause: `.load_kernel` did `mov al, 'L'` for the debug marker while
    AX still held the start cluster (2). AX became 0x004C = 76, so the FAT
    chain was followed from cluster 76 (LBA 678) instead of 2 (LBA 604).
  - Found via QEMU gdb stub: breakpoint after `call read_sector` showed
    AX=0x2A6=678; 678-604+2 = 76 = 0x4C = ASCII 'L'.
  - Fix: push/pop AX around the marker (vbr.asm .load_kernel). VBR now
    exactly 453 bytes (at limit).
  - Also this session: MBR dynamic CHS for LBA 63 (geometry query),
    VBR second disk reset after AH=08h (XTIDE mitigation), bounded
    serial_init() drain loop, DBG_VGA A-I markers in kmain().
  - QEMU: boots to shell; make test 2/2 PASS.
  - Created [[boot-chain]] wiki page (boot stages, debug marker legend,
    gdb breakpoint recipes); updated index.md.
  - Open: real 8086 (IBM 5155+XTIDE+CF) still stops at `VRS.` (root dir
    read hang) - needs hardware retest with new image.

2026-07-10 — **Real-hardware compat: CHS disk driver + 8086-safe bootloader.**
  - 86Box 386 froze after "Drivers OK": kernel disk driver used INT 13h
    AH=42h/43h (EDD) which does not exist on pre-1995 BIOSes. Rewrote
    src/drivers/disk.c to CHS AH=02h/03h with lazy AH=08h geometry query
    (fallback 63/16), 1 sector per call, reset+retry x3. Removed dap_t.
    New pragma helpers in port_io.h: disk_chs_read_int/write_int,
    disk_query_geom_int, disk_reset_int.
  - 86Box 8088 stuck at "Harvac>": MBR far "jump" used push imm16/retf;
    push imm16 is a 186+ opcode that misdecodes as a conditional jump on
    a real 8086/8088. Replaced with direct `jmp 0x0900:0x003B` (MBR) and
    `jmp 0x2000:0x0000` (VBR). Added `cpu 8086` directive to both boot
    files so NASM rejects 186+ instructions at assembly time.
  - QEMU verified: boots to shell; ls, cat README.TXT, .COM exec via
    serial all work through the CHS path. make test 2/2 PASS.
  - Rewrote [[disk-driver]] wiki page (CHS design + EDD history);
    updated [[boot-chain]].
  - Note: kernel .asm files use .386p (pusha etc.) - kernel itself still
    requires 386+; 8088 target only gets the boot chain for now.

2026-07-11 — **Kernel made 8086-clean; keyboard rewritten as IRQ1-driven.**
  - 86Box 8088 reached J (kernel jump) then froze: kernel contained
    186+/386+ code. Three fixes:
    (1) build.py: wcc now compiles with -0 (8086 codegen) for kernel and
        all apps;
    (2) entry.asm/syscall.asm: .386p -> .8086 so wasm rejects 186+ ops;
        the two `shl ax, 5` (186+ shift-by-imm) became mov cl,5/shl ax,cl;
    (3) rt.asm audited - already 8086-only (shift-by-1, rcl chains).
  - Keyboard driver rewritten from AT status-polling to IRQ1-driven
    (src/drivers/keyboard.c, new irq1_stub_ in syscall.asm): the AT status
    port 0x64 does not exist on XT machines (reads float 0xFF -> infinite
    flush loop, garbage polls). IRQ1 handler reads port 0x60, pulses 8255
    port 0x61 bit 7 (XT ack, harmless on AT), installs at IVT[9] replacing
    the BIOS INT 09h handler; the old "mask IRQ1" workaround removed.
  - Verified in QEMU: monitor sendkey injection of "ls<ret>" executes via
    the PS/2 IRQ path; serial input still works; make test 2/2 PASS.
  - Disassembly scan of KERNEL.COM shows no real 186+ ops (only ndisasm
    false positives on ASCII string data).
  - Checked off both items in improvements.md; 86Box 8088/real 5155
    verification pending.

2026-07-11 — **8088 sim command freeze: child exec exceeded physical RAM.**
  - 86Box IBM 5155 sim booted to shell but froze on any command. Root
    cause was configuration, not code: the sim had mem_size=256 (256 KB).
    Memory layout: kernel 0x2000 (128K), SHELL.COM 0x3000 (192K, stack at
    0x3FFFE = last word of 256K), child exec at 0x4000 (256K+) - entirely
    beyond physical RAM. Shell worked; any child load/stack use froze.
  - Fixed the sim config (mem_size=640, matching the real 5155's 640K).
  - Added a kernel-side guard in SYSCALL_EXEC (syscalls.c load_and_exec):
    reads BDA 0040:0013 (base RAM in KB) and returns ERR_NO_MEMORY when
    child_seg + 64K would exceed RAM top, instead of freezing. This also
    protects nested exec depth on 640K machines (7th level would hit VGA
    memory at 0xA000).
  - QEMU verified: ls/uname exec fine with the guard; make test 2/2 PASS.

2026-07-11 — **Memory conservation: 16 KB process quantum for 256 KB machines.**
  - The real IBM 5155 has 256 KB RAM (the sim had been set to 640). With
    64 KB per process, nothing could exec (child at 0x4000 = past 256 KB).
  - New convention: PROC_PARAS = 16 KB per process, initial SP = 0x3FFE
    (constants.h). Applied in exec.c (boot shell load) and syscalls.c
    SYSCALL_EXEC. All apps fit: largest is EDIT.COM ~10 KB image +
    ~2.5 KB BSS, leaving ~3.5 KB stack headroom.
  - SYSCALL_ALLOC now returns 0 instead of handing out segments past the
    BDA RAM top; EXEC guard updated to PROC_PARAS.
  - medit gb_init tries 48000 bytes, then 32 KB, then 16 KB for the gap
    buffer; doc_load caps file size to gb_cap()-1000 instead of the fixed
    GB_MAX_TEXT so the fallback sizes are honored (apps/medit/gap.c,
    doc.c). On 256 KB, EDIT gets the 32 KB tier (~31.7 KB max file);
    clipboard alloc may fail gracefully (clip_set returns -1).
  - 256 KB layout: kernel 0x2000-0x2FFF, shell 0x3000, child 0x3400,
    32 KB free at 0x3800 for ALLOC.
  - QEMU verified: ls/cat/uname, DEMO.BAT nested exec, EDIT startup all
    work; make test 2/2 PASS. 86Box 5155 sim at 256 KB is the
    verification target before writing the CF card.

2026-07-11 — **XFER local Q-quit fixed via new SYSCALL_KEY_AVAILABLE.**
  - xfer.asm's uart_getc polled ports 0x64/0x60 for the Q key. Broken
    twice over: the kernel keyboard driver is now IRQ1-driven (the kernel
    consumes every scancode before a poll can see it), and port 0x64 does
    not exist on XT machines.
  - Added SYSCALL_KEY_AVAILABLE (0x0B): non-blocking, returns nonzero if
    a key event is buffered; reads only the keyboard ring buffer, never
    the serial port (critical for XFER, which owns the UART).
  - uart_getc now: KEY_AVAILABLE -> READ_CHAR -> compare 'q'/'Q' -> quit.
  - QEMU verified via monitor sendkey: xfer launches, q quits back to
    the shell, uname runs fine afterwards.
  - Updated syscall-dispatch.md (0x0B), xfer.md (scope cut removed),
    improvements.md (item checked off).

2026-07-11 — **Open-source release prep: XFER v1, EDIT Esc, 4 MB image, DOCS/ on image.**
  - XFER blank-names bug on real hardware fixed: the agent sent v1-layout
    T_ENTRY packets (time/date before the name) but replied an empty VERSION
    ACK (= v0), so host.py parsed names at offset 5 and got the zeroed
    time/date bytes. `.h_version` now ACKs with payload 0x01. xfer.md updated.
  - EDIT Escape fixed: `K_ESC` was DOS ASCII 0x1B; the kernel delivers Esc as
    scancode-only, which kbd_get() normalizes to K_SPECIAL|0x01. medit.h
    updated; all four menu/dialog Esc checks now match. medit-harvac.md updated.
  - Disk image shrunk 32 MB -> 4 MB (8192 sectors, FAT_SECTORS 254 -> 32,
    8032 clusters): build.py, vbr.asm equate, mbr.asm partition size (dd 8129).
    boot-chain.md layout table updated.
  - SYSCALL_OPEN made CWD-aware via new cwd_dir_cluster() helper (same
    one-level resolution as OPENDIR) so files in subdirectories can be read
    (`cd DOCS` + `cat MANUAL.TXT`). syscall-dispatch.md updated.
  - os-docs/ adapted from DOS to HarvaC (XFER.md, EDIT.md), new MANUAL.md
    user manual; all three embedded on the image as DOCS/*.TXT by build.py
    (new page: on-image-docs.md). README.md rewritten for the open-source
    release; .gitignore extended (build/obj/, keep img+COM tracked).

2026-07-11 — **XFER LIST now honours the directory path in the spec.**
  - `dir DOCS` from host.py used to return the agent's CWD (the root),
    because serve_list ignored the spec entirely (kernel OPENDIR takes no
    path). serve_list now parses the spec: '\'->'/', truncate at the last
    separator, GETCWD to save, CHDIR to the target (leading '/' = root),
    list, CHDIR back before T_CLOSE. Unknown dir -> empty listing; wildcard
    still ignored. Added SYSCALL_CHDIR/GETCWD defines + v_didcd/v_cwdsave
    to apps/xfer.asm (XFER.COM 2669 -> 2792 bytes).
  - QEMU-verified: dir /, dir DOCS, dir BIN, dir BOGUS (empty), recursive
    dir / -r walks BIN/HOME/TMP/DOCS, and an upload after dir DOCS still
    lands in the root (CWD restored). xfer.md, os-docs/XFER.md,
    improvements.md updated.
- 2026-07-11: Fixed lowercase-CWD bug: `cd docs` updated the prompt but `ls`
  listed the root. vfs_chdir now stores the CWD upper-cased (src/fs/vfs.c),
  and the CWD-to-8.3 copy loops in cwd_dir_cluster() and SYSCALL_OPENDIR
  (src/kernel/syscalls.c) upper-case before fat16_find. QEMU-verified:
  `cd docs`/`cd DOCS` + ls, `cat manual.txt` lowercase, `cd ..` back to root.
  Updated [[vfs-layer]] and [[syscall-dispatch]].
- 2026-07-11: Fixed empty file reads over the mountfs.py FUSE mount: XFER's
  GET/PREAD/PWRITE handlers passed full paths (\DOCS\MANUAL.TXT) to the
  bare-name SYSCALL_OPEN; added path_enter/path_leave (CHDIR to dir part,
  operate on basename, restore CWD) in apps/xfer.asm. PWRITE refuses
  creating outside the root (SYSCALL_CREATE is root-only). QEMU-verified:
  10-case Link pread/pwrite suite, download DOCS/MANUAL.TXT, and a real
  FUSE mount (--root /) with byte-identical reads and write-through.
  Updated [[xfer]], os-docs/XFER.md, improvements.md.

- 2026-07-11: build.py — NCD.COM was built but never injected into
  harvac.img; added it via new BIN_APPS tuple. Optimized image population:
  apps are now written once, directly into BIN/ with write_file_to_subdir(),
  replacing the old write-to-root-then-copy-to-BIN flow that duplicated
  every app's data (root FAT chains were never freed). Removed the now-unused
  delete_file_from_root(). Verified with mtools (BIN/ holds all 7 apps,
  root only KERNEL/SHELL) and `make test` (2/2 pass). Updated [[user-apps]].

- 2026-07-11: Fix 9 NCD bugs — Tab key (keyboard.c scancode 0x0F→\\t),
  scroll-clamp underflow in panel_refresh() (panel.c), m_itoa/m_u32toa
  garbage bytes (str.c), FAT16 zero timestamps (build.py, fs.c), viewer
  scroll support (viewer.c), F3 on folders (main.c), Enter on .BAT/.COM +
  shell prompt on typing (main.c, str.c, str.h).

- 2026-07-12: Fix 12 NCD round-2 bugs, mostly kernel-side. Kernel: multi-level
  path resolution (`vfs_abspath`/`vfs_name_to_83`/`vfs_resolve_dir` in vfs.c,
  `resolve_user_path` in syscalls.c) + FAT16 `*_in_dir` variants so
  OPEN/CREATE/DELETE/MKDIR/RMDIR/STAT/RENAME honor subdirectories (fixed the
  `/TMP/DEM.BAT` corruption from the old `name_to_83`); SYSCALL_EXEC searches
  CWD then root then /BIN; READ/WRITE bounce buffers 32→512 B (copy no longer
  freezes); PROC_PARAS 16→24 KB + zero child slot before load (fixed EDIT
  hanging on multi-line files — its near BSS overran the slot into its own far
  gap buffer). SHELL.COM one-shot mode (runs PSP command tail then exits).
  NCD: panels sync CWD before every op, new |NAME|SIZE|DATE|TIME| column
  layout, F-key bar on row 24 (inverted), cursor bar only in active pane, F5
  copy confirmation, Shift-F4 new-file, F3 enters folders, command line +
  Enter launch .COM/.BAT with screen clear. Added os-docs/NCD.md → DOCS/NCD.TXT.
  Verified all 12 in QEMU (VGA dumps + serial); `make test` 2/2.
  Updated [[ncd]], [[vfs-layer]], [[com-executor]], [[on-image-docs]].
