---
title: FAT16 Driver
type: code-map
sources:
  - src/drivers/fat16.c
  - src/include/drivers/fat16.h
related:
  - "[[disk-driver]]"
  - "[[syscall-dispatch]]"
  - "[[plan]]"
created: 2026-07-05
updated: 2026-07-05
confidence: high
---

# FAT16 Driver

The FAT16 driver at `src/drivers/fat16.c` provides full read-only filesystem
access: mount, cluster chain traversal, root directory search, file open/read/seek,
and directory iteration (opendir/readdir/closedir).

## Interface (`src/include/drivers/fat16.h`)

### Key Types

| Type | Fields | Purpose |
|------|--------|---------|
| `fat16_fs_t` | drive, partition_lba, bytes_per_sector, sectors_per_cluster, reserved_sectors, fat_count, root_entries, fat_sectors, root_dir_lba, data_lba, total_clusters | Per-mount filesystem info (parsed from BPB) |
| `fat16_dirent_t` | name[8], ext[3], attrs, first_cluster, file_size | Raw 32-byte directory entry |
| `fat16_file_t` | fs, first_cluster, file_size, position, current_cluster, cluster_pos | Open file handle tracking read position |
| `fat16_dir_t` | fs, sector, entry_idx, abs_idx, root_sectors | Directory iterator state |

### Functions

| Function | Purpose |
|----------|---------|
| `fat16_mount(drive, partition_lba, fs)` | Parse BPB, compute FAT/root/data LBAs |
| `fat16_read_fat(fs, cluster)` | Read next cluster from FAT chain |
| `fat16_read_cluster(fs, cluster, sector_off, buffer)` | Read one sector from data area |
| `fat16_find(fs, name, dirent)` | Search root directory for 8.3 name |
| `fat16_open(fs, name, file)` | Initialize file handle from dirent |
| `fat16_read(file, buffer, count)` | Read bytes from file, following cluster chain |
| `fat16_seek(file, pos)` | Seek to arbitrary position in file |
| `fat16_write(file, buffer, count)` | Write bytes to file (extends if needed, allocates clusters) |
| `fat16_create(fs, name, file)` | Create new empty file in root directory |
| `fat16_delete(fs, name)` | Delete file, free cluster chain |
| `fat16_opendir(fs, dir)` | Initialize root directory iterator |
| `fat16_readdir(dir, dirent)` | Iterate through root directory entries |
| `fat16_closedir(dir)` | Reset directory iterator |
| `fat16_mkdir(fs, name)` | Create directory entry with `.`/`..` |
| `fat16_rmdir(fs, name)` | Remove empty directory (free cluster chain) |

## Implementation Details

### Mount

`fat16_mount` reads the boot sector at `partition_lba`, parses the BPB at
offset 0x0B, and computes:

```
root_dir_lba = partition_lba + reserved_sectors + (fat_count * fat_sectors)
data_lba = root_dir_lba + root_sectors
```

Where `root_sectors = (root_entries * 32 + 511) / 512`.

### FAT Cluster Chain

`fat16_read_fat` reads a 16-bit entry from the FAT at byte offset
`cluster * 2`. The FAT sector is located at:

```
fat_sector = partition_lba + reserved_sectors + (cluster * 2) / 512
```

End-of-chain markers: `0xFFF8` through `0xFFFF`.

### File Reading

`fat16_read` reads bytes from a file, tracking position in two levels:
- `current_cluster` — which cluster in the chain
- `cluster_pos` — byte offset within the current cluster

When `cluster_pos` reaches `cluster_size` (always 512 for our images), the
next cluster is fetched from the FAT. Each cluster is read one sector at a time
via `fat16_read_cluster`.

### Critical: SS≠DS during Syscall Handler

When `fat16_open`, `fat16_open_in_dir`, or any function that takes `&local_var`
is called from inside the INT 0x40 handler, the segment registers are split:
SS = COM_SEGMENT (0x4000, user stack) but DS = KERNEL_SEGMENT (0x2000).

OpenWatcom small model uses **near pointers** for everything. A near pointer to a
local stack variable yields its offset relative to BP (i.e. SS-relative). But
when the callee writes through that pointer (`*ptr = value`), it uses DS. So the
callee writes to KERNEL_SEGMENT:offset while the caller reads from
SS=COM_SEGMENT:offset — two different physical addresses.

**Affected functions (fixed by making locals static):**

| Function | Variables made static |
|----------|-----------------------|
| `fat16_open` (`src/drivers/fat16.c:283`) | `fat16_dirent_t dirent` |
| `fat16_open_in_dir` (`src/drivers/fat16.c:258-260`) | `fat16_dirent_t dirent`, `uint32_t sector_lba`, `uint16_t entry_offset` |
| `disk_read_sectors` (`src/drivers/disk.c`) | `dap_t dap` (BIOS INT 13h reads DAP at DS:SI) |

**Rule:** Any variable whose near address (`&var`) is passed to a callee must be
`static` (in the data segment, DS-relative) whenever that code path runs inside
the INT 0x40 handler (SS != DS). Large arrays used as read/write buffers have the
same requirement.

Also in `src/kernel/syscalls.c`, in the SYSCALL_EXEC case:

| Variable | Made static | Reason |
|----------|-------------|--------|
| `uint8_t name_83[12]` | yes | address passed to `name_to_83` |
| `fat16_file_t file` | yes | pointer passed to `fat16_open_in_dir` |
| `fat16_dirent_t bin_dirent` | yes | pointer passed to `fat16_find` |
| `uint8_t with_ext[12]` | yes | address passed to `fat16_open` |
| `uint8_t tmp[512]` | yes | address passed to `fat16_read` |

### Watch Out: `__U4D` in OpenWatcom Small Model

OpenWatcom's small model (`-ms`) does **not** provide `__U4D` / `__U4M`
runtime helpers for 32-bit division/modulo operations. Using `uint32_t / anything`
or `uint32_t % anything` produces unresolved symbol errors at link time.

**Fix:** Since `bytes_per_sector` is always 512 (a power of 2, verified at
mount time), replace division with right-shift and modulo with bitwise AND:

```c
// BAD — generates __U4D reference:
sector_in_cluster = (uint16_t)(cluster_pos / 512);

// GOOD — uses shift:
sector_in_cluster = (uint16_t)(cluster_pos >> 9);
offset_in_sector = (uint16_t)(cluster_pos & 0x1FF);
```

`fat16_seek` already uses shifts for the same reason (`target_cluster_idx = pos >> 9`,
`cluster_pos = pos & 0x1FF`).

### File Writing

`fat16_write` follows the same cluster-chain logic as `fat16_read`, but also:

1. **Allocates clusters** when the current one is exhausted: calls
   `fat16_find_free_cluster` (linear scan of FAT for `FAT16_FREE` entries),
   then links the new cluster with `fat16_write_fat`.
2. **Read-modify-write** each sector: reads the target sector into `sec_buf`,
   copies user data into it, then writes back with `fat16_write_cluster`.
3. **Updates file size** in the directory entry whenever the write extends
   the file, using `fat16_update_fsize` (reads the directory sector, writes
   the 4-byte size field at offset 28 of the entry).

### File Creation and Deletion

`fat16_create` scans the root directory for a free slot (deleted entry `0xE5`
or end marker `0x00`), allocates a cluster from the FAT, writes a new
directory entry (name, extension, archive attribute, first cluster, size=0),
and initializes the file handle.

`fat16_delete` finds the directory entry, marks its first byte as `0xE5`
(deleted), then calls `fat16_free_chain` to walk the cluster chain and mark
each cluster as `FAT16_FREE`.

### Internal Helpers

| Function | Purpose |
|----------|---------|
| `fat16_write_fat(fs, cluster, value)` | Read FAT sector, modify one entry, write back |
| `fat16_find_free_cluster(fs)` | Linear scan of FAT for free cluster (cluster >= 2) |
| `fat16_write_cluster(fs, cluster, sector_off, buffer)` | Write one sector to data area |
| `fat16_free_chain(fs, first_cluster)` | Walk cluster chain and free all clusters |
| `fat16_update_fsize(file)` | Update directory entry's file_size field |

### Directory Iteration

`fat16_opendir` initializes the iterator pointing at the first sector of the
root directory. `fat16_readdir` reads one sector at a time (16 entries per
sector) and skips deleted (`0xE5`), LFN (`attrs == 0x0F`), and volume label
entries. Returns `ERR_NOT_FOUND` at end of directory.

## Syscall Wiring

The file and directory syscalls are wired through `syscalls.c`:

| Syscall | Handler | Status |
|---------|---------|--------|
| `SYSCALL_OPEN` (0x10) | Converts C string to 8.3, allocates slot in open file table, returns handle | Complete |
| `SYSCALL_CLOSE` (0x11) | Frees open file table slot | Complete |
| `SYSCALL_READ` (0x12) | Reads from open file handle | Complete |
| `SYSCALL_SEEK` (0x14) | Seeks open file handle | Complete |
| `SYSCALL_WRITE` (0x13) | Writes to open file handle (extends file, allocates clusters) | Complete |
| `SYSCALL_STAT` (0x17) | Looks up directory entry by name | Complete |
| `SYSCALL_OPENDIR` (0x20) | Initializes user-provided `fat16_dir_t` | Complete |
| `SYSCALL_READDIR` (0x21) | Reads next directory entry | Complete |
| `SYSCALL_CLOSEDIR` (0x22) | Resets directory iterator | Complete |

The open file table (`syscalls.c:14-21`) supports up to 16 simultaneous open
files. SYSCALL_CREATE creates a new file, allocates a slot, and returns a
handle; SYSCALL_DELETE removes the file by name.

The root filesystem is mounted once during `syscall_init()` at partition
LBA 63 (first partition on disk).

## Key Files

- `src/drivers/fat16.c` — Implementation (~390 lines)
- `src/include/drivers/fat16.h` — Header with types and API (~115 lines)
- `src/kernel/syscalls.c` — Syscall dispatch for file/directory operations (~240 lines)