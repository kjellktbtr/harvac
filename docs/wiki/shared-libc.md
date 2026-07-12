---
title: Shared libc — Design Investigation
category: architecture
status: investigation (no committed direction)
---

# Shared libc — Design Investigation

## Background

Every `.COM` file in HarvaC is statically linked with its own copy of
`lib/posix/` and `lib/hdk/`.  That means the same string helpers, buffered I/O,
arg parser, and FAT formatting code is replicated in every binary.

### Measured duplication (as of Session 2)

| Binary       | Size (bytes) | Unique logic over HELLO |
|--------------|-------------|------------------------|
| HELLO.COM    | 8,092       | — (baseline)           |
| RM.COM       | 8,242       | 150                    |
| LS.COM       | 8,422       | 330                    |
| GREP.COM     | 8,514       | 422                    |
| LESS.COM     | 8,742       | 650                    |
| DU.COM       | 8,390       | 298                    |
| SHELL.COM    | 10,858      | 2,766                  |
| EDIT.COM     | 15,724      | 7,632                  |
| NCD.COM      | 13,434      | 5,342                  |

Total app bytes (22 apps, excl. kernel): **193 KB**  
Implied libc floor per app: **~8 KB**  
Rough libc-only bytes across all apps: **~178 KB** (22 × 8 KB)

The 24 KB process slot (`PROC_PARAS = 0x0600`) means each app already fits
comfortably.  Disk space on a 4 MB FAT16 image is not a concern.  The cost
today is mainly *duplication of disk bytes* and longer link times — not a
runtime memory problem.

---

## Option A — Resident libc via INT 0x40 syscalls

Move `strlen`, `strcpy`, `args_parse`, `format_size`, the buffered-I/O layer,
etc. into the kernel and expose them as new `INT 0x40` call numbers (e.g.
`SYSCALL_STRLEN = 0x50`, etc.).

**How it works:**  
Apps call `syscall_int40(SYSCALL_STRLEN, ...)` instead of calling an
inline function.  The kernel handles the call and returns a value in AX.

**Pros:**
- Zero per-app code duplication — every libc call is a 5-byte `INT 0x40`
  thunk.
- Library is always present; no loader or separate segment needed.
- ABI is already solved (INT 0x40 register convention).

**Cons:**
- Grows the kernel significantly.  Current `KERNEL.COM` is 18 KB; adding
  full libc (~8 KB) would push it toward 26 KB inside a 64 KB segment.
- Every `strcpy`, `strlen`, etc. becomes a full software interrupt (~50–100
  cycles on 286 vs 2–5 cycles inline).  Loop-heavy code (sorting, text
  rendering) slows down meaningfully.
- ABI lock-in: once apps depend on `SYSCALL_STRLEN = 0x50`, renumbering
  is a breaking change.
- Functions that must access *near* app data (e.g. `args_parse` writes
  into `argv[]`) need either far-pointer args or a different calling convention.

**Verdict:** Unattractive.  The performance hit for string-heavy operations
is real on 8 MHz 286 hardware, and bloating the kernel with utility code
violates the microkernel instinct that has served this project well.

---

## Option B — Shared `LIBC.COM` with a far-call table

Load a single library `.COM` once per boot; apps call through a fixed jump
table at a well-known address (e.g. segment `0x2F00`, pre-agreed offsets).

**How it works:**
1. `LIBC.COM` is loaded by the shell (or kernel) at a fixed segment and stays
   resident.
2. The first 512 bytes of `LIBC.COM` is an *export table*: a series of
   `CALL FAR` trampolines — one per exported function.
3. Apps are compiled with `extern __far void (*libc_strlen)(...)` pointing at
   the known segment:offset.  A thin `lib/stub/` replaces the real
   implementation with a single `CALL FAR [table_entry]`.
4. The loader (or a new `SYSCALL_LIBC_ADDR`) returns the segment so apps can
   find it.

**Pros:**
- Apps shrink to their unique code + stub thunks (~300–2,000 bytes).
- Library is loaded once and shared across all concurrently-runnable slots.
- Stays entirely outside the kernel — library bugs can be fixed without a
  kernel recompile.

**Cons:**
- Needs a loader change: the shell (or kernel exec path) must map the lib
  segment and pass its address to children.
- Far-call ABI: every libc call is `CALL FAR` (4-byte return address, extra
  segment register manipulation).  Still faster than INT 0x40 but slower than
  near inline.
- Lifetime and ref-count: who frees the library?  Simplest answer: it is
  permanently resident (never freed).  That costs ~8–10 KB of RAM forever.
- Build complexity: `build.py` must build `LIBC.COM` first, generate the
  export table, then build all apps with the stub library.  The linker script
  (`app.lbc`) changes.
- Stack alignment and calling conventions must match exactly between wcc
  small-model apps and the far-called library.  This is solvable but requires
  careful testing.

**Verdict:** The right long-term architecture if Harvac ever grows to 30+
commands where per-app duplication becomes a real disk or ROM constraint.
Not worth the build complexity for the current scale (~22 apps, 4 MB disk).

---

## Option C — Status quo (static link) + dead-strip (recommended)

Keep static linking but rely on `wlink`'s dead-code elimination to include
only the object files (and within each OBJ, only the functions) that each app
actually calls.

**How it works (today):**  
`build.py` links each app with the full `lib_posix.lib` and `lib_hdk.lib`
archives.  `wlink` with `-fe=.COM` already performs dead-code stripping at
the *module* (`.obj`) granularity — it only pulls in `.obj` files whose
symbols are referenced.  Within a module, all functions are included.

**Current state:**  
The 8 KB floor is dominated by `stdio.obj` (buffered I/O + `getline_fd`),
`string.obj` (all string helpers), `args.obj`, and `fmt.obj` — all pulled in
as a unit because at least one symbol from each is needed.

**To shrink further:**  
Split large multi-function `.c` files into one-function-per-file units so
`wlink` can strip at function granularity.  Example splits:

- `lib/posix/string.c` → `strcpy.c`, `strlen.c`, `strcmp.c`, `strcat.c`,
  `format_size.c`, `u32toa.c`, …
- `lib/posix/stdio.c` → `putchar.c`, `fputs.c`, `getline_fd.c`, `eputstr.c`
- `lib/posix/args.c` — already a single file, keep as-is

With fine-grained splitting, `HELLO.COM` would shrink to ~2–3 KB (just
`_main`, `fputs`, and `SYSCALL_EXIT`).  Utilities like `RM.COM` would be
~2.2 KB.  The total app footprint would drop from 193 KB to roughly 50–80 KB.

**Pros:**
- No new infrastructure: no loader changes, no far-call ABI, no kernel growth.
- Zero runtime overhead: all calls stay near (2–4 cycles).
- Mechanical refactor of `lib/posix/*.c` — low risk, reviewable in a single PR.
- Fully compatible with the existing build system; `build.py` needs no changes.

**Cons:**
- Refactoring ~10 files into ~50 single-function files clutters `lib/posix/`
  unless grouped into subdirectories or use a naming convention.
- Still duplicates code across apps — just less of it.  Not zero.
- Disk footprint saving (≈120 KB) on a 4 MB image is modest.

**Verdict:** Best return on effort for the current project scale.  When and if
Harvac grows to the point where a 4 MB image is full or RAM becomes scarce,
revisit Option B.

---

## Recommendation

**Do Option C now** (one-function-per-file splitting of `lib/posix/` and
`lib/hdk/`) if binary size ever becomes a concern.  The work is safe,
incremental, and fully compatible with the rest of the build.

**Defer Option B** until there is a concrete reason: disk image overflow,
RAM exhaustion at runtime, or a need for hot-patching the library.

**Never do Option A** for utility-class functions — the interrupt overhead on
real 8 MHz hardware is too high and the kernel bloat is undesirable.

---

## Memory layout impact

Current 24 KB process slot (`PROC_PARAS = 0x0600`):

```
0x0100  code start (_main)
  ...   app code + linked libc
  ...   BSS / static data
0x5FFE  initial SP (top of 24 KB)
```

With Option B (shared LIBC.COM), the slot shrinks to just unique code + stubs
(~2–4 KB), but an additional permanent 8–10 KB segment is consumed for the
library.  Net RAM impact is neutral to slightly worse for a small number of
simultaneous processes; slightly better once more than ~4 processes co-exist.
At the current single-child-at-a-time exec model, there is no benefit.
