# Plan: hexdump command

## Context
Add a `HEXDUMP.COM` user-space program that replicates the classic Linux `hexdump` byte-mode output format. The command reads files (or stdin) and prints a hex dump with offsets, hex bytes, and ASCII sidebar, with `*` for repeated lines.

## Goal / Success Criteria
- `HEXDUMP FILE` outputs a classic hexdump: 8-digit hex offset, 16 hex bytes per line with extra space at byte 8, ASCII `|...|` sidebar
- Repeated identical lines are replaced with a single `*`
- Final offset is printed after the last data line
- Reads from stdin when no file arguments given
- Supports multiple file arguments

## Assumptions & Open Questions
- Offset is 32-bit for files > 64 KB (using `uint32_t`)
- Output format matches the example exactly: lowercase hex, 2 spaces before hex, 2 spaces before ASCII sidebar
- `*` repeat logic: if a line of 16 bytes is identical to the previous line, output `*`; skip `*` on first line of a new sequence

## Phase 1 — Write hexdump.c
- [ ] Create `apps/hexdump.c` with `_main` as the first function, following the cat.c pattern
- [ ] Implement hex byte formatting helper (`byte_to_hex`)
- [ ] Implement 32-bit hex offset formatting helper (`offset_to_hex`)
- [ ] Implement `hexdump_fd()` that reads 16 bytes at a time, formats output, and handles `*` repeats
- [ ] Verify: `make check` compiles without errors
- [ ] Housekeeping: build passes

## Phase 2 — Wire into build system
- [ ] Add `"HEXDUMP"` to `BIN_APPS` tuple in `build.py`
- [ ] Add `"hexdump"` to the app build loop in `build.py`
- [ ] Verify: `make` produces `build/HEXDUMP.COM`
- [ ] Housekeeping: full build passes

## Done when
- [ ] All phases complete and checked off
- [ ] `make` builds successfully with HEXDUMP.COM
- [ ] `make check` passes
- [ ] Changes committed (with user approval)