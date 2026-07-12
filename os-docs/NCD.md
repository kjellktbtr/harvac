# NCD (Norton Commander Clone)

Dual-pane file manager for Harvac OS, in the spirit of Norton Commander.
Fits in a COM file (~9 kB), installed as `BIN/NCD.COM` on the disk image.

## Usage

    NCD

Starts with both panels showing the root directory. Each panel keeps its
own current directory; the active panel shows a highlighted cursor bar
(the inactive panel shows none).

## Screen layout

    +------------------ / ------------------+
    |NAME        |SIZE |DATE      |TIME |   |     (per panel)
    |BIN         |<DIR>|          |     |   |
    |DEMO     BAT|  123|2026.07.11|21:00|   |
    +----------------------------------------+
    [Left] Sel:0                                  <- status line
    /BIN>                                         <- command line
    3 View 4 Edit 5 Copy 6 RenMov 7 MkDir ...     <- F-key bar

Sizes are shown in bytes up to 99999, larger files in kilobytes with a
`K` suffix. Files created by Harvac itself have no timestamp yet, so
their DATE/TIME columns are blank.

## Navigation

| Key | Action |
|-----|--------|
| Up/Down | Move the cursor bar |
| PgUp/PgDn | Move one page |
| Home/End | Jump to first/last entry |
| Enter | Enter directory, or run a .COM/.BAT file |
| Backspace / Left | Go to the parent directory |
| Right | Enter the selected directory |
| Tab (or Shift+Left/Right) | Switch active panel |
| Ins | Mark/unmark the current entry (shown in marker color) |
| F10 | Quit |

## File operations (F-keys)

| Key | Action |
|-----|--------|
| F3 | View the current file (scrollable); on a directory: enter it |
| F4 | Edit the current file in EDIT |
| Shift-F4 | Ask for a name, create the empty file, open it in EDIT |
| F5 | Copy marked entries (or the current one) to the other panel, after confirmation. Directories are copied recursively |
| F6 | Rename/move the current entry (dialog) |
| F7 | Create a directory in the active panel |
| F8 | Delete marked entries (or the current one), after confirmation. Directories are deleted recursively |

F5 refuses to copy when both panels show the same directory.

## Command line

Typing any printable character opens the command line (bottom of the
screen, above the F-key bar) pre-filled with that character. Enter runs
the command in the active panel's directory through the shell — built-ins
(`cd`, `echo`, redirects with `>`), .COM programs and .BAT scripts all
work. The screen is handed to the command while it runs; press any key
afterwards to return to the panels. Esc cancels the command line.

## Viewer keys (F3)

| Key | Action |
|-----|--------|
| Up/Down | Scroll one line |
| PgUp/PgDn | Scroll one page |
| Home/End | Jump to top/bottom |
| F3 / Esc | Close the viewer |

The viewer shows the first 4 kB of a file.

## Build

Built automatically by the project build (OpenWatcom V2 in `/opt/watcom`):

    make            # runs build.py; compiles apps/ncd/ to build/NCD.COM

The result is placed on the disk image as `BIN/NCD.COM`.

## Architecture

See `docs/wiki/ncd.md` for the code map (panel model in a far segment,
INT 40h syscall wrappers, recursive copy/delete) and the kernel-side
path resolution it relies on.
