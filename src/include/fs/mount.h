#ifndef FS_MOUNT_H
#define FS_MOUNT_H

#include "types.h"
#include "drivers/fat16.h"

/* ─── Mount table ─── */
#define MAX_MOUNTS  8

typedef struct {
    uint8_t     used;
    char        mount_point[32];  /* e.g. "/" for root */
    uint8_t     drive;            /* BIOS drive number */
    uint32_t    partition_lba;    /* Partition start LBA */
    fat16_fs_t  fs;               /* Mounted filesystem */
} mount_entry_t;

/* Initialize the mount table. Mounts root partition at "/" by default. */
void mount_init(void);

/* Add a mount entry. Returns 0 on success. */
uint16_t mount_add(const char *mount_point, uint8_t drive, uint32_t partition_lba);

/* Remove a mount entry. Returns 0 on success, ERR_NOT_FOUND if not mounted. */
uint16_t mount_remove(const char *mount_point);

/* Find a mount entry by mount point. Returns NULL if not found. */
mount_entry_t *mount_find(const char *mount_point);

/* Find the mount entry that contains a given path.
 * Returns the mount entry and sets rel_path to the path after the mount point. */
mount_entry_t *mount_resolve(const char *path, const char **rel_path);

/* Get the root mount entry (always present). */
mount_entry_t *mount_get_root(void);

/* Get the nth mount entry (for listing). */
mount_entry_t *mount_get_entry(uint16_t index);

/* Format: "/FLOPPY" or "/HARDDISK" etc. */
uint16_t mount_format_name(uint16_t index, char *buf, uint16_t buflen);

#endif /* FS_MOUNT_H */