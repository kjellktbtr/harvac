---
title: Shell Commands
type: code-map
sources:
  - apps/shell.c
  - apps/cp.c
  - apps/mv.c
  - apps/rm.c
  - apps/mkdir.c
  - apps/rmdir.c
  - apps/cat.c
  - apps/grep.c
  - apps/head.c
  - apps/tail.c
  - apps/sort.c
  - apps/cut.c
  - apps/less.c
  - apps/df.c
  - apps/du.c
  - apps/free.c
  - lib/posix/args.c
  - lib/posix/fileops.c
  - lib/posix/stdio.c
related:
  - "[[syscall-dispatch]]"
  - "[[posix-api]]"
  - "[[user-apps]]"
  - "[[pipes]]"
created: 2026-07-12
updated: 2026-07-12
confidence: high
---

# Shell Commands

Harvac has two command surfaces: built-in commands in the userspace shell
(`apps/shell.c`) and external `.COM` programs in `BIN/` on the disk image.
All `.COM` programs are stdin/stdout-aware so they compose with pipes.

## Built-in Commands

Implemented directly in `apps/shell.c`; they run without spawning a child
process.

| Command | Description |
|---------|-------------|
| `cd [DIR]` | Change working directory (default `/`) |
| `pwd` | Print working directory |
| `echo [TEXT]` | Print text to stdout |
| `clear` | Clear the screen |
| `help` | List built-in commands |
| `exit` | Return to kernel REPL |

## File Management (BIN/)

External programs. All live in `BIN/` on the disk image.

| Command | Source | Description |
|---------|--------|-------------|
| `cp [-r] SRC DST` | `apps/cp.c` | Copy file or directory. Without `-r`, refuses to copy a dir (prints error). `-r`: recursive; if DST is an existing dir, copies SRC into it (Unix semantics). |
| `mv SRC DST` | `apps/mv.c` | Move/rename. Tries `rename()`; falls back to copy+delete across dirs. |
| `rm [-r] FILE...` | `apps/rm.c` | Remove files or directories. `-r` recursive (uses `remove_tree`). |
| `mkdir DIR...` | `apps/mkdir.c` | Create one or more directories. |
| `rmdir DIR...` | `apps/rmdir.c` | Remove empty directories. |

`cp -r` and `rm -r` are powered by `copy_tree()` / `remove_tree()` in
`lib/posix/fileops.c`, extracted from NCD's `ncd_copy_recursive` /
`ncd_delete_recursive`. These are the single source of truth; NCD could be
refactored to use them in the future.

## Text Filters (BIN/)

All text filters share the convention: if file arguments are given, process
each; otherwise read `STDIN_FILENO`. This makes them pipe-ready.

| Command | Source | Description |
|---------|--------|-------------|
| `cat [FILE...]` | `apps/cat.c` | Concatenate files or stdin to stdout. |
| `ls [PATH]` | `apps/ls.c` | DOS-style directory listing. Optional PATH arg (chdir save/restore). |
| `grep [-i] PAT [FILE...]` | `apps/grep.c` | Print lines matching substring PAT. `-i` case-insensitive. |
| `head [-n N] [FILE...]` | `apps/head.c` | Print first N lines (default 10). |
| `tail [-n N] [FILE...]` | `apps/tail.c` | Print last N lines (default 10); ring buffer. |
| `sort [FILE]` | `apps/sort.c` | Sort lines of file or stdin (bounded: ≤256 lines, ≤9 KB). |
| `cut -d D -f N [FILE]` | `apps/cut.c` | Extract field N (1-based) with delimiter D (default tab). |
| `less [FILE]` | `apps/less.c` | Interactive pager (Space=next, b=prev, j/k=line, q=quit). |

### `less` key bindings

| Key | Action |
|-----|--------|
| Space / Enter / PgDn | Next page |
| `b` / PgUp | Previous page |
| `j` / Down | Scroll down one line |
| `k` / Up | Scroll up one line |
| `q` / ESC | Quit |

`less` reads the entire input into `static char lines[150][80]` (12 KB BSS;
fits in the 24 KB process slot). Lines wider than 79 characters are truncated.

### Shared helpers

- **`args_parse`** (`lib/posix/args.c`) — tokenizes the PSP tail at `0x0082`
  in-place, returns `argc`. All `.COM` apps use this instead of hand-rolling
  their own argument parsing.
- **`getline_fd`** (`lib/posix/stdio.c`) — reads one `\n`-terminated line
  from a file descriptor, strips `\r`, returns byte count or −1 at EOF. Used
  by all line-oriented filters.
- **`copy_file` / `copy_tree` / `remove_tree`** (`lib/posix/fileops.c`) —
  shared file operation helpers used by `cp` and `rm`.

## Sysadmin (BIN/)

| Command | Source | Description |
|---------|--------|-------------|
| `free` | `apps/free.c` | Total/used/free RAM in KB. Uses `SYSCALL_MEM_INFO` (0x52). |
| `df` | `apps/df.c` | Disk total/used/free in KB. Uses `SYSCALL_STATFS` (0x33). |
| `du [PATH...]` | `apps/du.c` | Estimated disk usage of files/dirs. Accepts multiple args; no args = CWD. Pure userspace dir walk. |

`free` and `df` require kernel syscalls; `du` is pure userspace using
`opendir`/`readdir`/`stat`. The `df` arithmetic deliberately uses only 16-bit
operations because `bytes_per_sector` is always 512 on this platform:
`total_kb = (total_cl * spc) >> 1` (no `__U4M` needed — see
[[syscall-dispatch#Critical: SS != DS during the handler]]).

## Pipes

The shell (`apps/shell.c`) supports `|` pipelines using a temp-file model.

### Syntax

```
cmd1 [| cmd2 [| cmd3 ...]] [> OUTFILE]
```

Up to four pipeline stages. The trailing `>` redirect applies only to the last
stage's output.

### Mechanism

For a two-stage pipeline `cmd1 | cmd2`:

1. Shell creates `/TMP/PIPE0` (empty, writable).
2. Sets `SYSCALL_SET_STDOUT` → PIPE0 handle, then execs `cmd1`. All of `cmd1`'s
   stdout goes into `/TMP/PIPE0`.
3. Shell opens PIPE0 for reading. Sets `SYSCALL_SET_STDIN` → PIPE0 read handle,
   then execs `cmd2`. `cmd2` reads from the temp file instead of the keyboard.
4. Shell clears both redirects, closes handles, and deletes PIPE0.

The temp files live in `/TMP/` on the disk image (pre-created at build time).
Each stage runs to completion before the next starts (no concurrency).

### Stdin redirect (`SYSCALL_SET_STDIN`, 0x0C)

Mirrors `SYSCALL_SET_STDOUT` (0x08). The kernel stores an `g_stdin` handle
(default `0xFFFF` = keyboard). When `SYSCALL_READ_STDIN` is invoked and
`g_stdin` is active, the kernel reads raw bytes from the file into
`static uint8_t tmp_in[512]` and returns the count (0 = EOF). Interactive EOF
via Ctrl-D (`0x04`).

All userspace filters transparently pick up the redirect because `read()` in
`lib/posix/unistd.c` already routes through `SYSCALL_READ_STDIN` for
`STDIN_FILENO`.

`syscall_finalize_stdin()` (mirroring `syscall_finalize_redirect()`) is called
by `boot_shell()` on each exec re-entry to clear any lingering stdin handle.

## Glob Expansion

The shell (`apps/shell.c`) expands `*` and `?` wildcard tokens **before exec**,
so every command benefits without per-command changes.

### Mechanism

After tokenizing the command line (`|`-split, then space-split per stage):
1. Each token is tested with `has_glob()` (`lib/posix/fnmatch.c`): true if it
   contains `*` or `?`.
2. Tokens with a `/` are never expanded (path components are left literal).
3. For matching tokens: `opendir(".")`, iterate entries, `fnmatch(token, name)`
   (case-insensitive). Each match is appended to a static `_glob_buf[512]`
   (space-separated, already the right format for the arg string passed to exec).
4. If no entry matches, the literal token is kept (nullglob-off, like bash default).

### `fnmatch` implementation (`lib/posix/fnmatch.c`)

Case-insensitive classic non-recursive backtracking:
- `*` matches any sequence of characters (including empty).
- `?` matches exactly one character.
- A `*` followed by a longer match can back up: the algorithm saves the last
  `*` position and retries from the character after each failure.

```c
int fnmatch(const char *pattern, const char *name);
int has_glob(const char *str);   /* returns 1 if str contains * or ? */
```

Declared in `lib/include/fnmatch.h`; built as part of `lib_posix.lib`.

### Examples

```
ls *.TXT            → lists all .TXT files in CWD
cat *.BAT           → concatenates all .BAT files
du *                → disk usage of every entry in CWD
rm *.TMP            → remove all .TMP files
echo *              → shell prints all filenames
```

### Example pipelines

```
ls | grep -i com
cat README.TXT | head -n 5
ls | sort | less
```

## Adding New Commands

1. Write `apps/<name>.c` with `_main` as the first function in the file
   (wlink source-order rule; see [[user-apps]]).
2. Use `args_parse` for argument parsing, `getline_fd` for line reading.
3. Add `"NAME"` to `BIN_APPS` in `build.py` (around line 255).
4. Add the compile + link call in the app loop in `build.py` (around line 433).

No changes to `build_libs()` are needed — it auto-globs `lib/posix/*.c` and
`lib/hdk/*.c`.
