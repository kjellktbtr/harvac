---
title: Harvac Wiki Index
type: concept
sources:
  - All source files under src/
related: []
created: 2025-07-05
updated: 2026-07-11
confidence: high
---

# Harvac Wiki - Index

Master catalog of all wiki pages for the Harvac project (HarvaOS ported to C).

## Architecture & Design

| Page | Type | Description |
|------|------|-------------|
| [[plan]] | decision | Full implementation plan (plan.md at repo root) |
| [[toolchain]] | entity | OpenWatcom C toolchain setup and usage |
| [[architecture]] | code-map | System architecture and memory layout |
| [[boot-chain]] | code-map | MBR/VBR boot chain, debug markers, geometry handling, gdb recipes |

## Implementation Status

| Page | Phase | Status |
|------|-------|--------|
| [[bootloader]] | Phase 0 | Complete - MBR, VBR, loads KERNEL.COM from FAT16 |
| [[kernel-core]] | Phase 0 | Complete - entry.asm sets CS/DS/SS:SP, calls kmain() in C |
| [[video-driver]] | Phase 3 | Complete - VGA text mode 80x25, cursor, scrolling, attributes |
| [[keyboard-driver]] | Phase 3 | Complete - IRQ1-driven (XT+AT compatible), scancode set 1, ring buffer, shift/caps |
| [[serial-driver]] | Phase 3 | Complete - COM1 115200 8N1 driver |
| [[syscall-dispatch]] | Phase 2 | Complete - INT 0x40 handler, all syscalls wired |
| [[disk-driver]] | Phase 4 | Complete - INT 13h LBA read/write with retry logic |
| [[fat16-driver]] | Phase 4 | Complete - mount, open, read, write, create, delete, seek, stat, opendir/readdir/closedir, mkdir/rmdir |
| [[vfs-layer]] | Phase 5 | Complete - VFS path resolution, mount table, CWD, ls |
| [[shell]] | Phase 6 | Complete - REPL, builtins, .COM executor |
| [[com-executor]] | Phase 6 | Complete - .COM file load, far jump, syscall caller-DS fix |
| [[user-apps]] | Phase 7 | Complete - hello, cat, ls, uname .COM programs |
| [[medit-harvac]] | Phase 9 | Complete - MEDIT text editor ported as EDIT.COM |
| [[xfer]] | Phase 10 | Complete - XFER.COM serial file-transfer agent (COBS+CRC-16/32, protocol v1) |
| [[on-image-docs]] | Release | Complete - os-docs/ embedded on the image as DOCS/*.TXT |
| [[ncd]] | Phase 11 | Complete — NCD.COM dual-pane file manager with far-segment memory model |
| [[posix-api]] | Phase 12 | Complete — POSIX-like libc (open/read/write/lseek/dirent/stat/stdio) in lib/posix/ |
| [[harva-dev-kit]] | Phase 12 | Complete — HDK TUI library (video/keyboard/dialog/far/fmt) in lib/hdk/ |

## Key Source Files

| File | Role |
|------|------|
| `src/boot/mbr.asm` | MBR bootsector |
| `src/boot/vbr.asm` | Volume boot record |
| `src/kernel/entry.asm` | Kernel entry point |
| `src/kernel/kmain.c` | C main initialization |
| `src/kernel/syscalls.c` | Syscall dispatch table (file/dir/mount operations) |
| `src/drivers/fat16.c` | FAT16 driver implementation |
| `build.py` | Disk image builder (4 MB FAT16 image + DOCS/ embedding) |
| `os-docs/` | End-user docs (MANUAL, EDIT, XFER) - embedded as DOCS/ on the image |
| `tools/serial-xfer/` | Host-side transfer/mount tools (git submodule) |
| `ld/kernel.lbc` | Kernel linker script |
| `apps/xfer.asm` | XFER serial file-transfer agent (NASM flat .COM) |
| `lib/posix/` | POSIX-like libc sources (open/read/write/dirent/stat/stdio/stdlib/errno) |
| `lib/hdk/` | HDK TUI library sources (vid/kbd/dlg/far/fmt) |
| `lib/include/` | Shared headers for both libraries |