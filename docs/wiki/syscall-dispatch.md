---
title: Syscall Dispatch
type: code-map
sources:
  - src/kernel/syscall.asm
  - src/kernel/syscalls.c
  - src/include/syscall.h
related:
  - "[[fat16-driver]]"
  - "[[disk-driver]]"
  - "[[vfs-layer]]"
  - "[[plan]]"
created: 2026-07-05
updated: 2026-07-11
confidence: high
---

# Syscall Dispatch

Harvac uses `INT 0x40` for all system calls, matching the original harva ABI.
The handler at `src/kernel/syscall.asm` saves all registers, calls the C
dispatcher (`syscall_handler_c`), and restores registers before returning.

## Calling Convention

```
AH = syscall number
AL = sub-function / flags
BX, CX, DX, SI, DI = arguments (per-syscall)
AX = return value (0 = success, nonzero = error)
```

## Cross-Segment Handling

User .COM apps run at segment 0x4000 (COM_SEGMENT) while the kernel lives at
0x2000 (KERNEL_SEGMENT). The `INT 0x40` handler saves the caller's DS before
switching to kernel DS, and passes it as the 7th argument to
`syscall_handler_c()`.

All string and buffer pointers from user space must be accessed via
`read_far_b(caller_ds, offset)` and `write_far_b(caller_ds, offset, value)`.
Near pointer casts would read from kernel memory instead.

### Critical: SS != DS during the handler

When a user .COM calls `INT 0x40`, `_int40_entry_` does NOT change SS. It only
sets DS = ES = KERNEL_SEGMENT. So throughout `syscall_handler_c` and all
callees: **SS = COM_SEGMENT, DS = KERNEL_SEGMENT**.

OpenWatcom small model near pointers are DS-relative. A local auto variable on
the stack (SS-relative via BP) has a near address that is its SS-offset. When
that address is passed to a callee that writes to it (`*ptr = value`), the write
uses DS and hits KERNEL_SEGMENT:offset instead of COM_SEGMENT:offset. The
caller's stack variable reads from SS = COM_SEGMENT and sees unwritten garbage.

**The rule:** Any local variable whose near address (`&var`) is passed to a
callee that writes through the pointer must be declared `static`. Static
variables live in the data segment (DS = KERNEL_SEGMENT), making caller writes
and callee reads target the same physical memory.

This also applies to large local arrays used as buffers with near-pointer reads
(e.g., `fat16_read(file, buf, 512)` — `buf` must be static).

See [[fat16-driver]] for the full list of variables that were fixed.

As of 2026-07-11, all handler locals in `syscall_handler_c()` that are passed
to callees via near pointers are declared `static`. This covers the directory
and path handlers (OPENDIR, READDIR, CLOSEDIR, STAT, MKDIR, RMDIR, CHDIR,
GETCWD, RENAME, MOUNT, UNMOUNT) in addition to the file I/O handlers (OPEN,
READ, WRITE, EXEC). The SS != DS rule applies to every handler equally —
handler-local auto variables whose near address is passed to a callee are
forbidden.

### Register Convention Fix (syscall.asm)

OpenWatcom 16-bit `__watcall` passes function parameters as:
- 1st param → AX
- 2nd param → DX (NOT BX)
- 3rd param → BX
- 4th param → CX

The `_int40_entry_` stub in `src/kernel/syscall.asm` must map the original
caller registers to these positions:

```asm
mov ax, word ptr [bp+2]  ; 1st param = caller AX (syscall number)
mov dx, word ptr [bp-2]  ; 2nd param = caller BX (filename/buffer offset)
mov bx, word ptr [bp-4]  ; 3rd param = caller CX
mov cx, word ptr [bp-6]  ; 4th param = caller DX
```

The first time this was coded as `mov bx,[bp-2]` (BX→BX instead of BX→DX),
causing the filename offset to land in the wrong C parameter — always reading
0 instead of the pointer — and making SYSCALL_EXEC always fail.

## Implemented Syscalls

### Console I/O (0x00-0x0F)

| # | Name | Args | Returns |
|---|------|------|---------|
| 0x00 | `WRITE_STDOUT` | `SI` = string offset | - |
| 0x01 | `WRITE_CHAR` | `AL` = char | - |
| 0x02 | `READ_STDIN` | `BX` = buffer offset, `CX` = maxlen | count |
| 0x03 | `READ_CHAR` | - | char |
| 0x05 | `CLEAR_SCREEN` | - | - |
| 0x06 | `SET_CURSOR` | `BL` = row, `BH` = col | - |
| 0x08 | `SET_STDOUT` | `BX` = file handle (0xFFFF = serial) | - |
| 0x0A | `WRITE_VGA` | `SI` = string offset | - |
| 0x0B | `KEY_AVAILABLE` | - | nonzero if key event buffered (non-blocking; keyboard ring buffer only, never reads the UART - safe for apps that own the serial port, e.g. XFER) |
| 0x0C | `SET_STDIN` | `BX` = file handle (0xFFFF = keyboard) | - |

#### Stdout Redirect (`SYSCALL_SET_STDOUT`)

`g_redirect` (file-static `uint16_t` in `syscalls.c:29`) controls where all
console output goes. `0xFFFF` means normal serial. Any valid open-file handle
(0-15) redirects both `WRITE_STDOUT` and `WRITE_CHAR` through `out_byte(c)`,
which calls `fat16_write` instead of `serial_putchar`.

`g_ob` (file-static `uint8_t`) holds the byte passed to `fat16_write`. It must
be static because `fat16_write` takes a near pointer; stack variables live at
`SS=COM_SEGMENT` but `fat16_write` dereferences via `DS=KERNEL_SEGMENT`. See
[[syscall-dispatch#Critical: SS != DS during the handler]].

#### Stdin Redirect (`SYSCALL_SET_STDIN`)

`g_stdin` (file-static `uint16_t` in `syscalls.c`) mirrors `g_redirect`.
`0xFFFF` means keyboard; any valid open-file handle redirects
`SYSCALL_READ_STDIN` to read raw bytes from that file.

When `g_stdin` is active, `SYSCALL_READ_STDIN` reads up to `DI` bytes from the
file into `static uint8_t tmp_in[512]` and returns the byte count. Count = 0
means EOF. Interactive EOF from the keyboard sends Ctrl-D (0x04).

`syscall_finalize_stdin()` (declared in `src/include/syscall.h`) is called by
`boot_shell()` on every exec re-entry to reset `g_stdin = 0xFFFF` and close
any open handle. See [[shell-commands#Pipes]].

#### VGA-only Write (`SYSCALL_WRITE_VGA`)

`SYSCALL_WRITE_VGA` (0x0A) writes a NUL-terminated string from `caller_ds:SI`
to the VGA screen via `video_tty_putchar` only. Unlike `WRITE_STDOUT`, it does
not call `serial_putchar` and does not consult `g_redirect`.

This is used by `XFER.COM` to display its banner and host `T_MSG`/`T_RAW` text
without corrupting the binary serial protocol stream. See [[xfer]].

**Finalization on exec re-entry:** A successful `SYSCALL_EXEC` far-jumps to a
`.COM` file and never returns to the shell. When the `.COM` returns (retf) it
lands in `exec_reentry` -> `boot_shell`. `boot_shell()` (in
`src/shell/shell.c:27`) calls `syscall_finalize_redirect()` before doing
anything else. That function marks the redirect file slot as unused and resets
`g_redirect = 0xFFFF`.

This means external programs (ls, cat, hello) automatically have their output
captured without any change to those programs, as long as the shell set the
redirect before exec'ing them. The kernel closes the file on re-entry so
the file is properly flushed and the directory entry size is updated.

### File I/O (0x10-0x1F)

| # | Name | Args | Returns |
|---|------|------|---------|
| 0x10 | `OPEN` | `DS:BX` = filename, `AL` = flags | file handle (or error) |
| 0x11 | `CLOSE` | `BX` = handle | - |
| 0x12 | `READ` | `BX` = handle, `CX` = buffer, `DX` = count | bytes read |
| 0x13 | `WRITE` | `BX` = handle, `CX` = buffer, `DX` = count | bytes written |
| 0x14 | `SEEK` | `BX` = handle, `CX:DX` = position | - |
| 0x15 | `CREATE` | `DS:BX` = filename, `AL` = flags | file handle |
| 0x16 | `DELETE` | `DS:BX` = filename | - |
| 0x17 | `STAT` | `DS:BX` = name, `CX` = dirent ptr | - |
| 0x18 | `RENAME` | `SI` = old-name offset, `DI` = new-name offset | 0 = OK, error otherwise |
| 0x1A | `UTIME` | `SI` = path offset, `CX` = FAT16 date, `DX` = FAT16 time | 0 = OK, error otherwise |

### Directory (0x20-0x2F)

| # | Name | Args | Returns |
|---|------|------|---------|
| 0x20 | `OPENDIR` | `CX` = dir_t ptr | - |
| 0x21 | `READDIR` | `CX` = dir_t ptr, `DX` = dirent ptr | 0 = OK, ERR_NOT_FOUND at end |
| 0x22 | `CLOSEDIR` | `CX` = dir_t ptr | - |
| 0x23 | `MKDIR` | `DS:BX` = name | 0 = OK, error otherwise |
| 0x24 | `RMDIR` | `DS:BX` = name | 0 = OK, error otherwise |
| 0x25 | `CHDIR` | `DS:BX` = path | 0 = OK |
| 0x26 | `GETCWD` | `CX` = buffer, `DX` = buflen | - |

### Filesystem (0x30-0x3F)

| # | Name | Args | Returns |
|---|------|------|---------|
| 0x30 | `MOUNT` | `DS:BX` = mountpoint, `CX` = drive, `DX:SI` = LBA | 0 = OK |
| 0x31 | `UNMOUNT` | `DS:BX` = mountpoint | 0 = OK |
| 0x32 | `LIST_MOUNTS` | - | - |
| 0x33 | `STATFS` | `CX` = ptr to 8-byte `statfs_t` buffer | 0 = OK |

#### `SYSCALL_STATFS` (0x33)

Returns filesystem statistics by scanning the FAT for free clusters. The
8-byte `statfs_t` layout:

```
[0-1]  total_clusters     (uint16_t)
[2-3]  free_clusters      (uint16_t, computed by scanning FAT for 0x0000 entries)
[4]    sectors_per_cluster (uint8_t)
[5]    pad                (0)
[6-7]  bytes_per_sector   (uint16_t, always 512)
```

The kernel passes `CX` (the buffer near-pointer) from caller DS. The handler
fills the buffer using `write_far_b`/`write_far_w` to write into caller DS.
This avoids the SS!=DS trap — no stack-allocated receive buffer needed.

Used by `apps/df.c` to report total/used/free KB. Because `bytes_per_sector`
is always 512 on this platform, `df.c` computes `KB = (clusters * spc) >> 1`
to stay in 16-bit arithmetic (no `__U4M` runtime needed).

### Process (0x40-0x4F)

| # | Name | Args | Returns |
|---|------|------|---------|
| 0x40 | `EXEC` | `DS:BX` = .COM filename | - |
| 0x41 | `EXIT` | - | - |
| 0x42 | `GET_PID` | - | PID |

### Memory (0x50-0x5F)

| # | Name | Args | Returns |
|---|------|------|---------|
| 0x50 | `ALLOC` | - | - |
| 0x51 | `FREE` | - | - |
| 0x52 | `MEM_INFO` | `CX` = ptr to 4-byte buffer | 0 = OK |

#### `SYSCALL_MEM_INFO` (0x52)

Returns total and used RAM in paragraphs. Buffer layout:

```
[0-1]  total_paras  (uint16_t) — from BDA 0x0040:0x0013 (KB) × 64
[2-3]  used_paras   (uint16_t) — from get_next_seg() (next allocatable segment)
```

`get_next_seg()` returns the next free segment after all currently loaded
`.COM` processes. `apps/free.c` converts to KB via `paras >> 6` (16 bytes/para,
1024 bytes/KB → divide by 64).

### Misc (0x80-0x8F)

| # | Name | Args | Returns |
|---|------|------|---------|
| 0x80 | `REBOOT` | - | - |
| 0x81 | `HALT` | - | - |
| 0x82 | `GET_VERSION` | `DS:BX` = 4-byte buffer | - |

## Open File Table

`syscalls.c` maintains a static array of 16 `open_file_entry_t` slots. Each
slot tracks whether it's in use and holds a `fat16_file_t`. On `SYSCALL_OPEN`,
the slot index is returned as the file handle. `SYSCALL_CLOSE` marks the slot
free.

Since 2026-07-11 `SYSCALL_OPEN` is **CWD-aware**: the `cwd_dir_cluster()`
helper resolves the VFS CWD to a directory cluster (same one-level resolution
`SYSCALL_OPENDIR` uses) and opens via `fat16_open_in_dir`; when the CWD is `/`
it falls back to `fat16_open` on the root. This is what makes
`cd DOCS` + `cat MANUAL.TXT` (and XFER GET in a subdirectory) work.
`SYSCALL_CREATE`/`SYSCALL_DELETE` still operate on the root directory only.

**Case handling (2026-07-11):** the CWD-to-8.3 copy loops in
`cwd_dir_cluster()` and the `SYSCALL_OPENDIR` block upper-case each character
before `fat16_find` (FAT names are stored upper-case). Originally they copied
as-is, so a lowercase `cd docs` updated the prompt (vfs_chdir checks
case-insensitively) but `ls` silently fell back to the root listing when the
lookup failed. `vfs_chdir` now also stores the CWD upper-cased (see
[[vfs-layer]]), so the prompt shows `/DOCS` regardless of typed case.

## Mount

`syscall_init()` in `syscalls.c:52-63` installs the `INT 0x40` handler in the
IVT at `0x0000:0x0100` and mounts the root FAT16 partition at LBA 63 via
`mount_init()`. Additional partitions can be mounted at runtime via
`SYSCALL_MOUNT`.

## Key Files

- `src/kernel/syscall.asm` - Assembly stub (save/restore regs, call C)
- `src/kernel/syscalls.c` - C dispatcher and all handler implementations
- `src/include/syscall.h` - Syscall number constants
- `src/include/constants.h` - Error codes, flags, and other constants