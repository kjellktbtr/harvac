---
title: XFER Serial File Transfer Agent
type: code-map
sources:
  - apps/xfer.asm
  - tools/serial-xfer/PROTOCOL.md
  - tools/serial-xfer/host.py
related:
  - "[[syscall-dispatch]]"
  - "[[fat16-driver]]"
  - "[[user-apps]]"
created: 2026-07-09
updated: 2026-07-11
confidence: high
---

# XFER Serial File Transfer Agent

`XFER.COM` is a user-space serial file-transfer agent that speaks a framed
binary protocol over COM1, letting a PC-side Python driver (`host.py`) push
and pull files to/from the Harvac FAT16 disk.

The source (`apps/xfer.asm`) is a direct NASM port of
`tools/serial-xfer/xfercom.asm` (git submodule). The protocol core (CRC-16, CRC-32,
COBS, framing, ACK/NAK logic) is carried over verbatim; only the OS-boundary
routines (file I/O, screen output, entry/exit) were rewired to Harvac INT 0x40
syscalls.

## Wire Protocol (v1)

Protocol is documented in `tools/serial-xfer/PROTOCOL.md`.
XFER.COM declares **protocol v1**: the VERSION ACK carries payload byte `0x01`
(xfer.asm `.h_version`). T_ENTRY packets use the v1 layout
`attr(1) size(4) time(2) date(2) name` — Harvac's FAT16 driver stores zero
timestamps, so hosts show entries as undated. The v1 timestamp bytes a host
appends to the upload CLOSE payload are accepted and ignored (`.h_close`
checks only the first 4 CRC bytes).

**History (2026-07-11):** the agent originally replied an *empty* VERSION ACK
(= v0) while already sending v1-layout T_ENTRY packets. `host.py` therefore
parsed the entry name at offset 5 — landing on the zeroed time/date words —
and every `dir` name came back blank. Declaring v1 aligned the handshake with
the actual T_ENTRY layout.

### Framing

- Each packet is COBS-encoded with `0x00` as the frame delimiter.
- Every packet carries a 2-byte CRC-16/CCITT (poly 0x1021, init 0xFFFF) over
  the raw (pre-COBS) payload, placed at the end.
- Stop-and-wait: sender waits for ACK (0x10) or NAK (0x11) before the next
  packet. Three retries, then abort.

### Packet Types

| Value | Name | Direction | Description |
|-------|------|-----------|-------------|
| 0x10 | ACK | agent -> host | Positive acknowledgement |
| 0x11 | NAK | agent -> host | Negative acknowledgement / retry |
| 0x01 | OPEN | host -> agent | Create/open file for write |
| 0x02 | DATA | host -> agent | Write chunk (128 bytes max) |
| 0x03 | CLOSE | host -> agent | End of file, verify CRC-32 |
| 0x04 | QUIT | host -> agent | Exit agent cleanly |
| 0x05 | GET | host -> agent | Request file stream to host |
| 0x06 | MKDIR | host -> agent | Create directory |
| 0x07 | LIST | host -> agent | List CWD entries |
| 0x08 | ENTRY | agent -> host | One directory entry |
| 0x09 | MSG | host -> agent | Display text on screen |
| 0x0A | RAW | host -> agent | Display raw bytes on screen |
| 0x0B | DEL | host -> agent | Delete file |
| 0x0C | RMD | host -> agent | Remove directory |
| 0x0D | REN | host -> agent | Rename file |
| 0x0E | PREAD | host -> agent | Positional read |
| 0x0F | PWRITE | host -> agent | Positional write |
| 0x10 | VERSION | host -> agent | Protocol handshake |

### CRC Details

- **Per-packet CRC-16:** CCITT, poly 0x1021, init 0xFFFF (same as USB CRC-16).
  Computed over the raw payload before COBS encoding; appended as two bytes
  (big-endian) at the end of the raw frame.
- **Per-file CRC-32:** zlib polynomial 0xEDB88320, init 0xFFFFFFFF, final XOR
  0xFFFFFFFF. Accumulated across all DATA chunks; verified on CLOSE against
  the host-supplied 4-byte value in the CLOSE packet.

## OS Boundary - Harvac Changes

Everything below was changed from the original DOS agent:

### File Operations

| Routine | DOS INT 21h | Harvac INT 40h |
|---------|------------|----------------|
| `do_open` (read) | AH=3Dh | `SYSCALL_OPEN` (0x10) |
| `do_open` (create/write) | AH=3Ch | `SYSCALL_CREATE` (0x15) |
| `do_close` | AH=3Eh | `SYSCALL_CLOSE` (0x11) |
| `do_read` | AH=3Fh | `SYSCALL_READ` (0x12) |
| `do_write` | AH=40h | `SYSCALL_WRITE` (0x13) |
| `do_lseek` | AH=42h | `SYSCALL_SEEK` (0x14) |
| `do_delete` | AH=41h | `SYSCALL_DELETE` (0x16) |
| `do_mkdir` | AH=39h | `SYSCALL_MKDIR` (0x23) |
| `do_rmdir` | AH=3Ah | `SYSCALL_RMDIR` (0x24) |
| `do_rename` | AH=56h | `SYSCALL_RENAME` (0x18) |

### Directory Listing (`serve_list`)

DOS `findfirst`/`findnext` (DTA) replaced with
`SYSCALL_OPENDIR` / `SYSCALL_READDIR` / `SYSCALL_CLOSEDIR`. Since 2026-07-11
the directory part of the host's spec is honoured: `serve_list` converts
`\` to `/`, truncates the spec at the last separator, saves the CWD with
`SYSCALL_GETCWD`, `SYSCALL_CHDIR`s to the requested directory (a leading
`/` alone means the root), lists it, and restores the saved CWD before the
final `T_CLOSE`. An unknown directory yields an empty listing. The wildcard
pattern itself is still ignored (all entries are returned), and paths
resolve one level below the root (kernel OPENDIR limitation). A spec with
no directory part lists the current directory as before. One `T_ENTRY`
packet is sent per `fat16_dirent_t` entry; the loop ends with `T_CLOSE`.

### Paths in GET / PREAD / PWRITE (`path_enter` / `path_leave`)

The DOS original passed full paths straight to INT 21h; Harvac syscalls take
a bare 8.3 name in the CWD. Since 2026-07-11 (second fix that day) the shared
helpers `path_enter`/`path_leave` in `apps/xfer.asm` give the file handlers
the same directory support `serve_list` has: `path_enter` converts `\` to
`/`, splits the path at the last separator, saves the CWD (`SYSCALL_GETCWD`
into `v_cwdsave`), CHDIRs to the directory part (a leading separator alone
means the root) and returns the basename; `path_leave` restores the CWD.
The CWD is restored immediately after `do_open` — an open handle survives a
CHDIR. Applied to `serve_get` (download), `.h_pread` and `.h_pwrite`.

**Without this, every file read through `mountfs.py` came back empty**: the
FUSE mount reads via `T_PREAD` with full paths like `\DOCS\MANUAL.TXT`, the
kernel open failed on the mangled name, and the handler's empty-ACK reply is
indistinguishable from EOF.

`PWRITE` refuses the create-fallback (`O_CREAT`) unless the effective
directory is the root: `SYSCALL_CREATE` is root-only, so creating anywhere
else would silently drop the file in the wrong directory. Overwriting
existing files works in any (one-level) directory via the CWD-aware
`SYSCALL_OPEN`. `DEL`/`RMD`/`REN` deliberately did **not** get path support:
the kernel calls behind them are root-only, and CHDIRing first would make
them act on the wrong directory instead of failing cleanly.

`mountfs.py` must be started with `--root /` (its default `C:\` targets DOS
agents and yields an empty mount on Harvac).

### Screen Output

`T_MSG`, `T_RAW`, and the startup banner use **`SYSCALL_WRITE_VGA` (0x0A)**
to write text to the VGA screen only. `SYSCALL_WRITE_STDOUT` (0x00) was not
used because it also writes to COM1 (`out_byte` calls both `video_tty_putchar`
and `serial_putchar`), which would corrupt the binary protocol stream.

### Entry / Exit

- `parse_args` reads the arg string from `PSP+0x82` (Harvac EXEC writes the
  argument string there; DOS PSP would put the count byte at 0x80 and the
  string at 0x81, but Harvac skips the count). Default: COM1 at 9600 baud
  (divisor 12).
- NUL byte (0x00) is treated as end-of-args at every check; the original code
  only checked for 0x0D (DOS CR). This matters because Harvac EXEC uses
  calloc-style zeroed memory for the child segment, so 0x81 is always 0x00.
- On quit (`T_QUIT` packet or host error), exits with `SYSCALL_EXIT` (0x41).

### UART Access

`uart_init`, `uart_getc`, `uart_putc` use direct `in`/`out` port I/O
(COM1 = 0x3F8) and are unchanged from the original. The kernel serial driver
is bypassed intentionally: it does CR-to-LF translation that would corrupt
binary frames.

## Build

`build.py:build_xfer()` assembles with:

```
nasm -f bin -o XFER.COM apps/xfer.asm
```

`XFER.COM` (flat `.COM`, no linker) is injected into the `BIN/` directory on
the FAT16 image by `write_file_to_subdir("XFER.COM", ...)`, the same path
used for `EDIT.COM`.

## Running End-to-End

### Start QEMU

```bash
make xfer
```

This launches QEMU with:
- VGA display + PS/2 keyboard (`-display sdl`, no `-nographic`)
- COM1 on a Unix socket: `-serial unix:/tmp/harvac-xfer.sock,server=on,wait=off`
- QMP on a Unix socket: `-qmp unix:/tmp/harvac-qmp.sock,server=on,wait=off`

### Launch the Agent

At the Harvac shell, type:

```
xfer
```

The banner `XFER agent ready` appears on the VGA screen. The agent now
blocks in the `read_frame` loop waiting for a packet from `host.py`.

### Connect `host.py`

```bash
python tools/serial-xfer/host.py --socket /tmp/harvac-xfer.sock <command>
```

CLI commands (`host.py`):

| Command | Description |
|---------|-------------|
| `dir [spec] [-r]` | List a directory (`dir "/"`, `dir DOCS`, `-r` = recursive) |
| `upload local... ` | Upload files/trees (CRC-32 verified) |
| `download remote [local]` | Download file (`download DOCS/MANUAL.TXT`) |
| `quit` | Exit agent, return to shell |

The remaining packet types (mkdir/del/ren/pread/pwrite) are exposed as
`host.Link` methods and used by `mountfs.py`, not as CLI commands.

## Scope Cuts (Limitations)

- **LIST wildcard ignored**: the directory part of the `LIST` spec is
  honoured (since 2026-07-11), but the wildcard pattern is not — all entries
  are always returned. Directory paths resolve one level below the root.
- **Create/delete/rename are root-only**: `PWRITE` overwrites existing files
  in any directory but refuses to create outside the root; `DEL`/`RMD`/`REN`
  pass names through untouched and only work on root entries (kernel
  `SYSCALL_CREATE`/`DELETE`/`RENAME` are root-only — see
  `docs/improvements.md`).
- **No stored timestamps**: T_ENTRY time/date fields are sent but always zero
  (the FAT16 driver does not set them); GET replies a CRC-only CLOSE, and the
  v1 timestamp bytes on upload CLOSE are ignored.
- ~~**No local keyboard abort**~~ Fixed 2026-07-11: pressing `q`/`Q` on the
  local keyboard quits the agent. The `uart_getc` wait loop polls
  `SYSCALL_KEY_AVAILABLE` (0x0B) + `SYSCALL_READ_CHAR` - the earlier direct
  port 0x64/0x60 poll stopped working when the kernel keyboard driver went
  IRQ1-driven, and port 0x64 does not exist on XT machines. The syscalls
  read only the keyboard ring buffer, never the UART, so protocol bytes
  are not consumed. `T_QUIT` from host.py still works too.

See `docs/improvements.md` for open items.
