---
title: Architecture Overview
type: code-map
sources:
  - src/kernel/entry.asm
  - src/kernel/kmain.c
  - src/include/constants.h
related:
  - "[[toolchain]]"
  - "[[plan]]"
created: 2025-07-05
updated: 2025-07-05
confidence: high
---

# Architecture Overview

Harvac is a 16-bit real-mode x86 operating system written primarily in C
with OpenWatcom. It is a direct port of [[harva/]] (NASM assembly original).

## Boot Sequence

```
BIOS -> MBR (0x7C00) -> VBR (phys 0x903B) -> Kernel (0x2000:0x0000) -> kmain()
```

See [[boot-chain]] for the full boot chain, debug markers, and disk layout.

## Memory Layout

| Range | Size | Purpose |
|-------|------|---------|
| `0x00000-0x0FFFF` | 64 KB | BIOS data, IVT, boot sectors |
| `0x10000-0x1FFFF` | 64 KB | Kernel code & data (segment `0x1000`) |
| `0x20000-0x9EFFF` | ~512 KB | Heap (bitmap allocator, 16-byte pages) |
| `0x9F000` | — | Stack top (grows down) |
| `0xA0000+` | — | VGA memory, BIOS reserved |

Source: `src/include/constants.h`

## Memory Model: OpenWatcom `-ms` (Small)

- All pointers are NEAR (16-bit offset within the segment)
- One 64KB code segment (`_TEXT`)
- One 64KB data segment (`_DATA` + `CONST` + `_BSS`)
- No C runtime — `entry.asm` sets up segments and calls `kmain()`
- No standard library — freestanding environment

## Output Strategy

- **Kernel**: flat binary via `system rawbin` / `format raw bin`
- **User apps**: DOS .COM via `system com` (standard `cstart_t.obj` startup)
- **Bootloader**: NASM `-f bin` (standalone, unchanged from harva/)

## When Assembly Is Required

See `plan.md` for the assembly boundaries table. Key points:

1. **MBR/VBR** — NASM (bootloader, 512-byte constraint)
2. **`_start` entry** — WASM (set segments/stack, call `kmain`)
3. **`INT 0x40` handler** — WASM (save/restore regs, dispatch to C)
4. **Port I/O** — C via `#pragma aux` (no separate asm files needed)

## Subsystem Layering

```
┌──────────────────────────────┐
│  Shell (shell/, exec, etc.)  │
├──────────────────────────────┤
│  Syscall dispatch (INT 0x40) │
├──────────────────────────────┤
│  VFS Layer (fs/vfs.c)        │
├──────────────────────────────┤
│  Drivers (video, kbd, etc.)  │
├──────────────────────────────┤
│  Libraries (strings, mem)    │
├──────────────────────────────┤
│  Kernel core (entry, kmain)  │
└──────────────────────────────┘
```