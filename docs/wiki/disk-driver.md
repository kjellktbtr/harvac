---
title: Disk Driver
type: code-map
sources:
  - src/drivers/disk.c
  - src/include/drivers/disk.h
  - src/include/port_io.h
related:
  - "[[fat16-driver]]"
  - "[[boot-chain]]"
  - "[[plan]]"
created: 2026-07-06
updated: 2026-07-10
confidence: high
---

# Disk Driver

The disk driver at `src/drivers/disk.c` provides sector I/O via BIOS
`INT 0x13` **CHS transfers** (AH=02h read, AH=03h write).

## Why CHS and not EDD (history)

The driver originally used the EDD extensions (AH=42h/43h with a Disk
Address Packet). Those only exist on ~1995+ BIOSes: QEMU/SeaBIOS supports
them, but the target hardware (real/simulated 386, 8086+XTIDE) does not.
Symptom on a 386: boot reached "Drivers OK" and froze at the first
kernel-mode disk access (SHELL.COM load). Rewritten to CHS 2026-07-10 -
AH=02h/03h works on every x86 BIOS.

## Geometry

Geometry is queried once, lazily, via INT 13h AH=08h
(`disk_ensure_geometry`, disk.c:19). Fallback if the query fails or
returns SPT=0: 63 sectors/track, 16 heads. The same query-first approach
is used by the MBR and VBR (see [[boot-chain]]) so the whole chain agrees
with the BIOS on the LBA-to-CHS mapping.

## Implementation

`disk_chs_op` (disk.c:38) is shared by read and write:

- Transfers **one sector per BIOS call**, so a track boundary never splits
  a transfer. Fine for this workload: fat16.c only ever asks for 1 sector.
- LBA fits in 16 bits (32 MB disk = 65536 sectors), so all CHS math is
  native 16-bit division - no 32-bit runtime helpers needed (the kernel is
  freestanding).
- INT 13h packing: CH = cyl bits 7:0, CL = sector | (cyl bits 9:8 << 6),
  DH = head, DL = drive.
- Retries 3 times per sector with an AH=00h controller reset between
  attempts.
- Advances the buffer by adding 32 to the *segment* per sector (512/16),
  avoiding 16-bit offset overflow for long multi-sector transfers.

### pragma aux helpers (`src/include/port_io.h`)

| Function | INT 13h | Notes |
|----------|---------|-------|
| `disk_chs_read_int(cx, dx, seg, off)` | AH=02h AL=1 | seg in SI, moved to ES inside (ES saved/restored) |
| `disk_chs_write_int(cx, dx, seg, off)` | AH=03h AL=1 | same convention |
| `disk_query_geom_int(drive)` | AH=08h | returns (max_head<<8) \| SPT, 0 on failure; zeroes ES:DI first |
| `disk_reset_int(drive)` | AH=00h | controller reset |

## Interface (`src/include/drivers/disk.h`)

| Function | Purpose |
|----------|---------|
| `disk_read_sectors(drive, lba, count, segment, offset)` | Read sectors, 0 = success |
| `disk_write_sectors(drive, lba, count, segment, offset)` | Write sectors, 0 = success |
| `disk_get_params(drive, *sectors, *heads)` | BIOS-reported geometry |
| `disk_reset(drive)` | Controller reset |

## Syscall Wiring

The disk driver is used indirectly - called by `fat16.c` (always with
count=1) to read/write FAT sectors and data clusters. There are no direct
syscalls for disk I/O.

## Limitations

- Disk must be <= 504 MB in BIOS geometry terms (cyl <= 1023); the 32 MB
  image is far below this.
- One-sector-per-call is slower than multi-sector EDD reads; acceptable
  for the small binaries involved.
- Tested in QEMU (SeaBIOS CHS path); designed for pre-EDD BIOSes -
  86Box/real-hardware verification pending.
