---
title: OpenWatcom Toolchain
type: entity
sources:
  - /opt/watcom/binl/wcc
  - ld/kernel.lbc
related:
  - "[[architecture]]"
  - "[[plan]]"
created: 2025-07-05
updated: 2025-07-05
confidence: high
---

# OpenWatcom Toolchain

OpenWatcom v2.0 beta (Nov 2025 build) is installed at `/opt/watcom/`. It provides
a complete 16-bit x86 real-mode development toolchain.

## Tools

| Tool | Path | Purpose |
|------|------|---------|
| `wcc` | `/opt/watcom/binl/wcc` | 16-bit C compiler (small model `-ms`) |
| `wasm` | `/opt/watcom/binl/wasm` | x86 assembler (for kernel stubs) |
| `wlink` | `/opt/watcom/binl/wlink` | Linker with `rawbin` output support |
| `wcl` | `/opt/watcom/binl/wcl` | Compile-and-link driver |

## Kernel Binary Output

The kernel uses `system rawbin` + `format raw bin` to produce a flat binary
(no DOS/MZ header) loaded at `0x1000:0x0000`:

```
system rawbin
format raw bin
option offset=0x0000
option quiet
name KERNEL.COM
```

## User App Output

User .COM programs use `system com` (standard DOS .COM with `ORG 0x0100`):

```
system com
option quiet
name APP.COM
```

## Compiler Flags (Kernel)

| Flag | Effect |
|------|--------|
| `-ms` | Small memory model (near code, near data) |
| `-os` | Optimize for size |
| `-s` | Suppress stack overflow checks |
| `-zl` | Suppress default library search records |
| `-d0` | No debug info |
| `-wx` | Warnings as errors |
| `-bt=dos` | Target DOS calling convention |

## Port I/O via `#pragma aux`

OpenWatcom allows inline assembly via `#pragma aux`, which is used extensively
to avoid separate assembly files for hardware access. See `src/include/port_io.h`.

## WASM Assembler

WASM (Watcom Assembler) is used for kernel entry and syscall stubs. It produces
OMF object files (`.obj`) that link directly with `wlink`. The syntax is similar
to MASM 5.x but with some differences:

- Segments defined with `segment`/`ends` directives
- `assume ds:_DATA` for data segment references
- `public` for exported symbols
- `extrn` for external symbols (C identifiers)