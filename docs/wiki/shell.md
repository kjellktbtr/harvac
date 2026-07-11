---
title: Shell
type: code-map
sources:
  - src/shell/shell.c
  - apps/shell.c
related:
  - "[[com-executor]]"
  - "[[syscall-dispatch]]"
  - "[[user-apps]]"
created: 2026-07-06
updated: 2026-07-09
confidence: high
---

# Shell

Harvac has two shells: a kernel fallback REPL (in `src/shell/shell.c`) and the
primary user-space shell (`apps/shell.c`) which runs as `SHELL.COM`. The kernel
tries `SHELL.COM` first; only if it fails to load does it fall back to the
built-in REPL.

## Architecture

### Kernel-side (`src/shell/shell.c`)

Calls `boot_shell()` from `kmain()`:
1. Call `syscall_finalize_redirect()` to close any stdout redirect from a prior exec.
2. Call `vfs_init()`.
3. Try `exec_com(&root_fs, "SHELL   COM")` — if successful, this never returns
   (far-jumps to SHELL.COM). On .COM exit, control returns here via
   `exec_reentry` -> `boot_shell()` (starting over from step 1).
4. If exec fails, run the built-in kernel REPL (`kernel_shell()`).

### User-space REPL (`apps/shell.c`)

`SHELL.COM` is the primary interactive shell. It is a `.COM` user-space app
that runs the REPL loop via INT 0x40 syscalls:

1. Print CWD + prompt (`"> "`)
2. Read line via `SYSCALL_READ_STDIN` (calls `keyboard_getchar()` in kernel)
3. Scan line for `>` redirect operator (see below)
4. Find command word, null-terminate, advance to arg
5. Dispatch: built-ins first, then `SYSCALL_EXEC` for external commands
6. Close redirect if active, loop

#### Built-in Commands

| Command | Description |
|---------|-------------|
| `help` | List commands |
| `echo [args]` | Print args with CRLF |
| `clear` | Clear screen |
| `cd [path]` | Change directory (`/` if no arg) |
| `pwd` | Print working directory |
| `exit` | Call `SYSCALL_EXIT` (causes kernel to reboot SHELL.COM) |

Any unknown command is passed to `SYSCALL_EXEC`, which searches root then `BIN/`
for `CMD.COM`.

## Output Redirection (`>`)

The shell supports `cmd > file.ext` redirection for both built-in and external
commands.

**Parsing:** After skipping leading spaces, the line is scanned for `>`. If
found, the command portion is null-terminated there (trailing spaces stripped),
and the filename is taken from the rest of the line (leading spaces stripped,
null-terminated at next space).

**Setup:** The shell calls `SYSCALL_DELETE(filename)` to clear any existing
file, then `SYSCALL_CREATE(filename)` to get a file handle `rh`. It calls
`SYSCALL_SET_STDOUT(rh)` to redirect all kernel stdout to the file. `rh` is
saved as a `uint16_t`; `NO_REDIRECT` (0xFFFF) means no redirect.

**Built-in commands:** Output goes to the file automatically since
`WRITE_STDOUT` and `WRITE_CHAR` pass through `out_byte()` in the kernel.
After the command returns, the shell calls `SYSCALL_SET_STDOUT(0xFFFF)` and
`SYSCALL_CLOSE(rh)`.

**External commands:** `SYSCALL_EXEC` **returns** (DOS model; see
[[com-executor]]). The redirect cleanup (`SET_STDOUT(0xFFFF)` + `CLOSE(rh)`)
runs in the shell's `after_exec:` block after exec returns, so both built-ins
and external commands share the same cleanup path. Error messages printed after
a failed exec clear the redirect first so they appear on serial, not in the
file.

**Error handling:** If `SYSCALL_CREATE` returns `>= 16` (meaning `0xFFFF` for
any failure), the shell prints "Cannot create redirect file" and skips the
command.

See [[syscall-dispatch#Stdout Redirect]] for the kernel side.

## Batch Scripts (.BAT)

Scripts are a pure user-space feature — no kernel batch state. Because
`SYSCALL_EXEC` now returns, the shell stays resident and can hold batch state
in its own local variables.

### Invocation

- Type `DEMO.BAT` or `DEMO` (no extension): shell first tries `.COM`, then
  opens `DEMO.BAT` from the filesystem (root directory).
- `SYSCALL_OPEN` with a dotted name; `name_to_83` in the kernel converts
  `demo.bat` -> `DEMO    BAT`. Returns handle or 0xFFFF on failure.

### Line-by-line execution

`read_bat_line()` (`apps/shell.c`) reads 1 byte at a time via `SYSCALL_READ`,
skips `\r`, stops at `\n`, returns 1 while lines remain or 0 on EOF. On EOF the
batch handle is closed and the shell returns to interactive mode.

The regular REPL loop runs for each line: redirect parsing, command dispatch,
redirect cleanup. External commands (ls, cat, ...) run via `SYSCALL_EXEC` which
returns, so the loop advances to the next batch line. Built-ins run inline.

### Echo control

- Default: echo on (`bat_echo = 1`).
- `@`: suppress echo for the current line only (strips the `@`, sets
  `echo_this = 0`); remaining text is dispatched normally.
- `echo off` / `echo on`: sets `bat_echo = 0` or `1`. `echo off` alone still
  prints nothing when already in a `@echo off` line.
- When `bat_echo` is set and the line has no `@`: print `cwd> line\r\n`
  before dispatch.

### Single active script

Launching a second `.BAT` from within a script replaces the current cursor
(matches DOS without `CALL`). Nested `CALL`, `%1` parameters, `goto`/`:label`,
`if`/`for` are out of scope.

## Key Files

- `apps/shell.c` — User-space SHELL.COM (~300 lines); the full REPL + batch loop
- `src/shell/shell.c` — Kernel `boot_shell()` + fallback built-in REPL
- `src/shell/exec.c` — `exec_com()` for initial SHELL.COM load at boot
- `src/shell/exec_stub.asm` — `exec_reentry_`: restore kernel stack + call `boot_shell`