---
title: .COM Executor
type: code-map
sources:
  - src/shell/exec.c
  - src/shell/exec_stub.asm
  - src/kernel/syscall.asm
  - src/kernel/syscalls.c
related:
  - "[[syscall-dispatch]]"
  - "[[shell]]"
  - "[[user-apps]]"
created: 2026-07-05
updated: 2026-07-09
confidence: high
---

# .COM Executor

Harvac can load and execute DOS `.COM` format programs from the FAT16
filesystem. User apps are compiled with OpenWatcom's `system com` format
(gives `ORG 0x0100`) and use `INT 0x40` (via `#pragma aux syscall`) to invoke
kernel services.

## Execution Model: DOS-style resident parent

The shell (`SHELL.COM`, at `COM_SEGMENT 0x3000`) is **resident**: it stays in
memory while children run. `SYSCALL_EXEC` **returns** to the shell when the
child exits, like `INT 21h/AH=4Bh` in DOS. The kernel saves the shell's
return context in a `pctx` stack entry before launching the child, and
restores it on child exit via `iret`.

### Segment allocator (`g_next_seg`)

Children are not loaded at a fixed segment. Instead, `syscall.asm` maintains
`g_next_seg` (a CS-relative DW), and each child is loaded at whatever segment
it points to. After loading, `g_next_seg` is bumped by the child's allocation
(`PSP (16 paras) + code + 2KB stack (128 paras)`). On child exit,
`g_next_seg` is reset to the child's start segment (the value saved in the
pctx entry), freeing the allocation.

Accessors: `get_next_seg_` / `set_next_seg_` (PUBLIC near procs in `syscall.asm`);
called from C as `get_next_seg()` / `set_next_seg(v)`.

`exec.c` initializes `g_next_seg` to `COM_SEGMENT + alloc_paras` after the
shell itself is loaded at boot, so children start above the shell.

### Execution Flow

```
SHELL.COM calls SYSCALL_EXEC (BX=name, CX=arg_ptr)
  INT 0x40 -> _int40_entry_ -> saves all regs to save area -> kernel stack
  -> syscall_handler_c_  (SYSCALL_EXEC case):
       child_seg = get_next_seg()
       fat16_open + fat16_read -> load to child_seg:0x0100
       alloc_paras = 16 + ((filesize+15)>>4) + 128
       set_next_seg(child_seg + alloc_paras)
       arg string -> child_seg:0x0082
       exec_save_parent_ctx_(child_seg) -> push save area + child_seg onto pctx stack
       exec_far_jump_sp(child_seg, 0x0100, KERNEL_SEG, child_return_off, child_sp)
         cli; SS:SP = child_seg:child_sp; sti; DS=ES=child_seg
         push return (KERNEL_SEG:child_return_); push target; retf
         -> child runs at CS=DS=ES=SS=child_seg

  Child exits (retf from __far _main, or SYSCALL_EXIT):
    CASE retf: CS:IP = KERNEL_SEG:child_return_
    CASE SYSCALL_EXIT -> return_to_parent_
    -> return_to_parent_:
         dec pctx_depth; compute entry offset = depth * 32 + pctx_stack
         restore g_next_seg = entry[child_seg]  (frees child allocation)
         copy entry fields to save_* area
         restore SS:SP from pctx (shell's original stack)
         push FLAGS/CS/IP onto shell's stack
         restore regs (AX=0=EXEC return value)
         sti; iret -> shell resumes after INT 0x40

SYSCALL_EXEC returns 0 in shell (child ran OK) or ERR_NOT_FOUND (not found).
```

### Root shell exit (no parent context)

When `exit` is typed at the interactive shell, `SHELL.COM` calls `SYSCALL_EXIT`.
`pctx_depth==0` (SHELL.COM was launched by `boot_shell`, not by `SYSCALL_EXEC`).
`return_to_parent_` takes the fallback path:
```
rtp_fallback:  restore KERNEL stack; jmp exec_reentry_
exec_reentry_: cli; SS:SP = 0x2000:0xE000; sti; call boot_shell_
```
`boot_shell` re-execs SHELL.COM from the filesystem.

## Segment Map

| Role | Segment | Notes |
|------|---------|-------|
| Kernel | `0x2000` (KERNEL_SEGMENT) | Stack top `0xE000` |
| SHELL.COM (resident) | `0x3000` (COM_SEGMENT) | Loaded at boot by `exec.c` |
| 1st child | `0x3000 + alloc_paras` | Dynamic; set by `g_next_seg` after shell loads |
| 2nd child | above 1st child | Dynamic; each child loaded above previous |

Each child gets exactly `PSP (16 paras = 256 bytes) + code rounded up to
paragraphs + 2KB stack (128 paras)`. No wasted 64KB windows.

## Parent Context Stack (`pctx`)

Defined in `src/kernel/syscall.asm`, CS-relative (kernel code segment):

```asm
PCTX_MAX      EQU 8
PCTX_ENTRY_SZ EQU 32    ; 15 fields x 2 bytes + 2 pad

pctx_depth  DW 0
pctx_stack  DB 256 DUP (0)   ; 8 * 32 bytes
```

Entry layout (byte offsets within each 32-byte entry):

| Offset | Field |
|--------|-------|
| 0 | child_seg (used to reset g_next_seg on child exit) |
| 2 | user_ss |
| 4 | user_sp |
| 6 | ax |
| 8 | bx |
| 10 | cx |
| 12 | dx |
| 14 | si |
| 16 | di |
| 18 | user_ds |
| 20 | user_es |
| 22 | user_bp |
| 24 | ip |
| 26 | cs |
| 28 | flags |
| 30 | (padding) |

Up to 8 nesting levels (shell -> child -> grandchild -> ...).

## Key Files

| File | Role |
|------|------|
| `src/shell/exec.c` | `exec_com()`: loads SHELL.COM to COM_SEGMENT at boot; initializes g_next_seg |
| `src/shell/exec_stub.asm` | `exec_reentry_`: restore kernel stack, call `boot_shell` |
| `src/kernel/syscall.asm` | INT 0x40 handler + pctx stack + g_next_seg allocator (get/set_next_seg, exec_save_parent_ctx, child_return_, return_to_parent_) |
| `src/kernel/syscalls.c` | `SYSCALL_EXEC`: dynamic alloc + pctx save + far jump; `SYSCALL_EXIT`: call `return_to_parent()` |

## Far Memory Access in Syscalls

The C handler (`syscalls.c`) uses `read_far_b(caller_ds, offset)` and
`write_far_b(caller_ds, offset, byte)` for all data access to the caller's
memory space. These use `#pragma aux` helpers. The kernel always uses the
captured `caller_ds` -- it doesn't need to know which process is calling.
See [[syscall-dispatch]] for the SS!=DS rule.
