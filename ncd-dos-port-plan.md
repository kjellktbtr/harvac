# Plan: Port NCD to DOS as a standalone .COM binary

## Context
The HarvaC OS includes NCD (Norton Commander clone) as a built-in application (`apps/ncd/`). It uses the HarvaC HDK TUI library (`lib/hdk/`) and POSIX userspace library (`lib/posix/`) which are built on top of the HarvaC kernel's INT 0x40 syscall ABI.

The user wants a DOS port of NCD that:
1. Builds as a standard DOS `.COM` binary using OpenWatcom
2. Is placed in `/home/kjell/git/harvac/other/dos/ncd/`
3. Is NOT included in the HarvaC disk image
4. Replaces HarvaC-specific HDK/HDK calls with standard DOS/BIOS interrupts (INT 21h, INT 10h, INT 16h)

## Goal / Success Criteria
- [ ] `/home/kjell/git/harvac/other/dos/ncd/` contains a complete DOS port of NCD
- [ ] A build script (`build.sh` or `build.py`) compiles the DOS version using OpenWatcom tiny model (`-mt -bcl=com`)
- [ ] The resulting `NCD.COM` runs in DOSBox/QEMU DOS
- [ ] The HarvaC `build.py` is NOT modified to include the DOS binary in the disk image
- [ ] The DOS port uses standard C library functions where possible (conio.h, dos.h, stdio.h) and DOS interrupts for TUI operations

## Assumptions & Open Questions
- [ ] The DOS port will need to replace HDK video/keyboard/dialog functions with direct BIOS/DOS calls
- [ ] The FAT16 directory reading in `panel.c` (using POSIX `opendir`/`readdir`) needs to be replaced with DOS `findfirst`/`findnext` (INT 21h AH=4Eh/4Fh)
- [ ] The viewer will need to use DOS file I/O (INT 21h AH=3Dh/3Fh/3Eh) instead of POSIX
- [ ] The HDK dialog/box functions need replacement with simpler text-based dialogs
- [ ] Far memory allocation (SYSCALL_ALLOC) needs replacement with standard `malloc` or static buffers
- [ ] Build script should be a simple shell script or Python script in `other/dos/ncd/`

## Phase 1 — Project Structure & Build System
- [ ] Create `/home/kjell/git/harvac/other/dos/ncd/` directory structure (src/, include/, build/)
- [ ] Create `build.sh` or `build.py` for DOS build using OpenWatcom tiny model
- [ ] Create common header `dos_ncd.h` with DOS-specific type definitions and constants
- [ ] Verify build script compiles a simple "Hello World" COM binary

## Phase 2 — Core Infrastructure (Video, Keyboard, Memory)
- [ ] Create `dos_video.c/h` - VGA text mode operations via INT 10h / direct VGA memory (0xB8000)
- [ ] Create `dos_keys.c/h` - Keyboard input via INT 16h / BIOS keyboard handling
- [ ] Create `dos_mem.c/h` - Memory allocation (use malloc/free or static buffers)
- [ ] Create `dos_fs.c/h` - File system operations using INT 21h (findfirst/findnext, open/read/close)
- [ ] Create `dos_dialog.c/h` - Simple dialog boxes using video/keyboard primitives

## Phase 3 — Port Panel Module (Directory Browsing)
- [ ] Port `panel.c` → `dos_panel.c` using `dos_fs.c` for directory reading
- [ ] Replace far segment allocation with static arrays or malloc
- [ ] Adapt panel rendering to use `dos_video.c` functions
- [ ] Keep the same panel data structures and sorting logic where possible

## Phase 4 — Port Viewer Module
- [ ] Port `viewer.c` → `dos_viewer.c` using `dos_fs.c` for file reading
- [ ] Adapt viewer rendering to use `dos_video.c`
- [ ] Handle large files with buffered reading

## Phase 5 — Port Main Module & F-key Handlers
- [ ] Port `main.c` → `dos_main.c` with DOS entry point (`main` returning `int`)
- [ ] Adapt F3-F8 handlers to use DOS file operations (copy, move, delete, mkdir, edit/view)
- [ ] Replace `spawn`/`launch_child` with DOS `exec` (INT 21h AH=4Bh) or shell out
- [ ] Replace HDK dialogs with `dos_dialog.c` equivalents

## Phase 6 — Build & Test
- [ ] Run build script to produce `NCD.COM` in `other/dos/ncd/build/`
- [ ] Test in DOSBox or QEMU with FreeDOS
- [ ] Verify all F-keys work (F3=view, F4=edit, F5=copy, F6=move, F7=mkdir, F8=delete)
- [ ] Verify panel navigation, tab switching, selection with Insert

## Phase 7 — Documentation & Housekeeping
- [ ] Create `README.md` in `other/dos/ncd/` with build/run instructions
- [ ] Update `docs/wiki/index.md` and add wiki page for DOS NCD port
- [ ] Verify HarvaC `build.py` does NOT include the DOS binary in the disk image

## Done When
- [ ] All phases complete and checked off
- [ ] `NCD.COM` builds and runs in DOS
- [ ] HarvaC build still works and doesn't include DOS binary
- [ ] Documentation updated