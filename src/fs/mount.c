/* mount.c -- Mount table management for Harvac VFS */

#include "kernel.h"
#include "fs/mount.h"
#include "drivers/serial.h"
#include "lib/strings.h"

/* ─── Mount table ─── */
static mount_entry_t mount_table[MAX_MOUNTS];

/* ─── Initialize mount table and mount root ─── */
void mount_init(void)
{
    uint16_t i;

    for (i = 0; i < MAX_MOUNTS; i++)
        mount_table[i].used = 0;

    /* Mount root partition at "/" (drive 0x80, LBA 63) */
    if (fat16_mount(0x80, 63, &mount_table[0].fs) == 0) {
        mount_table[0].used = 1;
        mount_table[0].drive = 0x80;
        mount_table[0].partition_lba = 63;
        mount_table[0].mount_point[0] = '/';
        mount_table[0].mount_point[1] = '\0';
        serial_puts("mount: / mounted\n");
    } else {
        serial_puts("mount: root mount FAILED\n");
    }
}

/* ─── Add a mount entry ─── */
uint16_t mount_add(const char *mount_point, uint8_t drive, uint32_t partition_lba)
{
    uint16_t i;

    /* Check if already mounted */
    for (i = 0; i < MAX_MOUNTS; i++) {
        if (mount_table[i].used && strcmp(mount_table[i].mount_point, mount_point) == 0)
            return ERR_FILE_EXISTS;
    }

    /* Find free slot */
    for (i = 0; i < MAX_MOUNTS; i++) {
        if (!mount_table[i].used) {
            if (fat16_mount(drive, partition_lba, &mount_table[i].fs) != 0)
                return ERR_DISK_ERROR;

            mount_table[i].used = 1;
            mount_table[i].drive = drive;
            mount_table[i].partition_lba = partition_lba;
            strcpy(mount_table[i].mount_point, mount_point);
            return 0;
        }
    }

    return ERR_NO_MEMORY;
}

/* ─── Remove a mount entry ─── */
uint16_t mount_remove(const char *mount_point)
{
    uint16_t i;

    /* Don't allow unmounting root */
    if (mount_point[0] == '/' && mount_point[1] == '\0')
        return ERR_ACCESS_DENIED;

    for (i = 0; i < MAX_MOUNTS; i++) {
        if (mount_table[i].used && strcmp(mount_table[i].mount_point, mount_point) == 0) {
            mount_table[i].used = 0;
            return 0;
        }
    }

    return ERR_NOT_FOUND;
}

/* ─── Find mount by mount point ─── */
mount_entry_t *mount_find(const char *mount_point)
{
    uint16_t i;

    for (i = 0; i < MAX_MOUNTS; i++) {
        if (mount_table[i].used && strcmp(mount_table[i].mount_point, mount_point) == 0)
            return &mount_table[i];
    }

    return NULL;
}

/* ─── Resolve path to mount entry ─── */
mount_entry_t *mount_resolve(const char *path, const char **rel_path)
{
    uint16_t i;
    uint16_t best_len = 0;
    mount_entry_t *best = NULL;

    for (i = 0; i < MAX_MOUNTS; i++) {
        const char *mp;
        uint16_t mplen;

        if (!mount_table[i].used)
            continue;

        mp = mount_table[i].mount_point;
        mplen = strlen(mp);

        /* Check if path starts with this mount point */
        if (strncmp(path, mp, mplen) == 0) {
            /* Path after mount point should be '/' or '\0',
             * unless this is the root mount point "/" which
             * matches any path starting with '/'. */
            if (path[mplen] == '/' || path[mplen] == '\0'
                || (mplen == 1 && mp[0] == '/')) {
                if (mplen > best_len) {
                    best_len = mplen;
                    best = &mount_table[i];
                }
            }
        }
    }

    if (best) {
        const char *rest = path + best_len;
        if (*rest == '/')
            rest++;
        if (rel_path)
            *rel_path = rest;
    }

    return best;
}

/* ─── Get root mount entry ─── */
mount_entry_t *mount_get_root(void)
{
    return mount_find("/");
}

/* ─── Get entry by index ─── */
mount_entry_t *mount_get_entry(uint16_t index)
{
    if (index >= MAX_MOUNTS || !mount_table[index].used)
        return NULL;
    return &mount_table[index];
}

/* ─── Format mount name for display ─── */
uint16_t mount_format_name(uint16_t index, char *buf, uint16_t buflen)
{
    mount_entry_t *m = mount_get_entry(index);
    if (!m)
        return 1;

    strcpy(buf, m->mount_point);
    return 0;
}