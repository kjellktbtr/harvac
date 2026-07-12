---
title: User Applications
type: code-map
sources:
  - apps/hello.c
  - apps/cat.c
  - apps/ls.c
  - apps/uname.c
  - apps/cp.c
  - apps/mv.c
  - apps/rm.c
  - apps/mkdir.c
  - apps/rmdir.c
  - apps/grep.c
  - apps/head.c
  - apps/tail.c
  - apps/sort.c
  - apps/cut.c
  - apps/less.c
  - apps/df.c
  - apps/du.c
  - apps/free.c
  - build.py
related:
  - "[[com-executor]]"
  - "[[syscall-dispatch]]"
  - "[[shell-commands]]"
  - "[[plan]]"
created: 2026-07-06
updated: 2026-07-12T15:50
confidence: high
---

# User Applications

Four user-space `.COM` programs are compiled with OpenWatcom and injected into
the disk image by `build.py`. They are loaded by `SYSCALL_EXEC` at a dynamically
allocated segment above the resident shell (`COM_SEGMENT 0x3000`) and communicate
via `INT 0x40` syscalls. See [[com-executor]] for the segment allocator.

## Entry Point

All apps declare their entry point as:

```c
void __far _main(void)
```

The `__far` qualifier is required because `exec.c` does a **far jump**
(`push seg; push 0x0100; retf`) to load the app at `0x4000:0x0100`. Without
`__far`, the compiler generates a near call target which the far jump cannot
reach.

### Critical: `_main` Must Be the First Function Defined

`wlink system com` does NOT generate a jump table; it places code in the binary
in the order functions appear in the object file (i.e., source order). The
`.COM` format always starts execution at file offset 0 (which maps to memory
offset `0x0100` after loading). If any helper function is defined before
`_main` in the source, the binary starts with that helper's code — the CPU
executes it instead of `_main`, producing garbage output or a crash.  In
practice this causes an immediate machine reset (triple-fault) because the
helper returns via near `ret` instead of far `retf`.

**Pattern:** Add forward declarations (prototypes) for all helpers directly
after any `static` data declarations, then put `_main` as the first **function
definition** in the file, followed by the helper definitions. See `apps/ls.c`
and `apps/shell.c` as reference, and all other apps in `apps/`.

Static data declarations (`static char buf[N];`, `static int nlines;`, etc.)
are emitted to `_DATA`/`_BSS` segments, not `_TEXT`, so they do **not** push
`_main` out of first position — only static function definitions do.

**Build-time guard (`build.py` `_parse_main_addr`):** after every C app link,
`build_app` generates a wlink map file (`option map=…`), parses the Memory Map
section for `_main_` (OpenWatcom-mangled name), and asserts its address equals
`0x0100`. If not, the build fails with a diagnostic naming the app and
explaining the fix. This makes the rule a hard compile-time invariant, not just
a convention.

### Piped Stdin Buffering (`lib/posix/stdio.c`)

`getline_fd` in `lib/posix/stdio.c` reads characters via `glfd_getc(fd)`.
For file fds (3+) it already uses a 512-byte sector buffer to avoid per-byte
disk I/O.  For `fd=0` (stdin), it previously did a 1-byte `read(0, &c, 1)` on
every character.  When stdin is **piped** (`g_stdin` set by the shell), each
1-byte call dispatches `SYSCALL_READ_STDIN` to the kernel, which calls
`fat16_read` with `count=1` — one full sector read per byte.  For a 1 KB pipe
payload that is ~1000 sector reads instead of 2.

**Fix (`lib/posix/stdio.c`):** `glfd_getc` now queries `SYSCALL_ISATTY` once
on the first call with `fd=0` and caches the result in `_stdin_is_tty` (an
`int8_t` initialized to `-1` in the data segment so it survives BSS zeroing).

- **Interactive tty:** keep the 1-byte read so the kernel line editor semantics
  (echo, backspace, Ctrl-D) are preserved.
- **Piped stdin:** reuse the same `_glfd_buf[512]` / `_glfd_pos` / `_glfd_end`
  buffer already used for file fds.  `read(0, _glfd_buf, 512)` calls
  `SYSCALL_READ_STDIN` with `maxlen=512`, which in turn calls `fat16_read`
  with `count=512` — one sector read per 512 bytes of pipe data.

No kernel change is needed; the kernel's pipe branch already honours the
requested `maxlen`.

## Cross-Segment Buffer Access

Apps pass string and buffer pointers with their **own DS** (segment `0x4000`),
not the kernel's DS (segment `0x2000`). The `INT 0x40` handler in
`syscall.asm` saves the caller's DS before switching to kernel DS, and passes
it as the 7th argument to `syscall_handler_c()`.

The kernel uses `read_far_b(caller_ds, offset)` and
`write_far_b(caller_ds, offset, value)` to access user buffers — near pointer
casts would read from kernel memory instead.

## Applications

### HELLO.COM (49 bytes)

Minimal "Hello from Harvac!" program using `SYSCALL_WRITE_STDOUT`.

**Source:** `apps/hello.c`

```c
void __far _main(void) {
    syscall_int40(SYSCALL_WRITE_STDOUT, 0, 0, 0, 0, SI("Hello from Harvac!\r\n"), 0);
}
```

### CAT.COM (194 bytes)

Reads hardcoded file "README  TXT" (packed 8.3 name with no dot), prints
contents byte-by-byte to stdout.

**Source:** `apps/cat.c`

Uses `SYSCALL_OPEN` → loop `SYSCALL_READ` → `SYSCALL_WRITE_CHAR` → `SYSCALL_CLOSE`.

Note: The file path is hardcoded as "README.TXT" via the packed 8.3 format
"README  TXT" (name padded to 8 chars with spaces, extension 3 chars, no dot).

### LS.COM (~440 bytes)

DOS-style directory listing via `SYSCALL_OPENDIR`/`SYSCALL_READDIR`/`SYSCALL_CLOSEDIR`.

**Source:** `apps/ls.c`

Two-pass algorithm:
1. Pass 1 - iterate all entries, print only directories (`attrs & 0x10`) with `<DIR>` suffix.
2. Pass 2 - iterate again, print only files with right-justified byte size in a 7-char field.
3. Footer: `"        N file(s)\r\n"` where N is the total count.

Column format: name[8] + ' ' + ext[3] (no dot), fixed 12-char field. FAT16
stores names space-padded to 8+3, so the raw `dirent_buf[0..10]` is printed
directly. File size is at `dirent_buf[28..29]` (low 16 bits of `file_size`
field at FAT16 dirent offset 28; all files here are < 64 KB).

`dir_buf` is `sizeof(fat16_dir_t)` = 14 bytes. `dirent_buf` is 32 bytes
(`sizeof(fat16_dirent_t)`). Both must be `static` because the kernel writes
through near pointers into DS=KERNEL_SEGMENT (see [[syscall-dispatch#Critical]]).

### UNAME.COM (136 bytes)

Prints "Harvac vX.Y" by calling `SYSCALL_GET_VERSION` (0x82) which writes
4 bytes (major, minor, patch, reserved) to a user buffer.

**Source:** `apps/uname.c`

## Build Process

In `build.py`, each app is compiled and linked like the kernel:

```
wcc -ms -os -s -zl -d0 -wx -bt=dos -i=src/include app.c
wlink format raw bin option offset=0x100 name APP.COM file app.obj
```

The `option offset=0x100` is what creates the `.COM` format (ORG 0x100),
placing the entry point at the standard DOS .COM load address.

## File Injection into Disk Image

Only `KERNEL.COM` and `SHELL.COM` go in the FAT16 root directory. All user
apps are listed in the `BIN_APPS` tuple in `build.py` (HELLO, CAT, LS, UNAME,
EDIT, NCD, XFER) and are written **directly into `BIN/`** via
`write_file_to_subdir()` after the directory structure is created:

```python
bin_cluster = find_directory_cluster(image, "BIN", first_data)
for app_name in BIN_APPS:
    data = (BUILD_DIR / f"{app_name}.COM").read_bytes()
    write_file_to_subdir(image, data, f"{app_name}.COM", first_data, bin_cluster)
```

Historical note: builds used to write each app to the root directory first,
then write a second copy into `BIN/` and mark the root entry deleted — which
duplicated every app's data on the image because the root copy's FAT chain was
never freed. The direct-to-BIN approach (2026-07-11) writes each app once.
Adding a new app to the image is now a one-line change to `BIN_APPS`.

`BIN/` starts as a single-cluster directory (512 bytes = 16 entries, 2 used by
`.` and `..`), so it holds at most 14 apps in one cluster. When the directory
is full, `write_file_to_subdir()` automatically extends the FAT chain: it
allocates a new cluster, zeroes it, chains `current → new → EOC`, and continues
writing. This is transparent — no manual directory pre-sizing is required.

Additionally, `_read_fat()` and `_write_fat()` in `build.py` now read/write the
full FAT (all `_FAT_SECTORS = 32` sectors = 16 KB), not just the first sector.
The earlier one-sector read limited cluster discovery to clusters 2–255 (~127 KB
of data space), causing "Not enough clusters" failures for larger app sets.

For the full list of commands and their behavior, see [[shell-commands]].