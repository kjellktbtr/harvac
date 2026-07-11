---
title: VFS & Mount Layer
type: code-map
sources:
  - src/fs/vfs.c
  - src/fs/mount.c
  - src/include/fs/vfs.h
  - src/include/fs/mount.h
related:
  - "[[fat16-driver]]"
  - "[[syscall-dispatch]]"
  - "[[plan]]"
created: 2026-07-06
updated: 2026-07-11
confidence: high
---

# VFS & Mount Layer

The VFS (Virtual Filesystem) layer provides path resolution, CWD tracking, and
dispatch to lower-level filesystem drivers. It sits between `syscalls.c` and
`fat16.c`, enabling support for multiple mounted partitions.

## Mount Table (`src/fs/mount.c`)

The mount table holds up to 8 `mount_entry_t` entries, each tracking:

| Field | Type | Description |
|-------|------|-------------|
| `used` | `uint8_t` | Entry in use flag |
| `mount_point` | `char[32]` | e.g. `"/"` for root |
| `drive` | `uint8_t` | BIOS drive number (0x80 = first HDD) |
| `partition_lba` | `uint32_t` | Partition start sector |
| `fs` | `fat16_fs_t` | Mounted FAT16 filesystem state |

### Key Functions

| Function | Purpose |
|----------|---------|
| `mount_init()` | Initialises table, mounts root `/` on drive 0x80 LBA 63 |
| `mount_add(mount_point, drive, lba)` | Mount a new filesystem (returns 0 on success) |
| `mount_remove(mount_point)` | Unmount (marks entry free, root cannot be unmounted) |
| `mount_find(mount_point)` | Look up mount by exact mount point |
| `mount_resolve(path, rel_path)` | Find mount containing a path, returns relative path |
| `mount_get_root()` | Shortcut for `mount_find("/")` |
| `mount_get_entry(index)` | Get nth active mount entry |
| `mount_format_name(index, buf, len)` | Format mount name for display |

## VFS Path Resolution (`src/fs/vfs.c`)

The VFS dispatches file/directory operations through the mount table:

### CWD Tracking

- `vfs_cwd[VFS_MAX_CWD]` — current working directory string (starts as `"/"`)
- `vfs_chdir(path)` — resolve and update CWD (`".."` moves up, `"/"` resets);
  since 2026-07-11 the stored CWD is canonicalized to **upper-case** (FAT is
  case-insensitive and stores names upper-case), so `cd docs` yields `/DOCS`.
  Consumers like `SYSCALL_OPENDIR` match the CWD against 8.3 names and
  previously got the root listing after a lowercase `cd`.
- `vfs_getcwd(buf, len)` — copy CWD into caller buffer

### Path Resolution

`vfs_resolve(path)` uses prefix matching against the mount table:

1. If path starts with `"/"`, it's absolute — walk mount table entries for
   the longest prefix match
2. If path is relative, prepend CWD then resolve as absolute
3. Returns the mount entry and sets `rel_path` to the portion after the
   mount point

### Filesystem Operations

All VFS functions resolve the path, call the corresponding FAT16 function,
and propagate errors:

| Function | Fat16 Target |
|----------|--------------|
| `vfs_open(path, file, mode)` | `fat16_open` / `fat16_create` (if O_CREAT) |
| `vfs_unlink(path)` | `fat16_delete` |
| `vfs_rename(old, new)` | `fat16_rename` |
| `vfs_mkdir(path)` | `fat16_mkdir` |
| `vfs_rmdir(path)` | `fat16_rmdir` |

### Directory Listing

`vfs_ls(path)` prints a directory listing to serial output. It resolves the
path, then either:
- Calls `vfs_listdir(path)` for non-root directories (iterates mounted subdirectory)
- Iterates the root FAT16 directory directly for the root mount

## Syscall Wiring

The following syscalls in `syscalls.c` use the VFS layer:

| Syscall | Handler | Status |
|---------|---------|--------|
| `SYSCALL_CHDIR` (0x25) | Calls `vfs_chdir()` with path from `caller_ds:bx` | Complete |
| `SYSCALL_GETCWD` (0x26) | Calls `vfs_getcwd()`, writes to user buffer | Complete |
| `SYSCALL_MKDIR` (0x23) | Calls `vfs_mkdir()` via `name_to_83(caller_ds, bx)` | Complete |
| `SYSCALL_RMDIR` (0x24) | Calls `vfs_rmdir()` via `name_to_83(caller_ds, bx)` | Complete |
| `SYSCALL_MOUNT` (0x30) | Reads mount point from `caller_ds:bx`, drive from `cx`, LBA from `dx:si`, calls `mount_add()` | Complete |
| `SYSCALL_UNMOUNT` (0x31) | Reads mount point from `caller_ds:bx`, calls `mount_remove()` | Complete |
| `SYSCALL_LIST_MOUNTS` (0x32) | Iterates `mount_get_entry()`, prints each to serial | Complete |

## Key Files

- `src/fs/mount.c` — Mount table management (~155 lines)
- `src/fs/vfs.c` — VFS path resolution and dispatch (~360 lines)
- `src/include/fs/mount.h` — Mount table types and API
- `src/include/fs/vfs.h` — VFS API