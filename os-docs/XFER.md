# XFER — serial file transfer

Move files to and from a **HarvaC** machine over a plain **serial cable**, and
even **mount its filesystem** on your modern box — driven by a tiny (~2.5 KB)
hand-written agent and two Python scripts.

The HarvaC side is a single freestanding `XFER.COM` (assembled from
`apps/xfer.asm`, no runtime, no linker), installed as `BIN/XFER.COM` on the
disk image. The host side lives in `tools/serial-xfer/`: `host.py` (a
`scp`/`rsync`-style file-transfer tool) and `mountfs.py` (a FUSE filesystem
that makes the HarvaC disk show up under a local directory). The link is
binary-safe, CRC-checked, and resumes through line noise.

```
┌────────────┐   null-modem serial (COM1, 9600 8N1)   ┌─────────────────────────┐
│ modern host│  <───────────────────────────────────> │ HarvaC PC               │
│ host.py    │     COBS + CRC-16 + CRC-32 + ACK       │ XFER.COM (listening)    │
│ mountfs.py │                                        │ "press Q to quit"       │
└────────────┘                                        └─────────────────────────┘
```

## Features

- **Binary-safe & verified** — COBS framing, per-packet CRC-16, and a whole-file
  CRC-32 (zlib-compatible) on every transfer. Bad frames are NAK'd and resent;
  failed files are retried until they succeed or you press Ctrl-C.
- **Whole trees** — upload/download directory trees with structure preserved;
  long/illegal names are mangled to unique FAT 8.3 names automatically.
- **`dir` browsing** — list the agent's current directory from the host.
- **Live feedback on both ends** — the host shows a pre-transfer summary
  (per-file size + ETA) and dual progress bars; the HarvaC screen shows a
  fitted summary and a single 80-column status line that updates in place.
- **Mount it** — `mountfs.py` mounts the HarvaC disk as a local FUSE
  filesystem (the agent implements the full v2 byte-range protocol). Directory
  metadata is cached eagerly, so `ls`/`stat` are instant despite the slow
  link; file contents are fetched on demand.
- **Tiny, self-contained agent** — `XFER.COM` needs no resident driver or
  external libraries; just run `XFER` at the shell prompt. Quit any time with
  **Q** on the HarvaC keyboard.

## Requirements

- **Machine:** any PC with an **8088/8086 or later** CPU running **HarvaC**
  (reference target: IBM 5155 / 4.77 MHz 8088), plus a serial port
  (8250/16550 UART at COM1–COM4). The agent is assembled `cpu 8086` — no
  186/286/386 instructions — and uses only HarvaC INT 40h syscalls plus
  direct UART I/O, so it runs on the oldest IBM PCs and compatibles.
- A **null-modem serial cable** between the two machines (or an emulator's
  virtual serial port). The agent defaults to **COM1** at **9600 8N1**
  (COM1–4 and up to 115200 baud are supported as arguments).
- **Host:** Python ≥ 3.12 with `pyserial` (real ports) and `tqdm` (progress
  bars); `fusepy` + libfuse only for the mount tool. See
  `tools/serial-xfer/README.md` for host-side setup with uv.

## Hardware & cabling

This tool is aimed squarely at machines whose easiest (or only) link to the
modern world is an RS-232 serial port — the original **IBM PC 5150** and
**Portable PC 5155** (8088), and the many later PCs that have only a
3.5"/5.25" floppy and a serial port: no USB, no network. For those, a serial
cable beats shuttling media between machines.

**Connectors.** The IBM Asynchronous Communications Adapter in those PCs uses
a **DB-25 male** port; modern USB-serial dongles are **DB-9**. So you want a
null-modem cable/adapter that bridges **DB25 ↔ DB9**.

**A 3-wire cable is enough.** XFER paces the link with **software ACKs, not
hardware flow control**, so you only need three wires crossed null-modem
style:

```
   DB-9 (modern)              DB-25 (IBM PC)
   2  RxD  <───────────────  2  TxD
   3  TxD  ───────────────>  3  RxD
   5  GND  <──────────────>  7  GND
```

The agent raises DTR/RTS but never waits on CTS/DSR, so the handshake lines
can be left unconnected (or looped back locally if a stubborn UART/driver
insists on seeing them).

**Port and speed.** The agent defaults to **COM1** (I/O base 0x3F8) at
**9600 8N1**. Pass a baud rate and optionally a COM port number as arguments:
`XFER 38400` or `XFER 9600 2` (COM2). All four standard bases are supported
(COM1 0x3F8 · COM2 0x2F8 · COM3 0x3E8 · COM4 0x2E8). 9600 8N1 (~0.8 KB/s
effective) is a deliberately safe default: a 4.77 MHz 8088 polls the UART
comfortably at that rate, and the stop-and-wait protocol means there is no
overrun risk even on the slowest machine. If a given pair of machines proves
rock-solid, raise the rate on both ends (`XFER 38400` + `--baud 38400`).

## File-transfer tool — `host.py`

Start `XFER` on the HarvaC machine first, then pick how you reach it from the
host:

- `--port /dev/ttyUSB0` (or `/dev/ttyS0`, `COM3`, …) — a real serial port.
- `--tcp HOST:PORT` — an emulator exposing COM1 as a TCP socket.
- `--socket /path` — an emulator exposing COM1 as a Unix socket (QEMU
  `-serial unix:/tmp/hx.sock,server,nowait`).
- `--baud` (default 9600) — must match the rate given to `XFER`.

```bash
cd tools/serial-xfer

# upload files and/or a whole directory tree (structure preserved)
python host.py --port /dev/ttyS0 upload ./mydir ./notes.txt

# download a single file
python host.py --port /dev/ttyS0 download GOT.TXT ./got.txt

# list a directory: the root, a subdirectory, or everything recursively
python host.py --port /dev/ttyS0 dir "/"
python host.py --port /dev/ttyS0 dir DOCS
python host.py --port /dev/ttyS0 dir "/" -r

# tell the agent to exit
python host.py --port /dev/ttyS0 quit
```

**Note:** the wildcard part of a `dir` spec is ignored (the agent always
lists all entries), and directory paths resolve one level below the root
(matching the HarvaC filesystem layout). With no path, the agent lists its
**current working directory** (the directory the shell was in when `XFER`
was started); uploads land relative to that directory, with subdirectories
created as needed.

Failed files are retried indefinitely; **Ctrl-C** stops and prints the report
(including which files were still pending).

## Mount tool — `mountfs.py`

Mount the HarvaC disk as a local filesystem (needs fusepy + libfuse):

```bash
cd tools/serial-xfer
mkdir -p /mnt/harvac
python mountfs.py --port /dev/ttyS0 --root / /mnt/harvac
```

**Pass `--root /`** — the default remote base is `C:\` (for DOS agents), which
does not exist on HarvaC and yields an empty mount.

Then use normal tools — browsing is instant once the background crawl
finishes:

```bash
ls -R /mnt/harvac
cp /mnt/harvac/README.TXT .
echo "hi" > /mnt/harvac/NOTES.TXT
fusermount -u /mnt/harvac       # unmount (also tells the agent to quit)
```

The mount assumes it is the **only writer** while mounted — true for a
single-tasking HarvaC box.

## Wire protocol

A binary-safe stop-and-wait protocol: `COBS(packet)+0x00` frames, each packet
`TYPE SEQ DATA CRC16`, paced by ACK/NAK, with a whole-file CRC-32 check. The
HarvaC agent speaks **protocol v1** (directory entries carry FAT time/date
fields) and implements the full v2 packet set (delete/rename/byte-range I/O)
used by the mount tool. HarvaC's FAT16 driver does not set timestamps yet, so
dates are reported as zero (shown as unset on the host). See
`tools/serial-xfer/PROTOCOL.md` for the full specification.

## Building the agent

A prebuilt `XFER.COM` ships on the disk image (`BIN/XFER.COM`) and in
`build/XFER.COM`. It is rebuilt automatically by the project build:

```bash
make            # runs build.py, which assembles apps/xfer.asm with NASM
```

## Testing against QEMU

Boot the image with COM1 exposed as a Unix socket, run `XFER` at the HarvaC
prompt, then point `host.py` at the socket:

```bash
qemu-system-i386 -m 4 -drive file=harvac.img,format=raw \
    -serial unix:/tmp/hx.sock,server,nowait
python tools/serial-xfer/host.py --socket /tmp/hx.sock dir "/"
```

## Limitations

- Filenames are FAT **8.3** (≤12 chars), upper-case; the host mangles longer
  names to unique 8.3 names.
- File timestamps are not stored by HarvaC's FAT16 driver yet; listings show
  no dates, and uploaded files get zero timestamps.
- Directory paths resolve one level below the root (no nested trees on the
  standard image); the wildcard pattern in a `dir` spec is ignored.
- Over the mount, **reading and overwriting existing files works in any
  directory**, but **new files can only be created in the root** (the kernel's
  create call is root-only for now); creating elsewhere returns an error
  rather than misplacing the file. Delete/rename likewise act on the root
  only.
- COM port (1–4) and baud rate are command-line arguments; no recompile
  needed.

## License

GPL-2.0-only. See [LICENSE](../LICENSE).
© 2026 Kjell Kristian Grane Torgersen.
