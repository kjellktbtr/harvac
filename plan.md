# Harvac: HarvaOS Recreated in C

## Overview

Recreate the [HarvaOS](https://github.com/kjell/..%5Charva) 16-bit real-mode x86
operating system in C, using **OpenWatcom C v2.0** (installed at `/opt/watcom/`)
as the primary toolchain. Assembly is used only where unavoidable: the
bootloader (MBR/VBR), the kernel entry point, and the `INT 0x40` syscall
dispatcher.

Source lives in `harvac/`, builds independently from the original `harva/`
while maintaining full API, ABI, and feature compatibility.

## Toolchain Decision: OpenWatcom vs. Microsoft

| Criterion | OpenWatcom v2.0 | MS C 6.0 / MASM |
|-----------|-----------------|------------------|
| Availability | Installed at `/opt/watcom/` | Not installed, requires WINE/DOSBox |
| 16-bit real-mode C | Yes (`-ms`, `-mc`, `-ml`) | Only via ancient MS C 5.x/6.x |
| Flat binary output | `system rawbin` + `format raw bin` | Requires `exe2bin` post-processing |
| Open source | Yes (Sybase Open Watcom Public License) | No |
| Active upstream | Yes (Nov 2025 build) | Abandoned since ~1993 |
| WASM assembler | Bundled, modern syntax | MASM 5.x/6.x would need WINE |
| `#pragma aux` for inline asm | Yes — clean C-level I/O access | Only `_asm {}` blocks |

**Decision: OpenWatcom** for every component that can be written in C.
NASM is still used for the bootloader (MBR/VBR) since those are small,
self-contained, and NASM is fast/available — no benefit to converting them
to WASM syntax.

## Memory Model: OpenWatcom `-ms` (Small)

The kernel runs in a single 64 KB code segment and a single 64 KB data segment
at physical address `0x10000` (segment `0x1000`). This matches the original
layout exactly.

- `-ms` (small model): all pointers are NEAR (16-bit offset within the segment)
- Code segment: `_TEXT` class `CODE`
- Data segment: `_DATA` + `CONST` + `_BSS` class `DATA`
- Stack: grows down from `0x9F000` (set up in assembly entry)
- No C runtime required — we write `kmain()` called from `entry.asm`
- No standard library used — we implement everything ourselves (freestanding)

## Flat Binary Output Strategy

The kernel must be a **flat binary** loaded at `0x1000:0x0000` (no DOS header,
no MZ/PE wrapper). OpenWatcom's linker supports this:

```
system rawbin
format raw bin
option offset=0x0000      # code segment starts at offset 0
name KERNEL.COM
```

The linker produces a raw binary image with code first, then data/BSS/stack.
`entry.asm` is assembled separately (with WASM or NASM) and linked as the
first object file so `_start` is at offset 0.

User apps use the standard `system com` (DOS .COM format):

```
system com
option start=_main
name HELLO.COM
```

For .COM apps, `cstart_t.obj` is used as the startup object (provides the
`ORG 0x100` entry point, calls `main()`).

## File Structure

```
harvac/
├── Makefile                   # Top-level build targets
├── build.py                   # Python build orchestrator (disk image, QEMU)
├── toolchain.mk               # OpenWatcom paths and flags (if Makefile used)
│
├── src/
│   ├── boot/
│   │   ├── mbr.asm            # MBR — NASM, 512-byte bootsector
│   │   └── vbr.asm            # VBR — NASM, loads KERNEL.COM from FAT16
│   │
│   ├── kernel/
│   │   ├── entry.asm          # _start: set segments/stack, call kmain (WASM)
│   │   ├── syscall.asm        # INT 0x40 dispatcher + IRQ stubs (WASM)
│   │   ├── kmain.c            # Kernel entry from asm: init all, enter shell
│   │   ├── syscalls.c         # All syscall handler implementations
│   │   ├── timer.c            # PIT init, IRQ0 handler, sleep
│   │   └── memory.c           # Bitmap heap allocator
│   │
│   ├── drivers/
│   │   ├── video.c            # VGA text mode 80x25 driver
│   │   ├── keyboard.c         # PS/2 keyboard driver (ring buffer, scancodes)
│   │   ├── serial.c           # COM1 serial driver (115200 8N1)
│   │   ├── disk.c             # INT 13h LBA disk I/O (AH=42h/43h)
│   │   └── fat16.c            # Full FAT16 driver (mount, open, read, write...)
│   │
│   ├── fs/
│   │   ├── vfs.c              # VFS layer (path resolution, dispatch)
│   │   └── mount.c            # Mount table management
│   │
│   ├── shell/
│   │   ├── shell.c            # REPL loop, I/O redirection parser
│   │   ├── builtin.c          # Built-in commands (help, ls, cd, cat, etc.)
│   │   └── exec.c             # .COM file loader and executor
│   │
│   ├── lib/
│   │   ├── strings.c          # String utilities (kprint, itoa, hex, etc.)
│   │   └── mem.c              # memset, memcpy, memmove
│   │
│   └── include/
│       ├── kernel.h           # Main kernel header, type definitions
│       ├── syscall.h          # Syscall numbers (same as constants.inc)
│       ├── errors.h           # Error codes
│       ├── video.h            # VGA driver API
│       ├── keyboard.h         # Keyboard driver API
│       ├── serial.h           # Serial driver API
│       ├── disk.h             # Disk driver API (DAP struct, read/write)
│       ├── fat16.h            # FAT16 data structures (DPB, FCB, dirent)
│       ├── vfs.h              # VFS API
│       ├── memory.h           # Memory allocator API
│       └── shell.h            # Shell API
│
├── apps/
│   ├── hello.c                # Hello world .COM program
│   ├── cat.c                  # cat utility
│   ├── ls.c                   # Directory listing
│   └── uname.c                # System info
│
├── ld/
│   ├── kernel.lbc             # WLink command file for kernel flat binary
│   └── app.lbc                # WLink command file for .COM apps
│
└── docs/
    └── plan.md                # This file
```

## Utility Functions in C, Not Macros

Drivers use `#pragma aux` (OpenWatcom's inline assembly mechanism) or small
WASM stubs for privileged operations:

```c
// Port I/O — clean C-level inline assembly
#pragma aux inb = "in al, dx" parm [dx] value [al] modify [al];
#pragma aux outb = "out dx, al" parm [dx] [al];

// Far pointer construction for VGA buffer access
#pragma aux MK_FP = parm [dx] [ax] value [dx ax];  // dx=seg, ax=off → dx:ax

// INT 0x13 disk I/O
#pragma aux int13h = "int 0x13" parm [ah] [cx] [dx] [es] [bx] value [ax] modify [ax];

// INT 0x40 syscall from user programs
#pragma aux syscall = "int 0x40" parm [ah] [al] [bx] [cx] [dx] [si] [di] \
    value [ax] modify [ax bx cx dx si di];
```

## Assembly Boundaries (Where Asm Is Required)

| Component | Reason | File(s) |
|-----------|--------|---------|
| MBR | 512-byte bootsector, specific ORG, partition table | `src/boot/mbr.asm` |
| VBR | BPB parsing, FAT16 traversal to load kernel | `src/boot/vbr.asm` |
| `_start` | Set SS:SP, DS, call `kmain` | `src/kernel/entry.asm` |
| `INT 0x40` handler | Save/restore all regs, dispatch to C `syscall_handler()` | `src/kernel/syscall.asm` |
| IRQ0/IRQ1 stubs | Save context, call C handlers, send EOI | `src/kernel/syscall.asm` |
| Far jump to .COM program | `exec` needs to push segment, push 0x0100, retf | `src/shell/exec.c` via inline asm |

The goal: as little assembly as possible. Bootloader stays NASM (it's already
correct and minimal). Everything else that can be written in C, is.

## Phased Implementation

### Phase 0: Toolchain Verification & Build Skeleton

**Goal:** A single-file "Hello World" kernel that compiles, links to a flat
binary, and boots in QEMU to prove the toolchain chain works.

- [x] Verify `wcc`, `wasm`, `wlink` are functional
- [x] Write `src/kernel/entry.asm` — sets segments, calls `kmain`, infinite loop
- [x] Write minimal `src/kernel/kmain.c` — writes "Hello" to `0xB8000` (VGA)
- [x] Write `ld/kernel.lbc` — WLink directives for `system rawbin`
- [x] Write top-level `Makefile` — compile, link, build disk image, run QEMU
- [x] Write `build.py` — create FAT16 disk image, inject MBR/VBR/kernel
- [x] Verify: QEMU boots and shows "Hello" in VGA text mode

**Key deliverables:**
- `Makefile` with targets: `all`, `run`, `qemu`, `clean`
- `build.py` that creates a bootable FAT16 image
- Working toolchain pipeline: `.c` → `wcc` → `.obj` → `wlink` → `.bin`

### Phase 1: Bootloader (Bring Over from harva/, Minimal Changes)

**Goal:** Bootable disk image with MBR + VBR that loads KERNEL.COM.

- [x] Copy `mbr.asm` and `vbr.asm` from `harva/` (minimal adaptation)
- [x] Adjust VBR to load KERNEL.COM to segment `0x2000`
- [x] Integrate into `build.py`
- [x] Verify: boots to kernel in QEMU

### Phase 2: Kernel Core — Initialization, Syscalls, Memory

**Goal:** Full kernel skeleton with `INT 0x40` dispatch and heap allocator.

- [x] `entry.asm` — full version with segment setup, stack, calls `kmain`
- [x] `syscall.asm` — `INT 0x40` handler that saves regs, calls C dispatcher
- [x] `kmain.c` — init sequence: video → serial → keyboard → syscalls
- [x] `syscalls.c` — implement the syscall table (stubs for WRITE_CHAR, CLEAR_SCREEN, SET_CURSOR)
- [x] `memory.c` — bitmap allocator (16-byte pages, ~512KB heap)
- [x] `timer.c` — PIT init at 100 Hz, `timer_ticks` counter
- [x] Test syscall: write to stdout via INT 0x40

### Phase 3: Drivers — Serial, Video, Keyboard

**Goal:** Character I/O working through the syscall interface.

- [x] `serial.c` — COM1 init (115200 8N1), putc, gets, available
- [x] `video.c` — VGA text mode 80x25 at 0xB8000, cursor, scrolling, attributes
- [x] `keyboard.c` — PS/2 polling, scancode set 1 translation, ring buffer, shift/caps
- [x] Wire syscalls: `WRITE_STDOUT`, `WRITE_CHAR`, `READ_STDIN`, `READ_CHAR`,
      `WRITE_STDERR`, `CLEAR_SCREEN`, `SET_CURSOR`, `GET_CURSOR`

### Phase 4: Disk & FAT16 Driver

**Goal:** Filesystem I/O working through the syscall interface.

- [x] `disk.c` — INT 13h AH=42h/43h LBA read/write with retry logic
- [x] `fat16.c` — Mount, BPB parsing, FAT sector cache, cluster chain walking
- [x] `fat16.c` — open, close, read, seek, stat (dirent->file_size)
- [x] `fat16.c` — write, create, delete
- [x] `fat16.c` — opendir, readdir, closedir
- [x] `fat16.c` — mkdir, rmdir
- [x] Wire syscalls: `OPEN`, `CLOSE`, `READ`, `SEEK`, `STAT`
- [x] Wire syscalls: `WRITE`, `CREATE`, `DELETE`
- [x] Wire syscalls: `OPENDIR`, `READDIR`, `CLOSEDIR`
- [x] Wire syscalls: `MKDIR`, `RMDIR`, `CHDIR`, `GETCWD`

### Phase 5: VFS & Mount

**Goal:** Filesystem abstraction layer.

- [x] `mount.c` — mount table (max 8 entries)
- [x] `vfs.c` — path resolution, cwd tracking, dispatch to FAT16
- [x] Wire syscalls: `MOUNT`, `UNMOUNT`, `LIST_MOUNTS`

### Phase 6: Shell — REPL, Builtins, .COM Executor

**Goal:** Interactive shell with the same feature set as harva/.

- [x] `shell.c` — REPL loop, line editor, I/O redirection parser (`>`, `>>`, `<`, `|`)
- [x] `builtin.c` — help, dir/ls, cd, echo, cls, ver, cat, uname, exit
- [x] `exec.c` — load .COM file from FAT16, execute via far call, return to shell
- [x] Wire syscalls: `EXEC`, `EXIT`, `GET_PID`

### Phase 7: User Apps

**Goal:** Compile .COM programs with OpenWatcom's `system com`.

- [x] `hello.c` — prints "Hello from Harvac!"
- [x] `cat.c` — read file, write to stdout
- [x] `ls.c` — opendir/readdir loop, formatted output
- [x] `uname.c` — system version info via `GET_VERSION`

### Phase 8: Polish

- [ ] Editor app (optional, large effort — port from assembly or skip)
- [x] Test suite (automated QEMU tests via `make test`)
- [x] Documentation (wiki pages in `docs/`) — all pages created
- [x] `make check` target for syntax checking
- [ ] Performance comparison with original harva/ (code size, boot time)

## OpenWatcom Build Details

### Environment Variables

```makefile
WATCOM ?= /opt/watcom
CC      = $(WATCOM)/binl/wcc
ASM     = $(WATCOM)/binl/wasm
LD      = $(WATCOM)/binl/wlink
LIB     = $(WATCOM)/binl/wlib
```

### Compiler Flags

| Flag | Purpose |
|------|---------|
| `-ms` | Small memory model (near code, near data) |
| `-os` | Optimize for size |
| `-s` | Remove stack overflow checks |
| `-zl` | Suppress default library search records |
| `-d0` | No debug info |
| `-wx` | Warnings as errors |
| `-oh` | Enable optimizations |
| `-bt=dos` | Target DOS (sets calling convention) |
| `-i=src/include` | Include path |

### Kernel Linker Script (`ld/kernel.lbc`)

```
system rawbin
format raw bin
option offset=0x0000
option quiet
name KERNEL.COM
file entry.obj
file kmain.obj
file syscalls.obj
file memory.obj
file timer.obj
file video.obj
file keyboard.obj
file serial.obj
file disk.obj
file fat16.obj
file vfs.obj
file mount.obj
file shell.obj
file builtin.obj
file exec.obj
file strings.obj
file mem.obj
start __entry          # entry.asm exports this label
```

### App Linker Script (`ld/app.lbc`)

```
system com
option quiet
name HELLO.COM
file hello.obj
start _main
```

## Port I/O in C (Key Technique)

```c
/* port_io.h — OpenWatcom #pragma aux I/O helpers */

/* 8-bit port I/O */
#pragma aux port_in_b = "in al, dx" parm [dx] value [al] modify [al];
unsigned char port_in_b(unsigned short port);

#pragma aux port_out_b = "out dx, al" parm [dx] [al];
void port_out_b(unsigned short port, unsigned char val);

/* 16-bit port I/O */
#pragma aux port_in_w = "in ax, dx" parm [dx] value [ax] modify [ax];
unsigned short port_in_w(unsigned short port);

#pragma aux port_out_w = "out dx, ax" parm [dx] [ax];
void port_out_w(unsigned short port, unsigned short val);

/* Far pointer read/write */
#pragma aux read_far_b = "push ds" "mov ds, dx" "mov al, [bx]" "pop ds" \
    parm [dx] [bx] value [al] modify [al bx ds];
unsigned char read_far_b(unsigned short seg, unsigned short off);

#pragma aux write_far_b = "push ds" "mov ds, dx" "mov [bx], al" "pop ds" \
    parm [dx] [bx] [al] modify [bx ds];
void write_far_b(unsigned short seg, unsigned short off, unsigned char val);

/* INT 13h disk read */
#pragma aux disk_read_int13h = \
    "mov ah, 42h" \
    "int 13h" \
    "jc failure" \
    "xor ax, ax" \
    "failure:" \
    parm [dx] [si] value [ax] modify [ax bx cx dx si di es];
unsigned short disk_read_int13h(unsigned char drive, void far *dap);
```

## FAQ & Design Decisions

### Q: Why not NASM for assembly parts in kernel?

A: We use WASM (`wasm`) for kernel assembly stubs because:
- It's bundled with OpenWatcom — one less dependency
- WASM object files (`.obj`) link directly with `wlink` without format conversion
- NASM would require `-f obj` output and careful segment directive setup
- The kernel stubs are tiny (<200 lines total)

Moot point: the bootloader (MBR/VBR) stays NASM because they need `-f bin`
output and NASM is already available.

### Q: Why `_Interrupt` keyword not used?

A: OpenWatcom's `_Interrupt` adds IRET and pushes all registers automatically.
For the `INT 0x40` syscall handler, we need fine control over register saving
(because AH is the syscall number, and return values go in AX). We write the
stub in assembly and call a C function from it.

### Q: What about the editor?

A: The full-screen editor (`editor.asm`) is commented out by default in the
original. Port it last, and only if it provides value. It's a 2000+ line
assembly file that would be significant effort to port.

### Q: Why not `-mc` (compact model) for kernel?

A: Small model (`-ms`) matches the current kernel layout (everything in one
code segment and one data segment). Compact model adds `far` pointers for data
without any benefit — our kernel data fits easily within 64 KB.

### Q: How do .COM user apps make syscalls?

A: They call the same `syscall()` function via `#pragma aux`, which emits
`INT 0x40`. The kernel is installed as the handler for interrupt vector 0x40.
Apps are compiled with `system com` format which gives them `ORG 0x0100` and
the standard Watcom startup that calls `main()`.

### Q: What about the VGA buffer at physical `0xB8000`?

A: We need a far pointer to access it. With `-ms`, all pointers are 16-bit
near. We either:
1. Use `#pragma aux` to write a helper that sets `ES:BX` and does `mov [es:bx], al`
2. Or switch the video functions to a separate small WASM module

Option 1 is preferred — it keeps the logic in C and only the raw access in asm.

## Dependency Graph

```
                     ┌─────────────┐
                     │  mbr.asm    │  ← NASM, standalone
                     └──────┬──────┘
                            │ loads
                     ┌──────▼──────┐
                     │  vbr.asm    │  ← NASM, standalone
                     └──────┬──────┘
                            │ loads at 0x1000:0x0000
                     ┌──────▼──────┐
                     │ entry.asm   │  ← WASM, sets up C environment
                     └──────┬──────┘
                            │ calls
                     ┌──────▼──────────────────────────────┐
                     │  kmain()                            │
                     │  ┌────────────────────────────────┐ │
                     │  │ init: video → serial → keyboard│ │
                     │  │       → timer → memory → shell │ │
                     │  └────────────────────────────────┘ │
                     └─────────────────────────────────────┘
                                   │
                   ┌───────────────┼───────────────────────┐
                   │               │                       │
            ┌──────▼──────┐ ┌──────▼──────┐     ┌─────────▼──────────┐
            │ syscalls.c  │ │ drivers/    │     │ shell/              │
            │ memory.c    │ │ video.c     │     │ shell.c             │
            │ timer.c     │ │ keyboard.c  │     │ builtin.c           │
            └──────────────┘ │ serial.c   │     │ exec.c              │
                             │ disk.c     │     └─────────────────────┘
                             │ fat16.c    │
                             └────────────┘
                                   │
                            ┌──────▼──────┐
                            │ fs/         │
                            │ vfs.c       │
                            │ mount.c     │
                            └─────────────┘
```

## Build Targets

```bash
make              # build disk image (harva.img)
make run          # build + QEMU with serial output
make qemu         # QEMU only (no rebuild)
make clean        # remove build artifacts

# Direct invocation
python3 build.py
python3 build.py --run
python3 build.py --clean
```

## Success Criteria

1. `make run` boots to a working shell prompt in QEMU
2. All built-in commands work: `help`, `ls`, `cd`, `echo`, `cls`, `ver`, `cat`, `uname`, `exit`
3. I/O redirection works: `>`, `>>`, `<`, `|`
4. .COM programs execute from the shell
5. FAT16 read/write works (create file, read it back, delete it)
6. Same syscall ABI as harva/ (`INT 0x40`, AH=number, same error codes)
7. Kernel + apps fit within ~48 KB (same as original), giving room for growth
8. Consistent coding style: PEP 8 for Python, K&R for C, snake_case for C identifiers

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| OpenWatcom startup code expects DOS environment | Use `system rawbin` with custom entry point, no C runtime |
| Bitmap allocator pointer manipulation in C | Use `MK_FP`/`#pragma aux` or `unsigned long` arithmetic |
| Disk INT 13h requires ES:BX far buffer | Pass segment explicitly, use assembly trampoline |
| .COM app entry (ORG 0x100) vs OpenWatcom startup | Use `system com` with `cstart_t.obj` — standard and tested |
| FAT16 C driver could exceed 64KB code segment | Each FAT16 function is small; unlikely, but medium model (-mm) is a fallback |