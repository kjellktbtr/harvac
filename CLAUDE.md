# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Project Is

Harvac is a 16-bit real-mode x86 OS written in freestanding C (OpenWatcom), targeting IBM PC-compatible hardware as old as an 8088 with 256 KB RAM. It boots from a FAT16 disk image, has a shell with batch scripting, a full-screen text editor, and a serial file-transfer tool with FUSE mounting on the host side. The bootloader (MBR/VBR) stays in NASM assembly; everything else is OpenWatcom C with `#pragma aux` inline assembly for port I/O and far memory access.

## Build & Run

```bash
make              # build everything into harvac.img (4 MB FAT16 disk image)
make run          # build + launch QEMU (serial stdio, no display)
make qemu         # launch QEMU without rebuild
make display      # QEMU with VGA display + PS/2 keyboard
make xfer         # QEMU with COM1 on Unix socket for serial file transfer
make check        # compile all sources without linking (syntax/lint check)
make test         # automated QEMU smoke tests
make clean        # remove build artifacts and disk image
```

Direct invocation: `python3 build.py [--run | --qemu-only | --display | --xfer-run | --clean | --check | --test]`.

### Toolchain Requirements

- **OpenWatcom v2.0** at `/opt/watcom` (wcc, wasm, wlink)
- **NASM** — bootloader and XFER agent
- **Python >= 3.12** — build orchestrator (`build.py`)
- **QEMU** (qemu-system-i386) — for testing

## Architecture Overview

### Boot Chain

BIOS loads MBR (`src/boot/mbr.asm`, NASM) from sector 0 -> MBR loads VBR (`src/boot/vbr.asm`, NASM) from LBA 63 -> VBR reads FAT16 root directory, finds `KERNEL.COM`, loads it to `0x20000` -> kernel entry (`src/kernel/entry.asm`, WASM) sets up segments and stack -> calls `kmain()` in C.

### Memory Layout

| Range | Usage |
|-------|-------|
| `0x07C00` | MBR (loaded by BIOS) |
| `0x09000` | VBR |
| `0x20000` – `0x2FFFF` | Kernel segment (KERNEL.COM + stack) |
| `0x30000` – ... | Shell and child programs (.COM, 16 KB slots) |
| `0xB8000` – `0xB8FFF` | VGA text mode buffer |

### Kernel Init Order (kmain.c)

`serial -> video -> keyboard -> timer -> memory -> mount -> vfs -> syscall_init -> enable_interrupts -> boot_shell`

Each stage writes a debug character to VGA (A=entered kmain, B=serial OK, ..., I=syscall OK) so hangs are diagnosable even before the console is ready.

### Syscall API

`INT 0x40` with AH = syscall number. Handler is installed in `syscall.asm` (WASM), which saves all registers and calls `syscall_handler_c()` in `syscalls.c`. User programs invoke syscalls via the `syscall_int40()` pragma in `port_io.h`.

### Source Organization

```
src/
  boot/       mbr.asm, vbr.asm          — NASM, do not modify
  kernel/     entry.asm, rt.asm, syscall.asm  — WASM stubs
              kmain.c, syscalls.c, memory.c   — C
  drivers/    video.c, keyboard.c, serial.c, timer.c, disk.c, fat16.c
  fs/         vfs.c, mount.c
  shell/      shell.c, builtin.c, exec.c, exec_stub.asm
  lib/        strings.c
  include/    types.h, constants.h, port_io.h, kernel.h, syscall.h, shell.h
              drivers/*.h, fs/*.h, lib/*.h
apps/         hello.c, cat.c, ls.c, uname.c, shell.c  — .COM user programs
              medit/                              — EDIT.COM (multi-file)
              xfer.asm                            — NASM flat binary
ld/           kernel.lbc, app.lbc               — WLink command files
build.py      Python build orchestrator         — compiles, links, assembles, builds FAT16 image
```

### Key C Constraints

- **Freestanding C** — no standard library. Everything (string ops, memory alloc, I/O) is hand-implemented.
- **Small memory model** (`-ms`) — all pointers are 16-bit near. Far memory access requires `read_far_b()` / `write_far_b()` helpers from `port_io.h`.
- **K&R style**, snake_case identifiers, `#pragma aux` for inline assembly.
- Use `unsigned char` / `unsigned short` for port I/O and register-sized values.
- Kernel functions are prefixed by subsystem (e.g. `vga_putchar`, `kbd_read`, `fat16_open`).

## Files to Avoid Modifying

- `src/boot/mbr.asm` and `src/boot/vbr.asm` — NASM bootloader, copied from the original harva/ project
- `build.py` — Python build orchestrator; add features carefully, don't break the FAT16 image writer

## Wiki (Mandatory)

After every non-trivial change, update `docs/wiki/`:
- Create or enrich the relevant wiki page(s)
- Update `docs/wiki/index.md`
- Append one line to `docs/wiki/log.md`

See `docs/CLAUDE.md` for full wiki workflow (page types, frontmatter, ingest/query/lint).

## Other Documentation

- `plan.md` — phased implementation plan with checkboxes
- `os-docs/` — end-user docs (also embedded on the disk image as DOCS/*.TXT)
- `README.md` — project overview for external readers
