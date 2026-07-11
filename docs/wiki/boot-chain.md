---
title: Boot Chain (MBR, VBR, kernel load)
type: code-map
sources:
  - src/boot/mbr.asm
  - src/boot/vbr.asm
  - src/kernel/entry.asm
  - src/kernel/kmain.c
related:
  - "[[architecture]]"
  - "[[fat16-driver]]"
created: 2026-07-10
updated: 2026-07-11
confidence: high
---

# Boot Chain

```
BIOS -> MBR (0x7C00) -> VBR (0x0900:0x003B, phys 0x903B) -> KERNEL.COM (0x2000:0x0000, phys 0x20000) -> kmain()
```

## Stages

1. **MBR** (`src/boot/mbr.asm`, LBA 0, loaded at 0x7C00)
   - Queries BIOS geometry (INT 13h AH=08h), computes CHS for LBA 63
     dynamically (`load_vbr`, mbr.asm:38-65). Falls back to SPT=63,
     heads=255 if the query fails.
   - Loads the VBR sector to 0x0900:0x0000 with 3 retries + reset between
     attempts, stores boot drive at 0x0900:0x01FF, far-jumps to 0x0900:0x003B.
2. **VBR** (`src/boot/vbr.asm`, LBA 63, org 0x003B)
   - Code starts at offset 0x3B of the sector; **max 453 bytes** (currently
     exactly 453 - adding any byte requires saving one elsewhere).
   - Also queries geometry (vbr.asm:50-60) and stores `num_sects`/`num_heads`
     used by `read_sector` for all LBA-to-CHS conversion.
   - Scans the 32 root directory sectors (LBA 128+) for `KERNEL  COM`,
     follows the FAT16 chain (FAT0 at LBA 64), copies each 512-byte sector
     from the bounce buffer 0x0900:0x0200 to 0x2000:loaded_bytes.
   - Far-returns to 0x2000:0x0000.
3. **Kernel entry** (`src/kernel/entry.asm`) - sets DS=ES=SS=0x2000,
   SP=0xE000, calls `kmain_`.

## Disk layout (FAT16, from build.py)

4 MB image (8192 sectors) since 2026-07-11; was 32 MB with 254-sector FATs.
The historical bug notes below still quote the old LBAs (572/604).

| LBA | Content |
|-----|---------|
| 0 | MBR |
| 63 | VBR |
| 64 | FAT0 (32 sectors) |
| 96 | FAT1 |
| 128 | Root dir (32 sectors) |
| 160 | First data sector (cluster 2 = KERNEL.COM start) |

8032 data clusters (1 sector/cluster) — above the 4085-cluster FAT16
minimum. Geometry is hardcoded three places that must stay in sync:
`build.py` (`create_fat16_partition` + module-level `_FAT_SECTORS`),
`src/boot/vbr.asm` equates, and the MBR partition entry size
(`mbr.asm`, `dd 8129`).

## Serial/VGA debug markers

Both bootloader stages emit single chars to COM1 (0x3F8) and VGA (INT 10h
AH=0Eh) via `dbg_char` (vbr.asm:209). A healthy boot prints:

```
Harvac> V R S . K L *...* J
```

| Char | Meaning |
|------|---------|
| `>` | MBR VBR-read attempt (`!` = retry after failure) |
| `V` | VBR entered |
| `R` | first disk reset done |
| `S` | root dir search starting |
| `.` | one root sector read |
| `K` | KERNEL.COM entry found |
| `L` | kernel load starting |
| `*` | one kernel sector loaded |
| `J` | jumping to kernel |
| `E` | fatal read error, `H` = KERNEL.COM not found |

`kmain()` continues with letters A-I written directly to VGA memory 0xB800:0
before/after each driver init (kmain.c:19-45, `DBG_VGA`), for diagnosing
hangs on real hardware where serial may be absent.

## Pitfall: 8086/8088 instruction set

Both boot files carry a `cpu 8086` NASM directive so any 186+ instruction
is rejected at assembly time. This caught a real bug: the far jumps were
`push imm16` / `retf`, and `push imm16` (opcode 0x68) is 186+ - on a real
8088 it misdecodes as a conditional jump, so the boot silently died right
after the VBR read. Both stages now use direct `jmp seg:off`.

The kernel is also 8086-clean as of 2026-07-11: wcc compiles with `-0`,
and entry.asm/syscall.asm carry `.8086` so wasm rejects 186+ ops too.

## Pitfall: dbg_char call sites must preserve AX

`dbg_char` preserves registers internally, but `mov al, 'X'` **before** the
call clobbers AL. If AX holds live data (cluster number, LBA), wrap the call
site in `push ax` / `pop ax`. This caused a real bug (see below).

## Bug history: kernel loaded from wrong cluster (fixed 2026-07-10)

Symptom: QEMU and real 386 printed the full `VRS.KL*...*J` sequence but the
kernel never reached `kmain()` - physical 0x20000 held garbage.

Root cause: `.load_kernel` did `mov al, 'L'` while AX still held the start
cluster (2). AX became 0x004C = 76, so the loader followed the FAT chain
from cluster 76 (LBA 678, middle of SHELL.COM/BIN data) instead of cluster 2
(LBA 604). The load "succeeded" and jumped into garbage.

Found with QEMU gdb stub: breakpoint after `call read_sector` in the load
loop showed AX=0x2A6 (LBA 678); 678-604+2 = cluster 76 = 0x4C = ASCII 'L'.

Fix: `push ax` / `pop ax` around the 'L' marker (vbr.asm:137-141).

## Memory requirements

Kernel segment 0x2000 (stack at offset 0xE000, so 0x20000-0x2E000 used).
Every process (shell and exec'd children) gets **PROC_PARAS = 16 KB**
(constants.h): code + BSS above the raw image + stack at the top
(initial SP = PROC_SP = 0x3FFE). SHELL.COM loads at 0x3000, children
stack upward from 0x3400 in 16 KB steps; SYSCALL_ALLOC blocks (EDIT gap
buffer, clipboard) come from the same bump allocator.

Minimum RAM to run a command is therefore **~224 KB**; on the 256 KB
IBM 5155: shell 0x3000, child 0x3400, 32 KB left at 0x3800 for ALLOC -
enough for EDIT's 32 KB fallback gap buffer (see gb_init in
apps/medit/gap.c: tries 48000 bytes, then 32 KB, then 16 KB).

Two guards prevent silent freezes when RAM runs out: SYSCALL_EXEC
returns ERR_NO_MEMORY and SYSCALL_ALLOC returns segment 0 when the block
would extend past the BDA base-memory top (word 0040:0013);
syscalls.c load_and_exec and SYSCALL_ALLOC.

## Geometry notes (real hardware)

- QEMU AH=08h reported SPT=63, heads=16 for the old 32 MB image (the 4 MB
  image gets a smaller head count). The LBA-to-CHS round trip is consistent
  for any head count as long as the same geometry is used for conversion and
  by the BIOS - which is why both MBR and VBR must query rather than hardcode.
- The VBR issues a second INT 13h reset after the geometry query
  (vbr.asm:62-66) because AH=08h may disturb some controllers (XTIDE).
- Open issue: on a real 8086 (IBM 5155 + XTIDE + CF), boot stops at `VRS.`
  - the first root-dir read (then LBA 572; now 128 after the 4 MB shrink)
  hangs. Tracked in `docs/improvements.md`.

## Debugging recipe (QEMU + gdb)

```
qemu-system-i386 -display none -m 4 -drive file=harvac.img,format=raw -s -S &
gdb -q -batch -ex "target remote :1234" -ex "set architecture i8086" \
    -ex "hbreak *0x9116" -ex "continue" -ex "info registers" ...
```

Useful physical breakpoints (VBR at CS=0x0900, so phys = 0x9000 + offset):
`0x9084` after geometry query, `0x9116` after each kernel-sector read,
`0x90CE` at the cluster fetch `mov dx,[di+0x1A]`, `0x91B5` at the INT 13h
inside `read_sector`. Buffer to inspect: 0x9200 (bounce), 0x20000 (kernel).
Note: a hardware breakpoint on the INT instruction itself re-fires without
advancing; break after the `call` instead.
