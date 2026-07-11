#ifndef DRIVERS_FAT16_H
#define DRIVERS_FAT16_H

#include "types.h"

/* ─── FAT16 Filesystem Info (per-mount) ─── */
typedef struct {
    uint8_t  drive;              /* BIOS drive number (0x80) */
    uint32_t partition_lba;      /* Partition start LBA */
    uint16_t bytes_per_sector;   /* Usually 512 */
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entries;
    uint16_t fat_sectors;        /* Sectors per FAT */
    uint32_t root_dir_lba;       /* LBA of root directory start */
    uint32_t data_lba;           /* LBA of data area start */
    uint16_t total_clusters;
} fat16_fs_t;

/* ─── Raw Directory Entry (32 bytes) ─── */
typedef struct {
    uint8_t  name[8];
    uint8_t  ext[3];
    uint8_t  attrs;
    uint8_t  reserved[10];
    uint16_t time;
    uint16_t date;
    uint16_t first_cluster;
    uint32_t file_size;
} fat16_dirent_t;

/* ─── Open File Handle ─── */
typedef struct {
    fat16_fs_t *fs;
    uint16_t    first_cluster;
    uint32_t    file_size;
    uint32_t    position;
    uint16_t    current_cluster;
    uint32_t    cluster_pos;     /* bytes read from current cluster */
    uint32_t    dir_sector_lba;  /* LBA of directory sector with this file's entry */
    uint16_t    dir_entry_offset;/* byte offset within that sector (multiple of 32) */
} fat16_file_t;

/* ─── Attribute Constants ─── */
#define FAT16_ATTR_READONLY  0x01
#define FAT16_ATTR_HIDDEN    0x02
#define FAT16_ATTR_SYSTEM    0x04
#define FAT16_ATTR_VOLUME    0x08
#define FAT16_ATTR_DIRECTORY 0x10
#define FAT16_ATTR_ARCHIVE   0x20
#define FAT16_ATTR_LFN       0x0F   /* Long File Name entry */

/* ─── FAT16 Special Cluster Values ─── */
#define FAT16_FREE      0x0000
#define FAT16_RESERVED  0x0001
#define FAT16_BAD       0xFFF7
#define FAT16_EOF_MIN   0xFFF8
#define FAT16_EOF_MAX   0xFFFF

/* ─── Directory Handle ─── */
typedef struct {
    fat16_fs_t *fs;
    uint16_t    sector;          /* current sector in root dir (or sector offset in cluster for subdir) */
    uint16_t    entry_idx;       /* entry within current sector (0-15) */
    uint16_t    abs_idx;         /* absolute entry index */
    uint16_t    root_sectors;    /* cached number of root sectors (0 for subdirs) */
    uint16_t    dir_cluster;     /* 0 = root dir, >=2 = starting cluster of subdir */
    uint16_t    current_cluster; /* current cluster in subdir chain (0 for root) */
} fat16_dir_t;

/* ─── API ─── */

/* Open root directory for iteration.
 * Returns 0 on success. */
uint16_t fat16_opendir(fat16_fs_t *fs, fat16_dir_t *dir);

/* Open a subdirectory by its first cluster for iteration.
 * cluster must be >= 2. Returns 0 on success. */
uint16_t fat16_opendir_cluster(fat16_fs_t *fs, uint16_t cluster, fat16_dir_t *dir);

/* Read next directory entry. Returns 0 on success, ERR_NOT_FOUND at end. */
uint16_t fat16_readdir(fat16_dir_t *dir, fat16_dirent_t *dirent);

/* Close directory handle (resets state). */
void fat16_closedir(fat16_dir_t *dir);

/* Mount a FAT16 partition: parse BPB and compute layout.
 * Returns 0 on success, nonzero on error. */
uint16_t fat16_mount(uint8_t drive, uint32_t partition_lba, fat16_fs_t *fs);

/* Read a FAT entry for a given cluster. Returns next cluster or
 * FAT16_EOF_MIN..FAT16_EOF_MAX if end-of-chain, FAT16_BAD if bad. */
uint16_t fat16_read_fat(fat16_fs_t *fs, uint16_t cluster);

/* Read one sector from the data area.
 * cluster: cluster number (2-based).
 * sector_off: offset within cluster (0 to sectors_per_cluster-1).
 * buffer: 512-byte output buffer.
 * Returns 0 on success. */
uint16_t fat16_read_cluster(fat16_fs_t *fs, uint16_t cluster,
                            uint16_t sector_off, uint8_t *buffer);

/* Find a file in root directory. name is 8.3 format ("FILE    TXT").
 * dirent receives the raw directory entry.
 * If out_sector_lba and out_entry_offset are non-NULL, they receive the
 * location of the directory entry on disk (for updating it later).
 * Returns 0 on success, ERR_NOT_FOUND if not found. */
uint16_t fat16_find(fat16_fs_t *fs, const uint8_t *name,
                     fat16_dirent_t *dirent,
                     uint32_t *out_sector_lba, uint16_t *out_entry_offset);

/* Find a file in a subdirectory. dir_cluster is the first cluster of
 * the subdirectory data area. name is 8.3 format.
 * Returns 0 on success, ERR_NOT_FOUND if not found. */
uint16_t fat16_find_in_dir(fat16_fs_t *fs, uint16_t dir_cluster,
                           const uint8_t *name,
                           fat16_dirent_t *dirent,
                           uint32_t *out_sector_lba,
                           uint16_t *out_entry_offset);

/* Open a file in a subdirectory by name (8.3 format).
 * dir_cluster is the first cluster of the subdirectory.
 * Returns 0 on success. */
uint16_t fat16_open_in_dir(fat16_fs_t *fs, uint16_t dir_cluster,
                           const uint8_t *name, fat16_file_t *file);

/* Open a file by 8.3 name. Initializes the file handle.
 * Returns 0 on success. */
uint16_t fat16_open(fat16_fs_t *fs, const uint8_t *name,
                     fat16_file_t *file);

/* Read up to count bytes from an open file into buffer.
 * Returns number of bytes read (may be less than count at EOF). */
uint16_t fat16_read(fat16_file_t *file, uint8_t *buffer, uint16_t count);

/* Seek to a position in an open file.
 * Returns 0 on success. */
uint16_t fat16_seek(fat16_file_t *file, uint32_t pos);

/* Write up to count bytes from buffer to an open file at current position.
 * Extends the file if necessary (allocates clusters).
 * Returns number of bytes written (may be less than count if disk full). */
uint16_t fat16_write(fat16_file_t *file, const uint8_t *buffer, uint16_t count);

/* Create a new empty file in root directory.
 * name is 8.3 format ("FILE    TXT").
 * file receives the initialized file handle.
 * Returns 0 on success, ERR_FILE_EXISTS if name already taken,
 * ERR_NO_MEMORY if directory is full or no free clusters. */
uint16_t fat16_create(fat16_fs_t *fs, const uint8_t *name,
                       fat16_file_t *file);

/* Delete a file from root directory and free its cluster chain.
 * name is 8.3 format ("FILE    TXT").
 * Returns 0 on success, ERR_NOT_FOUND if not found. */
uint16_t fat16_delete(fat16_fs_t *fs, const uint8_t *name);

/* Create a directory in root directory.
 * name is 8.3 format ("DIRNAME    " — no extension).
 * Returns 0 on success, ERR_FILE_EXISTS or ERR_NO_MEMORY on failure. */
uint16_t fat16_mkdir(fat16_fs_t *fs, const uint8_t *name);

/* Remove an empty directory.
 * name is 8.3 format.
 * Returns 0 on success, ERR_NOT_FOUND or ERR_ACCESS_DENIED if not empty. */
uint16_t fat16_rmdir(fat16_fs_t *fs, const uint8_t *name);

/* Rename a file or directory.
 * old_name and new_name are 8.3 format.
 * Returns 0 on success, ERR_NOT_FOUND or ERR_FILE_EXISTS on failure. */
uint16_t fat16_rename(fat16_fs_t *fs, const uint8_t *old_name,
                       const uint8_t *new_name);

#endif /* DRIVERS_FAT16_H */