# HarvaC User Manual

HarvaC is a small 16-bit operating system for classic IBM PC compatibles
(8088 and later). This manual covers the shell, batch scripts and the
bundled programs. On the disk image this file is `DOCS/MANUAL.TXT`.

## Booting

The BIOS loads HarvaC from the hard disk image: MBR, then the FAT16 volume
boot record, then `KERNEL.COM`. The kernel starts the shell (`SHELL.COM`)
and you get a prompt showing the current directory:

    Harvac Shell v0.4

    /> _

## The shell

Type a command and press Enter. Commands are case-sensitive built-ins or
the name of a program / batch script.

### Built-in commands

    help          Show the command list
    echo [text]   Print text; "echo on" / "echo off" controls batch echoing
    clear         Clear the screen
    cd [path]     Change directory ("cd" alone returns to /)
    pwd           Print the current directory
    exit          Leave the shell (the kernel restarts it)

### Running programs

Anything else is run as a program: the shell asks the kernel to execute
`NAME`, which is searched first in the root directory,
then in `BIN/`, with `.COM` appended automatically. So `ls`, `cat`,
`edit hello.txt` and `xfer` all work from anywhere.

Output can be redirected to a file with `>`:

    ls > LISTING.TXT

(The redirect file is created in the root directory.)

### Bundled programs (in BIN/)

    LS            List the current directory (directories first)
    CAT <file>    Print a file from the current directory
    EDIT [file]   Full-screen text editor — see DOCS/EDIT.TXT
    XFER [baud [com]]  Serial file-transfer agent — see DOCS/XFER.TXT
    UNAME         Show kernel name and version
    HELLO         Test program ("hello world")

## Batch scripts

A file ending in `.BAT` is a batch script: the shell runs it line by line.
Typing `NAME` also finds `NAME.BAT` if no `NAME.COM` exists. Rules:

- Each line is echoed with its prompt before running; `echo off` disables
  this, `echo on` re-enables it.
- A line starting with `@` is never echoed (hence the classic `@echo off`
  first line).
- Scripts end at end-of-file; there is no `CALL`, `GOTO`, `IF`, `FOR` or
  `%1`-style parameters.

Try the bundled example:

    /> demo

which runs `DEMO.BAT` in the root directory.

## Files and directories

The disk is FAT16 with classic **8.3 upper-case names** (`LETTER.TXT`,
`AUTOEXEC.BAT`). Paths use `/` as separator; the layout on the image is:

    /           KERNEL.COM, SHELL.COM, README.TXT, DEMO.BAT
    /BIN        the bundled .COM programs
    /DOCS       this manual and the program documentation
    /HOME       yours to use
    /TMP        scratch space

Use `cd DOCS` + `ls` to browse, and `cat MANUAL.TXT` to read files there.
File access resolves one directory level: work inside a directory with
`cd`, rather than giving nested paths to commands. HarvaC's FAT16 driver
does not store file timestamps yet.

## Getting files in and out

Run `XFER` on HarvaC and use the host-side tools over a serial cable (or
an emulator's virtual serial port) to upload, download or even mount the
HarvaC disk from a modern machine. See `DOCS/XFER.TXT` for cabling,
speeds and usage.

## Editing text

`EDIT [file]` opens a full-screen editor with menus (Alt-F/Alt-R/Alt-S),
clipboard, search/replace and CGA-friendly output. Esc closes menus and
dialogs. See `DOCS/EDIT.TXT` for the full key list.
