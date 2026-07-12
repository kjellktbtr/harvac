---
title: On-Image Documentation (DOCS/)
type: code-map
sources:
  - build.py
  - os-docs/MANUAL.md
  - os-docs/EDIT.md
  - os-docs/NCD.md
  - os-docs/XFER.md
related:
  - "[[user-apps]]"
  - "[[shell]]"
  - "[[fat16-driver]]"
  - "[[ncd]]"
created: 2026-07-11
updated: 2026-07-12
confidence: high
---

# On-Image Documentation (DOCS/)

The end-user documentation in `os-docs/` is embedded on the disk image under
`DOCS/` so it can be read from inside HarvaC with `cd DOCS` + `cat <file>`.

## Pipeline (build.py)

During image population (after the `BIN/` apps are placed):

1. `DOCS` is created alongside `BIN`/`HOME`/`TMP` by `create_directory()`.
2. Each source markdown file is converted to plain text:
   - Unicode box-drawing/typographic characters are mapped to ASCII
     (`─`→`-`, `│`→`|`, corners→`+`, `↔`→`<->`, dashes/quotes, `≤`/`≥`),
     any remaining non-ASCII becomes `?`.
   - `LF` → `CRLF` (the shell's `cat` prints text as-is; the VGA TTY expects
     CRLF line endings).
3. The converted bytes are written into `DOCS/` with
   `write_file_to_subdir()`.

| Source | On image |
|--------|----------|
| `os-docs/MANUAL.md` | `DOCS/MANUAL.TXT` |
| `os-docs/EDIT.md` | `DOCS/EDIT.TXT` |
| `os-docs/NCD.md` | `DOCS/NCD.TXT` |
| `os-docs/XFER.md` | `DOCS/XFER.TXT` |

`README.TXT` in the root points users at `DOCS/MANUAL.TXT`.

## Reading the docs in-OS

`cat` uses `SYSCALL_OPEN`, which resolves the CWD one directory level (see
[[syscall-dispatch]]); so the working pattern is `cd DOCS` then
`cat MANUAL.TXT` — not `cat DOCS/MANUAL.TXT`.

## Conventions

- New user-facing docs go in `os-docs/` as markdown, get an 8.3 `.TXT` name,
  and are added to the tuple in `build.py`'s docs-copy loop.
- Keep `os-docs/` sources ASCII-friendly; the converter's replacement map
  only covers common typographic characters.
