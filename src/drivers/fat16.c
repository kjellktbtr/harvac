/* fat16.c -- FAT16 filesystem driver
 *
 * Implements: mount, cluster chain walk, root directory search,
 * file open/read/seek.
 */

#include "kernel.h"
#include "drivers/disk.h"
#include "drivers/fat16.h"

/* BPB field offsets within a boot sector (relative to sector start) */
#define BPB_BYTES_PER_SEC    0x0B  /* uint16_t */
#define BPB_SEC_PER_CLUST    0x0D  /* uint8_t */
#define BPB_RESERVED_SEC     0x0E  /* uint16_t */
#define BPB_FAT_COUNT        0x10  /* uint8_t */
#define BPB_ROOT_ENTRIES     0x11  /* uint16_t */
#define BPB_TOTAL_SEC_16     0x13  /* uint16_t */
#define BPB_MEDIA            0x15  /* uint8_t */
#define BPB_FAT_SECS         0x16  /* uint16_t */
#define BPB_SEC_PER_TRK      0x18  /* uint16_t */
#define BPB_HEADS            0x1A  /* uint16_t */
#define BPB_HIDDEN_SEC       0x1C  /* uint32_t */
#define BPB_TOTAL_SEC_32     0x20  /* uint32_t */

/* Single 512-byte sector buffer for all FAT16 I/O.
 * Must be in kernel data segment so DAP can address it. */
static uint8_t sec_buf[512];

/* ─── Mount: parse BPB, compute filesystem layout ─── */
uint16_t fat16_mount(uint8_t drive, uint32_t partition_lba, fat16_fs_t *fs)
{
    /* Read the boot sector (contains BPB) */
    if (disk_read_sectors(drive, partition_lba, 1, KERNEL_SEGMENT,
                          (uint16_t)sec_buf) != 0)
        return 1;

    /* Parse BPB at byte offset 0x0B */
    {
        uint8_t *bpb = sec_buf + 11;        /* shortcut to BPB start */
        fs->drive = drive;
        fs->partition_lba = partition_lba;
        fs->bytes_per_sector = *(uint16_t *)(bpb + 0);
        fs->sectors_per_cluster = *(bpb + 2);
        fs->reserved_sectors = *(uint16_t *)(bpb + 3);
        fs->fat_count = *(bpb + 5);
        fs->root_entries = *(uint16_t *)(bpb + 6);
        fs->fat_sectors = *(uint16_t *)(bpb + 11);
    }
    if (fs->bytes_per_sector != 512)
        return 2;

    /* Compute derived locations */
    {
        uint16_t root_sectors;

        root_sectors = (uint16_t)((fs->root_entries * 32 +
                                   fs->bytes_per_sector - 1) /
                                  fs->bytes_per_sector);

        fs->root_dir_lba = partition_lba
                           + fs->reserved_sectors
                           + (uint32_t)(fs->fat_count * fs->fat_sectors);

        fs->data_lba = fs->root_dir_lba + root_sectors;

        /* Estimate total clusters from partition size */
        {
            uint32_t data_sectors;
            uint32_t total_sec;
            uint32_t hidden;

            /* Read hidden sectors and total sectors from BPB */
            {
                uint8_t *bpb = sec_buf + 11;
                hidden = *(uint32_t *)(bpb + 17);
                total_sec = *(uint32_t *)(bpb + 21);
            }

            data_sectors = total_sec - (fs->data_lba - hidden);
            /* sectors_per_cluster is always 1 for our images */
            fs->total_clusters = (uint16_t)data_sectors;
        }
    }

    return 0;
}

/* ─── Read a FAT entry ─── */
uint16_t fat16_read_fat(fat16_fs_t *fs, uint16_t cluster)
{
    uint32_t fat_offset;       /* byte offset within FAT */
    uint32_t fat_sector_lba;   /* absolute LBA of FAT sector */
    uint16_t entry_in_sector;  /* byte offset within the 512-byte sector */

    /* FAT16: each entry is 2 bytes */
    fat_offset = (uint32_t)cluster * 2;
    /* bytes_per_sector is always 512 (power of 2) — use shift */
    fat_sector_lba = fs->partition_lba + fs->reserved_sectors
                     + (fat_offset >> 9);
    entry_in_sector = (uint16_t)(fat_offset & 0x1FF);

    if (disk_read_sectors(fs->drive, fat_sector_lba, 1, KERNEL_SEGMENT,
                          (uint16_t)sec_buf) != 0)
        return FAT16_BAD;

    return *(uint16_t *)(sec_buf + entry_in_sector);
}

/* ─── Count free clusters by scanning FAT sector by sector ─── */
/* 32 disk reads (one per FAT sector) instead of ~8000 (one per cluster). */
uint16_t fat16_count_free_clusters(fat16_fs_t *fs)
{
    uint16_t fat_sector;
    uint32_t fat_lba = fs->partition_lba + fs->reserved_sectors;
    uint16_t free_count = 0;
    uint16_t cl_base = 0;  /* cluster index of first entry in this sector */

    for (fat_sector = 0; fat_sector < fs->fat_sectors; fat_sector++) {
        uint16_t i;
        if (disk_read_sectors(fs->drive, fat_lba + fat_sector, 1,
                              KERNEL_SEGMENT, (uint16_t)sec_buf) != 0)
            break;
        /* Each 512-byte sector holds 256 FAT16 entries */
        for (i = 0; i < 256; i++) {
            uint16_t cl = cl_base + i;
            if (cl < 2) continue;              /* entries 0 and 1 are reserved */
            if (cl >= fs->total_clusters + 2) goto done;
            if (*(uint16_t *)(sec_buf + i * 2) == FAT16_FREE)
                free_count++;
        }
        cl_base += 256;
    }
done:
    return free_count;
}

/* ─── Read one sector from a cluster ─── */
uint16_t fat16_read_cluster(fat16_fs_t *fs, uint16_t cluster,
                            uint16_t sector_off, uint8_t *buffer)
{
    uint32_t lba;

    if (cluster < 2)
        return 1;

    /* sectors_per_cluster is always 1 for our images */
    lba = fs->data_lba + (uint32_t)(cluster - 2) + sector_off;

    return disk_read_sectors(fs->drive, lba, 1, KERNEL_SEGMENT,
                             (uint16_t)buffer);
}

/* ─── Compare 8.3 filename ─── */
static uint8_t name_match(const uint8_t *name, const uint8_t *entry)
{
    uint8_t i;

    /* Check 8-byte name */
    for (i = 0; i < 8; i++) {
        if (name[i] == ' ' && entry[i] == ' ')
            break;
        if (name[i] != entry[i])
            return 0;
    }
    /* Check 3-byte extension */
    for (i = 0; i < 3; i++) {
        uint8_t nc = name[8 + i];
        uint8_t ec = entry[8 + i];
        if (nc == ' ' && ec == ' ')
            break;
        if (nc != ec)
            return 0;
    }
    return 1;
}

/* ─── Find a file in root directory ─── */
uint16_t fat16_find(fat16_fs_t *fs, const uint8_t *name,
                     fat16_dirent_t *dirent,
                     uint32_t *out_sector_lba, uint16_t *out_entry_offset)
{
    uint16_t entry_count;
    uint16_t i;
    uint16_t root_sectors;
    uint16_t sec;

    entry_count = fs->root_entries;
    root_sectors = (uint16_t)((entry_count * 32 +
                               fs->bytes_per_sector - 1)
                              / fs->bytes_per_sector);

    for (sec = 0; sec < root_sectors; sec++) {
        if (disk_read_sectors(fs->drive, fs->root_dir_lba + sec, 1,
                              KERNEL_SEGMENT, (uint16_t)sec_buf) != 0)
            return 1;

        /* Scan 16 entries per sector (512 / 32) */
        for (i = 0; i < 16; i++) {
            fat16_dirent_t *de = (fat16_dirent_t *)(sec_buf + i * 32);

            /* Skip empty (0x00 = end, no more entries in this dir) */
            if (de->name[0] == 0x00)
                return ERR_NOT_FOUND;

            /* Skip deleted (0xE5) and LFN entries */
            if (de->name[0] == 0xE5)
                continue;
            if (de->attrs == FAT16_ATTR_LFN)
                continue;
            /* Skip volume label */
            if (de->attrs & FAT16_ATTR_VOLUME)
                continue;

            if (name_match(name, de->name)) {
                /* Found */
                *dirent = *de;
                if (out_sector_lba)
                    *out_sector_lba = fs->root_dir_lba + sec;
                if (out_entry_offset)
                    *out_entry_offset = (uint16_t)(i * 32);
                return 0;
            }
        }
    }

    return ERR_NOT_FOUND;
}

/* ─── Find a file in a subdirectory (by cluster) ─── */
/* Searches the directory cluster chain for name (8.3 format).
 * If out_sector_lba and out_entry_offset are non-NULL, they receive
 * the location of the directory entry on disk (for updates).
 * Returns 0 on success, ERR_NOT_FOUND if not found. */
uint16_t fat16_find_in_dir(fat16_fs_t *fs, uint16_t dir_cluster,
                           const uint8_t *name,
                           fat16_dirent_t *dirent,
                           uint32_t *out_sector_lba,
                           uint16_t *out_entry_offset)
{
    uint16_t cluster = dir_cluster;

    /* dir_cluster 0 = root directory (fixed area, not a cluster chain) */
    if (dir_cluster == 0)
        return fat16_find(fs, name, dirent, out_sector_lba, out_entry_offset);

    while (cluster >= 2 && cluster < FAT16_EOF_MIN) {
        uint32_t sector_lba = fs->data_lba + (uint32_t)(cluster - 2);
        uint16_t i;

        if (disk_read_sectors(fs->drive, sector_lba, 1,
                              KERNEL_SEGMENT, (uint16_t)sec_buf) != 0)
            return ERR_DISK_ERROR;

        for (i = 0; i < 16; i++) {
            fat16_dirent_t *de = (fat16_dirent_t *)(sec_buf + i * 32);

            if (de->name[0] == 0x00)
                return ERR_NOT_FOUND;

            if (de->name[0] == 0xE5)
                continue;
            if (de->attrs == FAT16_ATTR_LFN)
                continue;
            if (de->attrs & FAT16_ATTR_VOLUME)
                continue;

            if (name_match(name, de->name)) {
                *dirent = *de;
                if (out_sector_lba)
                    *out_sector_lba = sector_lba;
                if (out_entry_offset)
                    *out_entry_offset = (uint16_t)(i * 32);
                return 0;
            }
        }

        /* Follow cluster chain */
        cluster = fat16_read_fat(fs, cluster);
    }

    return ERR_NOT_FOUND;
}

/* ─── Open a file in a subdirectory ─── */
/* Given a directory's first cluster and an 8.3 name, finds the file
 * and initializes a fat16_file_t handle. Returns 0 on success. */
uint16_t fat16_open_in_dir(fat16_fs_t *fs, uint16_t dir_cluster,
                           const uint8_t *name, fat16_file_t *file)
{
    /* Static: these receive near-pointer writes from callees.
     * SS=COM_SEGMENT during syscalls; static puts them in DS=KERNEL_SEGMENT
     * so callee writes and our reads address the same memory. */
    static fat16_dirent_t dirent;
    static uint32_t sector_lba;
    static uint16_t entry_offset;
    uint16_t ret;

    ret = fat16_find_in_dir(fs, dir_cluster, name, &dirent,
                            &sector_lba, &entry_offset);
    if (ret != 0)
        return ret;

    file->fs = fs;
    file->first_cluster = dirent.first_cluster;
    file->file_size = dirent.file_size;
    file->current_cluster = dirent.first_cluster;
    file->position = 0;
    file->cluster_pos = 0;
    file->dir_sector_lba = sector_lba;
    file->dir_entry_offset = entry_offset;
    return 0;
}

/* ─── Open a file ─── */
uint16_t fat16_open(fat16_fs_t *fs, const uint8_t *name,
                     fat16_file_t *file)
{
    /* Static: receives near-pointer write from fat16_find; same SS/DS issue. */
    static fat16_dirent_t dirent;
    uint16_t ret;

    ret = fat16_find(fs, name, &dirent,
                     &file->dir_sector_lba, &file->dir_entry_offset);
    if (ret != 0)
        return ret;

    file->fs = fs;
    file->first_cluster = dirent.first_cluster;
    file->file_size = dirent.file_size;
    file->current_cluster = dirent.first_cluster;
    file->position = 0;
    file->cluster_pos = 0;

    return 0;
}

/* ─── Open root directory for iteration ─── */
uint16_t fat16_opendir(fat16_fs_t *fs, fat16_dir_t *dir)
{
    dir->fs = fs;
    dir->sector = 0;
    dir->entry_idx = 0;
    dir->abs_idx = 0;
    dir->root_sectors = (uint16_t)((fs->root_entries * 32
                                    + fs->bytes_per_sector - 1)
                                   / fs->bytes_per_sector);
    dir->dir_cluster = 0;
    dir->current_cluster = 0;
    return 0;
}

/* ─── Open subdirectory by first cluster ─── */
uint16_t fat16_opendir_cluster(fat16_fs_t *fs, uint16_t cluster, fat16_dir_t *dir)
{
    if (cluster < 2)
        return 1;
    dir->fs = fs;
    dir->sector = 0;
    dir->entry_idx = 0;
    dir->abs_idx = 0;
    dir->root_sectors = 0;
    dir->dir_cluster = cluster;
    dir->current_cluster = cluster;
    return 0;
}

/* ─── Read next directory entry ─── */
uint16_t fat16_readdir(fat16_dir_t *dir, fat16_dirent_t *dirent)
{
    fat16_fs_t *fs = dir->fs;

    if (dir->dir_cluster == 0) {
        /* Root directory iteration */
        while (dir->sector < dir->root_sectors) {
            if (dir->entry_idx == 0) {
                if (disk_read_sectors(fs->drive, fs->root_dir_lba + dir->sector,
                                      1, KERNEL_SEGMENT, (uint16_t)sec_buf) != 0)
                    return ERR_DISK_ERROR;
            }

            while (dir->entry_idx < 16) {
                fat16_dirent_t *de = (fat16_dirent_t *)(sec_buf
                                                         + dir->entry_idx * 32);

                dir->abs_idx++;
                dir->entry_idx++;

                if (de->name[0] == 0x00)
                    return ERR_NOT_FOUND;
                if (de->name[0] == 0xE5)
                    continue;
                if (de->attrs == FAT16_ATTR_LFN)
                    continue;
                if (de->attrs & FAT16_ATTR_VOLUME)
                    continue;

                *dirent = *de;
                return 0;
            }

            dir->sector++;
            dir->entry_idx = 0;
        }
    } else {
        /* Subdirectory iteration: read cluster chain */
        uint16_t cluster_size;
        uint16_t max_entries;

        cluster_size = (uint16_t)fs->sectors_per_cluster * fs->bytes_per_sector;
        max_entries = cluster_size / 32;

        while (dir->current_cluster >= 2 && dir->current_cluster < FAT16_EOF_MIN) {
            /* If we've read all entries in the current cluster, move to next */
            if (dir->entry_idx >= max_entries) {
                dir->current_cluster = fat16_read_fat(fs, dir->current_cluster);
                dir->entry_idx = 0;
                dir->sector = 0;
                continue;
            }

            /* Read current sector (1 sector per cluster in our setup) */
            if (fat16_read_cluster(fs, dir->current_cluster, dir->sector, sec_buf) != 0)
                return ERR_DISK_ERROR;

            while (dir->entry_idx < max_entries && dir->sector < fs->sectors_per_cluster) {
                fat16_dirent_t *de = (fat16_dirent_t *)(sec_buf
                                                         + dir->entry_idx * 32);

                dir->abs_idx++;
                dir->entry_idx++;

                if (de->name[0] == 0x00)
                    return ERR_NOT_FOUND;
                if (de->name[0] == 0xE5)
                    continue;
                if (de->attrs == FAT16_ATTR_LFN)
                    continue;
                if (de->attrs & FAT16_ATTR_VOLUME)
                    continue;

                *dirent = *de;
                return 0;
            }

            /* Move to next sector in cluster */
            dir->sector++;
            dir->entry_idx = 0;
        }
    }

    return ERR_NOT_FOUND;
}

/* ─── Close directory handle ─── */
void fat16_closedir(fat16_dir_t *dir)
{
    dir->sector = 0;
    dir->entry_idx = 0;
    dir->abs_idx = 0;
}
uint16_t fat16_read(fat16_file_t *file, uint8_t *buffer, uint16_t count)
{
    uint16_t total_read;
    uint16_t cluster_size;

    cluster_size = (uint16_t)file->fs->sectors_per_cluster
                   * file->fs->bytes_per_sector;
    total_read = 0;

    while (count > 0) {
        uint16_t chunk;
        uint16_t remaining_in_cluster;
        uint16_t to_eof;

        /* Check for EOF */
        if (file->position >= file->file_size)
            break;

        /* Check if we need to move to next cluster */
        if (file->cluster_pos >= cluster_size) {
            file->current_cluster = fat16_read_fat(file->fs,
                                                   file->current_cluster);
            if (file->current_cluster >= FAT16_EOF_MIN) {
                /* End of cluster chain */
                file->cluster_pos = cluster_size;  /* prevent re-read */
                break;
            }
            file->cluster_pos = 0;
        }

        /* How many bytes available in current cluster */
        remaining_in_cluster = cluster_size - (uint16_t)file->cluster_pos;

        /* How many bytes until EOF */
        to_eof = (uint16_t)(file->file_size - file->position);

        /* Read one cluster at a time via sector reads */
        {
            uint16_t sector_in_cluster;
            uint16_t offset_in_sector;
            uint16_t sec;

            sector_in_cluster = (uint16_t)(file->cluster_pos >> 9);
            offset_in_sector = (uint16_t)(file->cluster_pos & 0x1FF);

            /* Read the sector containing the data */
            sec = sector_in_cluster;
            if (fat16_read_cluster(file->fs, file->current_cluster,
                                   sec, sec_buf) != 0)
                return total_read;

            /* Copy bytes from sector buffer to output */
            chunk = file->fs->bytes_per_sector - offset_in_sector;
            if (chunk > to_eof)
                chunk = to_eof;
            if (chunk > count)
                chunk = count;

            {
                uint16_t j;
                for (j = 0; j < chunk; j++)
                    buffer[j] = sec_buf[offset_in_sector + j];
            }

            buffer += chunk;
            total_read += chunk;
            file->position += chunk;
            file->cluster_pos += chunk;
            count -= chunk;
        }
    }

    return total_read;
}

/* ─── Seek to a position in an open file ─── */
uint16_t fat16_seek(fat16_file_t *file, uint32_t pos)
{
    if (pos > file->file_size)
        return 1;

    /* cluster_size is always 512 (2^9) — use shift */
    if (pos == 0) {
        file->current_cluster = file->first_cluster;
        file->position = 0;
        file->cluster_pos = 0;
    } else {
        uint32_t target_cluster_idx;
        uint16_t cluster;
        uint16_t i;

        target_cluster_idx = pos >> 9;          /* pos / 512 */
        cluster = file->first_cluster;

        for (i = 0; i < target_cluster_idx; i++) {
            cluster = fat16_read_fat(file->fs, cluster);
            if (cluster >= FAT16_EOF_MIN)
                break;
        }

        file->current_cluster = cluster;
        file->position = pos;
        file->cluster_pos = (uint16_t)(pos & 0x1FF);  /* pos % 512 */
    }

    return 0;
}

/* ─── Write a FAT entry ─── */
static uint16_t fat16_write_fat(fat16_fs_t *fs, uint16_t cluster, uint16_t value)
{
    uint32_t fat_offset;
    uint32_t fat_sector_lba;
    uint16_t entry_in_sector;

    fat_offset = (uint32_t)cluster * 2;
    fat_sector_lba = fs->partition_lba + fs->reserved_sectors
                     + (fat_offset >> 9);
    entry_in_sector = (uint16_t)(fat_offset & 0x1FF);

    /* Read the FAT sector */
    if (disk_read_sectors(fs->drive, fat_sector_lba, 1, KERNEL_SEGMENT,
                          (uint16_t)sec_buf) != 0)
        return 1;

    /* Modify the entry */
    *(uint16_t *)(sec_buf + entry_in_sector) = value;

    /* Write back */
    return disk_write_sectors(fs->drive, fat_sector_lba, 1, KERNEL_SEGMENT,
                              (uint16_t)sec_buf);
}

/* ─── Find a free cluster in the FAT ─── */
static uint16_t fat16_find_free_cluster(fat16_fs_t *fs)
{
    uint16_t cluster;

    for (cluster = 2; cluster < fs->total_clusters; cluster++) {
        if (fat16_read_fat(fs, cluster) == FAT16_FREE)
            return cluster;
    }
    return FAT16_BAD;
}

/* ─── Write one sector to the data area ─── */
static uint16_t fat16_write_cluster(fat16_fs_t *fs, uint16_t cluster,
                                     uint16_t sector_off, const uint8_t *buffer)
{
    uint32_t lba;

    if (cluster < 2)
        return 1;

    lba = fs->data_lba + (uint32_t)(cluster - 2) + sector_off;

    return disk_write_sectors(fs->drive, lba, 1, KERNEL_SEGMENT,
                              (uint16_t)buffer);
}

/* ─── Free a cluster chain ─── */
static void fat16_free_chain(fat16_fs_t *fs, uint16_t first_cluster)
{
    uint16_t cluster;
    uint16_t next;

    cluster = first_cluster;
    while (cluster < FAT16_EOF_MIN && cluster >= 2) {
        next = fat16_read_fat(fs, cluster);
        fat16_write_fat(fs, cluster, FAT16_FREE);
        cluster = next;
    }
}

/* ─── Update file size in directory entry ─── */
static uint16_t fat16_update_fsize(fat16_file_t *file)
{
    /* Read the directory sector, modify the 4-byte file_size field at
     * offset 28 within the 32-byte entry, write back. */
    if (disk_read_sectors(file->fs->drive, file->dir_sector_lba, 1,
                          KERNEL_SEGMENT, (uint16_t)sec_buf) != 0)
        return 1;

    *(uint32_t *)(sec_buf + file->dir_entry_offset + 28) = file->file_size;

    return disk_write_sectors(file->fs->drive, file->dir_sector_lba, 1,
                              KERNEL_SEGMENT, (uint16_t)sec_buf);
}

/* ─── Write to an open file ─── */
uint16_t fat16_write(fat16_file_t *file, const uint8_t *buffer, uint16_t count)
{
    uint16_t total_written;
    uint16_t cluster_size;

    cluster_size = (uint16_t)file->fs->sectors_per_cluster
                   * file->fs->bytes_per_sector;
    total_written = 0;

    while (count > 0) {
        uint16_t chunk;
        uint16_t remaining_in_cluster;
        uint16_t sector_in_cluster;
        uint16_t offset_in_sector;

        /* Check if we need to move to next cluster */
        if (file->cluster_pos >= cluster_size) {
            uint16_t next;

            next = fat16_read_fat(file->fs, file->current_cluster);
            if (next >= FAT16_EOF_MIN) {
                /* Allocate a new cluster */
                uint16_t new_cluster;

                new_cluster = fat16_find_free_cluster(file->fs);
                if (new_cluster == FAT16_BAD)
                    break;  /* disk full */

                fat16_write_fat(file->fs, file->current_cluster, new_cluster);
                fat16_write_fat(file->fs, new_cluster, FAT16_EOF_MIN);
                file->current_cluster = new_cluster;
            } else {
                file->current_cluster = next;
            }
            file->cluster_pos = 0;
        }

        remaining_in_cluster = cluster_size - (uint16_t)file->cluster_pos;

        /* Read the current sector, modify, write back */
        sector_in_cluster = (uint16_t)(file->cluster_pos >> 9);
        offset_in_sector = (uint16_t)(file->cluster_pos & 0x1FF);

        if (fat16_read_cluster(file->fs, file->current_cluster,
                               sector_in_cluster, sec_buf) != 0)
            break;

        chunk = file->fs->bytes_per_sector - offset_in_sector;
        if (chunk > remaining_in_cluster)
            chunk = remaining_in_cluster;
        if (chunk > count)
            chunk = count;

        {
            uint16_t j;
            for (j = 0; j < chunk; j++)
                sec_buf[offset_in_sector + j] = buffer[j];
        }

        if (fat16_write_cluster(file->fs, file->current_cluster,
                                 sector_in_cluster, sec_buf) != 0)
            break;

        buffer += chunk;
        total_written += chunk;
        file->position += chunk;
        file->cluster_pos += chunk;
        count -= chunk;

        if (file->position > file->file_size) {
            file->file_size = file->position;
            fat16_update_fsize(file);
        }
    }

    return total_written;
}

/* ─── Find (or make) a free directory entry slot ─── */
/* dir_cluster 0 = root (fixed area; a full root fails with ERR_NO_MEMORY).
 * For subdirectories the cluster chain is extended with a zeroed cluster
 * when no free slot exists.
 * Postcondition on success: sec_buf holds the sector at *out_sector_lba. */
static uint16_t fat16_dir_free_slot(fat16_fs_t *fs, uint16_t dir_cluster,
                                    uint32_t *out_sector_lba,
                                    uint16_t *out_entry_offset)
{
    uint16_t i;

    if (dir_cluster == 0) {
        uint16_t root_sectors;
        uint16_t sec;

        root_sectors = (uint16_t)((fs->root_entries * 32
                                   + fs->bytes_per_sector - 1)
                                  / fs->bytes_per_sector);

        for (sec = 0; sec < root_sectors; sec++) {
            if (disk_read_sectors(fs->drive, fs->root_dir_lba + sec, 1,
                                  KERNEL_SEGMENT, (uint16_t)sec_buf) != 0)
                return ERR_DISK_ERROR;

            for (i = 0; i < 16; i++) {
                fat16_dirent_t *de = (fat16_dirent_t *)(sec_buf + i * 32);

                if (de->name[0] == 0x00 || de->name[0] == 0xE5) {
                    *out_sector_lba = fs->root_dir_lba + sec;
                    *out_entry_offset = (uint16_t)(i * 32);
                    return 0;
                }
            }
        }
        return ERR_NO_MEMORY;  /* root directory full */
    }

    /* Subdirectory: walk the cluster chain */
    {
        uint16_t cluster = dir_cluster;
        uint16_t prev = dir_cluster;

        while (cluster >= 2 && cluster < FAT16_EOF_MIN) {
            uint32_t sector_lba = fs->data_lba + (uint32_t)(cluster - 2);

            if (disk_read_sectors(fs->drive, sector_lba, 1,
                                  KERNEL_SEGMENT, (uint16_t)sec_buf) != 0)
                return ERR_DISK_ERROR;

            for (i = 0; i < 16; i++) {
                fat16_dirent_t *de = (fat16_dirent_t *)(sec_buf + i * 32);

                if (de->name[0] == 0x00 || de->name[0] == 0xE5) {
                    *out_sector_lba = sector_lba;
                    *out_entry_offset = (uint16_t)(i * 32);
                    return 0;
                }
            }

            prev = cluster;
            cluster = fat16_read_fat(fs, cluster);
        }

        /* Chain full: extend the directory with a zeroed cluster */
        {
            uint16_t new_cluster = fat16_find_free_cluster(fs);
            uint32_t sector_lba;
            uint16_t j;

            if (new_cluster == FAT16_BAD)
                return ERR_NO_MEMORY;
            if (fat16_write_fat(fs, prev, new_cluster) != 0)
                return ERR_DISK_ERROR;
            if (fat16_write_fat(fs, new_cluster, FAT16_EOF_MIN) != 0)
                return ERR_DISK_ERROR;

            sector_lba = fs->data_lba + (uint32_t)(new_cluster - 2);
            for (j = 0; j < 512; j++)
                sec_buf[j] = 0;
            if (disk_write_sectors(fs->drive, sector_lba, 1,
                                   KERNEL_SEGMENT, (uint16_t)sec_buf) != 0)
                return ERR_DISK_ERROR;

            *out_sector_lba = sector_lba;
            *out_entry_offset = 0;
            return 0;
        }
    }
}

/* ─── Create a new file in a directory (0 = root) ─── */
uint16_t fat16_create_in_dir(fat16_fs_t *fs, uint16_t dir_cluster,
                             const uint8_t *name, fat16_file_t *file)
{
    uint32_t free_sector_lba;
    uint16_t free_entry_offset;
    uint16_t cluster;
    uint16_t ret;

    /* First check if file already exists */
    {
        fat16_dirent_t tmp;
        if (fat16_find_in_dir(fs, dir_cluster, name, &tmp, NULL, NULL) == 0)
            return ERR_FILE_EXISTS;
    }

    /* Allocate a cluster for the file data; mark it end-of-chain right
     * away so a directory-extension allocation below cannot hand out the
     * same cluster. */
    cluster = fat16_find_free_cluster(fs);
    if (cluster == FAT16_BAD)
        return ERR_NO_MEMORY;
    if (fat16_write_fat(fs, cluster, FAT16_EOF_MIN) != 0)
        return ERR_DISK_ERROR;

    /* Find a free directory slot (leaves that sector in sec_buf) */
    ret = fat16_dir_free_slot(fs, dir_cluster, &free_sector_lba,
                              &free_entry_offset);
    if (ret != 0) {
        fat16_write_fat(fs, cluster, FAT16_FREE);
        return ret;
    }

    /* Fill in the directory entry */
    {
        uint8_t j;

        /* Clear the entry slot */
        for (j = 0; j < 32; j++)
            sec_buf[free_entry_offset + j] = 0;

        /* Copy name (8 bytes) */
        for (j = 0; j < 8; j++)
            sec_buf[free_entry_offset + j] = name[j];

        /* Copy extension (3 bytes) */
        for (j = 0; j < 3; j++)
            sec_buf[free_entry_offset + 8 + j] = name[8 + j];

        /* Attributes: archive */
        sec_buf[free_entry_offset + 11] = FAT16_ATTR_ARCHIVE;

        /* First cluster (little-endian) */
        sec_buf[free_entry_offset + 26] = (uint8_t)(cluster & 0xFF);
        sec_buf[free_entry_offset + 27] = (uint8_t)(cluster >> 8);

        /* File size = 0 (already cleared) */
    }

    /* Write directory sector back */
    if (disk_write_sectors(fs->drive, free_sector_lba, 1, KERNEL_SEGMENT,
                           (uint16_t)sec_buf) != 0)
        return ERR_DISK_ERROR;

    /* Initialize file handle */
    file->fs = fs;
    file->first_cluster = cluster;
    file->file_size = 0;
    file->current_cluster = cluster;
    file->position = 0;
    file->cluster_pos = 0;
    file->dir_sector_lba = free_sector_lba;
    file->dir_entry_offset = free_entry_offset;

    return 0;
}

/* ─── Create a new file in root directory ─── */
uint16_t fat16_create(fat16_fs_t *fs, const uint8_t *name,
                       fat16_file_t *file)
{
    return fat16_create_in_dir(fs, 0, name, file);
}

/* ─── Delete a file from a directory (0 = root) ─── */
uint16_t fat16_delete_in_dir(fat16_fs_t *fs, uint16_t dir_cluster,
                             const uint8_t *name)
{
    fat16_dirent_t dirent;
    uint32_t sector_lba;
    uint16_t entry_offset;

    if (fat16_find_in_dir(fs, dir_cluster, name, &dirent,
                          &sector_lba, &entry_offset) != 0)
        return ERR_NOT_FOUND;

    /* Read the directory sector */
    if (disk_read_sectors(fs->drive, sector_lba, 1, KERNEL_SEGMENT,
                          (uint16_t)sec_buf) != 0)
        return ERR_DISK_ERROR;

    /* Mark entry as deleted */
    sec_buf[entry_offset] = 0xE5;

    /* Write back */
    if (disk_write_sectors(fs->drive, sector_lba, 1, KERNEL_SEGMENT,
                           (uint16_t)sec_buf) != 0)
        return ERR_DISK_ERROR;

    /* Free the cluster chain */
    fat16_free_chain(fs, dirent.first_cluster);

    return 0;
}

/* ─── Delete a file from root directory ─── */
uint16_t fat16_delete(fat16_fs_t *fs, const uint8_t *name)
{
    return fat16_delete_in_dir(fs, 0, name);
}

/* ─── Create a directory inside a directory (parent 0 = root) ─── */
uint16_t fat16_mkdir_in_dir(fat16_fs_t *fs, uint16_t parent_cluster,
                            const uint8_t *name)
{
    uint32_t free_sector_lba;
    uint16_t free_entry_offset;
    uint16_t cluster;
    uint16_t ret;

    /* Check if name already exists */
    {
        fat16_dirent_t tmp;
        if (fat16_find_in_dir(fs, parent_cluster, name, &tmp, NULL, NULL) == 0)
            return ERR_FILE_EXISTS;
    }

    /* Allocate the directory's cluster; mark end-of-chain right away so a
     * parent-extension allocation below cannot hand out the same cluster. */
    cluster = fat16_find_free_cluster(fs);
    if (cluster == FAT16_BAD)
        return ERR_NO_MEMORY;
    if (fat16_write_fat(fs, cluster, FAT16_EOF_MIN) != 0)
        return ERR_DISK_ERROR;

    /* Initialize the new cluster with "." and ".." entries */
    {
        uint32_t cluster_lba = fs->data_lba + (uint32_t)(cluster - 2);
        uint16_t j;

        /* Clear the whole sector so unused entries are 0x00 */
        for (j = 0; j < 512; j++)
            sec_buf[j] = 0;

        /* Entry 0: "." -> this directory */
        for (j = 0; j < 11; j++) sec_buf[j] = ' ';
        sec_buf[0] = '.';
        sec_buf[11] = FAT16_ATTR_DIRECTORY;
        sec_buf[26] = (uint8_t)(cluster & 0xFF);
        sec_buf[27] = (uint8_t)(cluster >> 8);

        /* Entry 1: ".." -> parent directory (cluster 0 = root) */
        for (j = 0; j < 11; j++) sec_buf[32 + j] = ' ';
        sec_buf[32] = '.';
        sec_buf[33] = '.';
        sec_buf[32 + 11] = FAT16_ATTR_DIRECTORY;
        sec_buf[32 + 26] = (uint8_t)(parent_cluster & 0xFF);
        sec_buf[32 + 27] = (uint8_t)(parent_cluster >> 8);

        if (disk_write_sectors(fs->drive, cluster_lba, 1, KERNEL_SEGMENT,
                               (uint16_t)sec_buf) != 0)
            return ERR_DISK_ERROR;
    }

    /* Find a free slot in the parent AFTER writing the dot entries, so
     * sec_buf holds the parent directory sector when we fill the entry
     * (filling it earlier corrupted that sector with dot-entry data). */
    ret = fat16_dir_free_slot(fs, parent_cluster, &free_sector_lba,
                              &free_entry_offset);
    if (ret != 0) {
        fat16_write_fat(fs, cluster, FAT16_FREE);
        return ret;
    }

    /* Fill in the parent directory entry */
    {
        uint8_t j;

        for (j = 0; j < 32; j++)
            sec_buf[free_entry_offset + j] = 0;

        for (j = 0; j < 8; j++)
            sec_buf[free_entry_offset + j] = name[j];

        for (j = 0; j < 3; j++)
            sec_buf[free_entry_offset + 8 + j] = name[8 + j];

        sec_buf[free_entry_offset + 11] = FAT16_ATTR_DIRECTORY;

        sec_buf[free_entry_offset + 26] = (uint8_t)(cluster & 0xFF);
        sec_buf[free_entry_offset + 27] = (uint8_t)(cluster >> 8);
    }

    /* Write directory sector back */
    if (disk_write_sectors(fs->drive, free_sector_lba, 1, KERNEL_SEGMENT,
                           (uint16_t)sec_buf) != 0)
        return ERR_DISK_ERROR;

    return 0;
}

/* ─── Create a directory in root directory ─── */
uint16_t fat16_mkdir(fat16_fs_t *fs, const uint8_t *name)
{
    return fat16_mkdir_in_dir(fs, 0, name);
}

/* ─── Remove an empty directory from a directory (0 = root) ─── */
uint16_t fat16_rmdir_in_dir(fat16_fs_t *fs, uint16_t dir_cluster,
                            const uint8_t *name)
{
    fat16_dirent_t dirent;
    uint32_t sector_lba;
    uint16_t entry_offset;

    if (fat16_find_in_dir(fs, dir_cluster, name, &dirent,
                          &sector_lba, &entry_offset) != 0)
        return ERR_NOT_FOUND;

    /* Must be a directory */
    if (!(dirent.attrs & FAT16_ATTR_DIRECTORY))
        return ERR_ACCESS_DENIED;

    /* Check that the whole cluster chain holds only "." / ".." entries */
    {
        uint16_t cluster = dirent.first_cluster;

        while (cluster >= 2 && cluster < FAT16_EOF_MIN) {
            uint32_t cluster_lba = fs->data_lba + (uint32_t)(cluster - 2);
            uint16_t i;

            if (disk_read_sectors(fs->drive, cluster_lba, 1, KERNEL_SEGMENT,
                                  (uint16_t)sec_buf) != 0)
                return ERR_DISK_ERROR;

            for (i = 0; i < 16; i++) {
                fat16_dirent_t *de = (fat16_dirent_t *)(sec_buf + i * 32);
                if (de->name[0] == 0x00 || de->name[0] == 0xE5)
                    continue;
                if (de->name[0] == '.')
                    continue;
                return ERR_ACCESS_DENIED;  /* directory not empty */
            }

            cluster = fat16_read_fat(fs, cluster);
        }
    }

    /* Free the cluster chain */
    if (dirent.first_cluster >= 2)
        fat16_free_chain(fs, dirent.first_cluster);

    /* Mark parent directory entry as deleted */
    if (disk_read_sectors(fs->drive, sector_lba, 1, KERNEL_SEGMENT,
                          (uint16_t)sec_buf) != 0)
        return ERR_DISK_ERROR;

    sec_buf[entry_offset] = 0xE5;

    if (disk_write_sectors(fs->drive, sector_lba, 1, KERNEL_SEGMENT,
                           (uint16_t)sec_buf) != 0)
        return ERR_DISK_ERROR;

    return 0;
}

/* ─── Remove a directory from root directory ─── */
uint16_t fat16_rmdir(fat16_fs_t *fs, const uint8_t *name)
{
    return fat16_rmdir_in_dir(fs, 0, name);
}

/* ─── Rename a file/directory within a directory (0 = root) ─── */
uint16_t fat16_rename_in_dir(fat16_fs_t *fs, uint16_t dir_cluster,
                             const uint8_t *old_name,
                             const uint8_t *new_name)
{
    fat16_dirent_t dirent;
    uint32_t sector_lba;
    uint16_t entry_offset;
    uint8_t j;

    if (fat16_find_in_dir(fs, dir_cluster, old_name, &dirent,
                          &sector_lba, &entry_offset) != 0)
        return ERR_NOT_FOUND;

    /* Check that new name doesn't already exist */
    {
        fat16_dirent_t tmp;
        if (fat16_find_in_dir(fs, dir_cluster, new_name, &tmp, NULL, NULL) == 0)
            return ERR_FILE_EXISTS;
    }

    /* Read directory sector */
    if (disk_read_sectors(fs->drive, sector_lba, 1, KERNEL_SEGMENT,
                          (uint16_t)sec_buf) != 0)
        return ERR_DISK_ERROR;

    /* Overwrite name (8 bytes) and extension (3 bytes) */
    for (j = 0; j < 8; j++)
        sec_buf[entry_offset + j] = new_name[j];
    for (j = 0; j < 3; j++)
        sec_buf[entry_offset + 8 + j] = new_name[8 + j];

    /* Write back */
    return disk_write_sectors(fs->drive, sector_lba, 1, KERNEL_SEGMENT,
                              (uint16_t)sec_buf);
}

/* ─── Rename a file/directory in root directory ─── */
uint16_t fat16_rename(fat16_fs_t *fs, const uint8_t *old_name,
                       const uint8_t *new_name)
{
    return fat16_rename_in_dir(fs, 0, old_name, new_name);
}

/* ─── Set date/time on a directory entry ─── */
/* dir_cluster 0 = root.  Writes FAT16-format date (CX) and time (DX)
 * to directory entry offsets 22-23 (time) and 24-25 (date).
 * Mirrors the fat16_update_fsize() pattern. */
uint16_t fat16_set_datetime_in_dir(fat16_fs_t *fs, uint16_t dir_cluster,
                                   const uint8_t *name83,
                                   uint16_t date, uint16_t time)
{
    fat16_dirent_t dirent;
    uint32_t sector_lba;
    uint16_t entry_offset;

    if (fat16_find_in_dir(fs, dir_cluster, name83, &dirent,
                          &sector_lba, &entry_offset) != 0)
        return ERR_NOT_FOUND;

    if (disk_read_sectors(fs->drive, sector_lba, 1, KERNEL_SEGMENT,
                          (uint16_t)sec_buf) != 0)
        return ERR_DISK_ERROR;

    /* FAT16 dirent layout: offset 22 = creation/write time, 24 = date */
    sec_buf[entry_offset + 22] = (uint8_t)(time & 0xFF);
    sec_buf[entry_offset + 23] = (uint8_t)(time >> 8);
    sec_buf[entry_offset + 24] = (uint8_t)(date & 0xFF);
    sec_buf[entry_offset + 25] = (uint8_t)(date >> 8);

    return disk_write_sectors(fs->drive, sector_lba, 1, KERNEL_SEGMENT,
                              (uint16_t)sec_buf);
}