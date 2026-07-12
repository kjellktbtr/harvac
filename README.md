# Harvac

**HarvaC** is a 16-bit real-mode x86 operating system written primarily in C
(OpenWatcom), with assembly only where unavoidable. It boots from a FAT16
hard-disk image, has a shell with pipes and batch scripting, a full-screen
text editor, a dual-pane file manager, and a serial file-transfer tool with
FUSE mounting on the host side.

## Why this exists

HarvaC is a **research project in agentic AI**: the operating system is
designed and implemented by an AI coding agent (Claude), with the author
directing the work and testing the results. The question being explored is
how far an agent can get building a real, working OS on its own — not a toy
that only runs in an emulator.

To keep the answer honest, the reference target is deliberately unforgiving:
an **IBM 5155 Portable PC** — 8088 CPU at **4.77 MHz**, **256 KB RAM**, CGA —
which the author owns and runs the system on. A machine like that is a blunt
judge of code quality: wasteful CPU cycles and bloated memory use show up
immediately. Everything is freestanding C (no standard library), `cpu 8086`
assembly, and small-model 64 KB segments.

## Features

- **Boot chain:** MBR → FAT16 VBR → `KERNEL.COM`, all fitting classic BIOS
  INT 13h CHS reads
- **Kernel:** INT 0x40 syscall API (file I/O, directories, exec, console,
  timer), VGA text + serial console, PS/2 keyboard (IRQ1)
- **Filesystem:** FAT16 read/write with subdirectories, 8.3 names, VFS with
  CWD and mount table
- **POSIX-like libc** (`lib/posix/`): open, read, write, lseek, dirent, stat,
  stdio, stdlib, errno, fnmatch — a clean API layer for user programs
- **HDK TUI library** (`lib/hdk/`): video, keyboard, dialog, far memory, and
  formatted output — powers NCD and EDIT
- **Shell (`SHELL.COM`):** user-space shell with pipes (`|`), output
  redirection (`>`), built-in commands, `.BAT` batch scripts, and external
  `.COM` programs loaded from `BIN/`
- **Command suite:** `cp`, `mv`, `rm`, `mkdir`, `rmdir`, `cat`, `ls`,
  `grep`, `head`, `tail`, `sort`, `cut`, `less`, `free`, `df`, `du`, `uname`
- **NCD:** dual-pane file manager with navigation, viewing, copy/move/delete
  using far-segment memory allocation for large file operations
- **EDIT:** full-screen text editor (a port of the MEDIT DOS editor) with
  menus, clipboard, search/replace — CGA-snow aware
- **XFER:** serial file-transfer agent; the host side (`tools/serial-xfer/`)
  uploads/downloads whole trees and can even **mount the HarvaC disk** over
  a null-modem cable via FUSE
- **On-image documentation:** `DOCS/MANUAL.TXT` and per-program docs,
  readable with `cat`

## Getting started

A prebuilt disk image (`harvac.img`, 4 MB) ships in the repository, along
with prebuilt `.COM` binaries in `build/` — you don't need the toolchain
just to try it.

**In QEMU:**

```bash
qemu-system-i386 -m 4 -drive file=harvac.img,format=raw
# or with the serial console on stdio:
make qemu
```

At the `/>` prompt, try `help`, `ls`, `cat README.TXT`, `ls | grep -i txt`,
`ncd`, or `edit HELLO.TXT`.

**On real hardware:** write `harvac.img` raw to a small hard disk / CF card
(e.g. XT-IDE) and boot an 8088-class or later PC. Use `XFER` plus a
null-modem serial cable to move files to and from a modern machine — see
`os-docs/XFER.md` for cabling and usage.

## Building from source

Prerequisites:

- **OpenWatcom v2.0** (C compiler, assembler, linker) installed at `/opt/watcom`
- **NASM** — bootloader and XFER agent
- **Python ≥ 3.12** — build orchestrator
- **QEMU** — for testing

```bash
make              # build everything into harvac.img
make run          # build and launch QEMU
make qemu         # launch QEMU without rebuilding
make clean        # remove build artifacts
make check        # compile all sources without linking (syntax/lint check)
make test         # automated QEMU smoke tests
```

Or directly: `python3 build.py [--run | --qemu-only | --display | --xfer-run | --clean | --check | --test]`.

## How it boots

1. **BIOS** loads the MBR (`src/boot/mbr.asm`) from sector 0 to `0x7C00`
2. **MBR** scans the partition table, loads the VBR from LBA 63 to `0x9000`,
   and far-jumps to it
3. **VBR** (`src/boot/vbr.asm`) reads the FAT16 root directory, finds
   `KERNEL.COM`, loads it to `0x20000`, and far-jumps to `0x2000:0x0000`
4. **Kernel entry** (`src/kernel/entry.asm`) sets up segments and stack,
   then calls `kmain()`
5. **kmain** initializes drivers, filesystem and syscalls, then starts
   `SHELL.COM` (falling back to a kernel built-in shell)

## Memory layout

| Range | Usage |
|-------|-------|
| `0x00000` – `0x004FF` | IVT, BIOS data area |
| `0x07C00` – `0x07DFF` | MBR (loaded by BIOS) |
| `0x09000` – `0x090FF` | VBR |
| `0x20000` – `0x2FFFF` | Kernel segment (`KERNEL.COM` + kernel stack) |
| `0x30000` – …         | Shell and child programs (`.COM`, 16 KB slots) |
| `0xB8000` – `0xB8FFF` | VGA text mode buffer |

## Project layout

```
harvac/
├── Makefile              # Convenience targets
├── build.py              # Python build orchestrator (also writes the FAT16 image)
├── harvac.img            # Prebuilt bootable disk image (4 MB)
├── src/
│   ├── boot/             # MBR + VBR (NASM)
│   ├── kernel/           # entry, kmain, syscalls, memory (OpenWatcom C)
│   ├── drivers/          # video, serial, keyboard, disk, fat16, timer
│   ├── fs/               # VFS + mount table
│   ├── shell/            # kernel fallback shell + COM executor
│   ├── lib/              # freestanding string routines
│   └── include/          # headers (syscall numbers, port I/O pragmas)
├── apps/                 # user programs: shell, cat, grep, ls, ncd/, medit/, xfer.asm
├── lib/
│   ├── posix/            # POSIX-like libc (open, read, write, dirent, stat, stdio, stdlib, etc.)
│   ├── hdk/              # HDK TUI library (video, keyboard, dialog, far memory, formatting)
│   └── include/          # shared library headers (args.h, fcntl.h, unistd.h, etc.)
├── build/                # prebuilt binaries (KERNEL.COM, SHELL.COM, BIN apps)
├── os-docs/              # end-user docs (also embedded on the image as DOCS/)
├── tools/serial-xfer/    # host-side transfer/mount tools (git submodule)
├── docs/wiki/            # developer wiki (architecture, drivers, logs)
└── ld/                   # WLink command files
```

## Documentation

- `os-docs/` — user-facing docs (`MANUAL.md`, `EDIT.md`, `XFER.md`); the
  same files live on the disk image under `DOCS/`
- `docs/wiki/` — developer wiki: architecture, boot chain, FAT16 driver,
  syscall dispatch, the editor port, and an append-only work log
- `plan.md` — the original phased implementation plan

## License

GPL-2.0-only. See [LICENSE](LICENSE).
© 2026 Kjell Kristian Grane Torgersen.
