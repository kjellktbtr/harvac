/* vfs.c -- Virtual file system: path resolution, CWD, mount dispatch */

#include "kernel.h"
#include "fs/mount.h"
#include "fs/vfs.h"
#include "drivers/serial.h"
#include "drivers/fat16.h"
#include "lib/strings.h"

/* ─── Current working directory ─── */
static char cwd[VFS_MAX_CWD];

/* ─── Initialize VFS ─── */
void vfs_init(void)
{
    cwd[0] = '/';
    cwd[1] = '\0';
}

/* ─── Get working directory ─── */
void vfs_getcwd(char *buf, uint16_t buflen)
{
    uint16_t i;
    uint16_t len = strlen(cwd);
    if (len >= buflen)
        len = buflen - 1;
    for (i = 0; i < len; i++)
        buf[i] = cwd[i];
    buf[len] = '\0';
}

/* ─── Normalize a path: remove "..", ".", double slashes ─── */
static void normalize_path(const char *src, char *dst, uint16_t dstlen)
{
    char stack[VFS_MAX_CWD][32];
    uint16_t sp = 0;
    uint16_t i = 0;
    uint16_t di;
    uint16_t j;
    char comp[32];

    /* Ensure absolute */
    if (src[0] != '/') {
        dst[0] = '/';
        dst[1] = '\0';
        return;
    }

    while (src[i] != '\0' && sp < VFS_MAX_CWD) {
        /* Skip slashes */
        while (src[i] == '/') i++;
        if (src[i] == '\0') break;

        /* Read component */
        for (j = 0; j < 31 && src[i] != '\0' && src[i] != '/'; j++, i++)
            comp[j] = src[i];
        comp[j] = '\0';

        if (strcmp(comp, ".") == 0)
            continue;
        if (strcmp(comp, "..") == 0) {
            if (sp > 0) sp--;
            continue;
        }
        strcpy(stack[sp], comp);
        sp++;
    }

    /* Build normalized path */
    di = 0;
    dst[di++] = '/';
    for (i = 0; i < sp && di < dstlen - 2; i++) {
        for (j = 0; stack[i][j] != '\0' && di < dstlen - 2; j++)
            dst[di++] = stack[i][j];
        if (i < sp - 1)
            dst[di++] = '/';
    }
    dst[di] = '\0';
}

/* ─── Change working directory ─── */
uint16_t vfs_chdir(const char *path)
{
    char resolved[VFS_MAX_CWD];
    char full[VFS_MAX_CWD];
    mount_entry_t *mnt;
    const char *rel;
    uint16_t ret;

    /* Build full path */
    if (path[0] == '/') {
        /* Absolute path */
        normalize_path(path, resolved, VFS_MAX_CWD);
    } else {
        /* Relative path: combine with cwd */
        uint16_t i, j;
        i = strlen(cwd);
        for (j = 0; j < i && j < VFS_MAX_CWD - 1; j++)
            full[j] = cwd[j];
        if (full[j - 1] != '/')
            full[j++] = '/';
        for (i = 0; path[i] != '\0' && j < VFS_MAX_CWD - 1; i++, j++)
            full[j] = path[i];
        full[j] = '\0';
        normalize_path(full, resolved, VFS_MAX_CWD);
    }

    /* Check that the directory exists by trying to resolve it.
     * For root dir ("/"), always succeed. */
    if (strcmp(resolved, "/") == 0) {
        strcpy(cwd, resolved);
        return 0;
    }

    /* For subdirectories, try to resolve via mount */
    mnt = mount_resolve(resolved, &rel);
    if (!mnt)
        return ERR_NOT_FOUND;

    if (rel && *rel) {
        uint8_t name_83[12];
        fat16_dirent_t dirent;
        uint16_t j;

        for (j = 0; j < 11; j++) name_83[j] = ' ';
        name_83[11] = '\0';
        for (j = 0; j < 8 && rel[j] != '\0' && rel[j] != '/'; j++) {
            uint8_t ch = (uint8_t)rel[j];
            if (ch >= 'a' && ch <= 'z') ch -= 32;
            name_83[j] = ch;
        }

        ret = fat16_find(&mnt->fs, name_83, &dirent, NULL, NULL);
        if (ret != 0)
            return ERR_NOT_FOUND;
        if (!(dirent.attrs & FAT16_ATTR_DIRECTORY))
            return ERR_ACCESS_DENIED;
    }

    /* Store canonical upper-case (FAT is case-insensitive; consumers
     * such as SYSCALL_OPENDIR match the CWD against 8.3 names) */
    for (ret = 0; resolved[ret] != '\0'; ret++) {
        if (resolved[ret] >= 'a' && resolved[ret] <= 'z')
            resolved[ret] -= 32;
    }
    strcpy(cwd, resolved);
    return 0;
}

/* ─── Resolve a path to a mount + 8.3 filename ─── */
uint16_t vfs_resolve(const char *path,
                      mount_entry_t **mount_out,
                      uint8_t *name_83_out)
{
    char full[VFS_MAX_CWD];
    char resolved[VFS_MAX_CWD];
    const char *rel;
    uint16_t i;
    mount_entry_t *mnt;

    /* Build absolute path */
    if (path[0] != '/') {
        /* Relative to CWD */
        uint16_t j;
        i = strlen(cwd);
        for (j = 0; j < i && j < VFS_MAX_CWD - 1; j++)
            full[j] = cwd[j];
        if (i > 0 && full[i - 1] != '/')
            full[i++] = '/';
        for (j = 0; path[j] != '\0' && i < VFS_MAX_CWD - 1; i++, j++)
            full[i] = path[j];
        full[i] = '\0';
    } else {
        i = 0;
        while (path[i] != '\0' && i < VFS_MAX_CWD - 1) {
            full[i] = path[i];
            i++;
        }
        full[i] = '\0';
    }

    normalize_path(full, resolved, VFS_MAX_CWD);

    /* Resolve through mount table */
    mnt = mount_resolve(resolved, &rel);
    if (!mnt)
        return ERR_NOT_FOUND;

    if (mount_out)
        *mount_out = mnt;

    /* Extract filename into 8.3 format */
    {
        uint16_t j;
        uint8_t dot_found = 0;
        uint8_t name_part[9], ext_part[4];
        uint8_t ni = 0, ei = 0;

        for (j = 0; j < 8; j++) name_part[j] = ' ';
        for (j = 0; j < 3; j++) ext_part[j] = ' ';

        if (rel) {
            /* Skip to last component (past any slashes) */
            const char *last = rel;
            const char *p = rel;
            while (*p) {
                if (*p == '/')
                    last = p + 1;
                p++;
            }

            /* Parse name[.ext] */
            for (p = last; *p && *p != '/'; p++) {
                if (*p == '.' && !dot_found) {
                    dot_found = 1;
                } else if (!dot_found && ni < 8) {
                    uint8_t ch = (uint8_t)*p;
                    if (ch >= 'a' && ch <= 'z') ch -= 32;
                    name_part[ni++] = ch;
                } else if (dot_found && ei < 3) {
                    uint8_t ch = (uint8_t)*p;
                    if (ch >= 'a' && ch <= 'z') ch -= 32;
                    ext_part[ei++] = ch;
                }
            }
        }

        for (j = 0; j < 8; j++)
            name_83_out[j] = name_part[j];
        for (j = 0; j < 3; j++)
            name_83_out[8 + j] = ext_part[j];
        name_83_out[11] = '\0';
    }

    return 0;
}

/* ─── VFS file operations ─── */

uint16_t vfs_open(const char *path, fat16_file_t *file)
{
    mount_entry_t *mnt;
    uint8_t name83[12];
    uint16_t ret;

    ret = vfs_resolve(path, &mnt, name83);
    if (ret != 0) return ret;

    return fat16_open(&mnt->fs, name83, file);
}

uint16_t vfs_create(const char *path, fat16_file_t *file)
{
    mount_entry_t *mnt;
    uint8_t name83[12];
    uint16_t ret;

    ret = vfs_resolve(path, &mnt, name83);
    if (ret != 0) return ret;

    return fat16_create(&mnt->fs, name83, file);
}

uint16_t vfs_delete(const char *path)
{
    mount_entry_t *mnt;
    uint8_t name83[12];
    uint16_t ret;

    ret = vfs_resolve(path, &mnt, name83);
    if (ret != 0) return ret;

    return fat16_delete(&mnt->fs, name83);
}

uint16_t vfs_mkdir(const char *path)
{
    mount_entry_t *mnt;
    uint8_t name83[12];
    uint16_t ret;

    ret = vfs_resolve(path, &mnt, name83);
    if (ret != 0) return ret;

    return fat16_mkdir(&mnt->fs, name83);
}

uint16_t vfs_rmdir(const char *path)
{
    mount_entry_t *mnt;
    uint8_t name83[12];
    uint16_t ret;

    ret = vfs_resolve(path, &mnt, name83);
    if (ret != 0) return ret;

    return fat16_rmdir(&mnt->fs, name83);
}

uint16_t vfs_rename(const char *old_path, const char *new_path)
{
    mount_entry_t *mnt_old, *mnt_new;
    uint8_t old83[12], new83[12];
    uint16_t ret;

    ret = vfs_resolve(old_path, &mnt_old, old83);
    if (ret != 0) return ret;

    ret = vfs_resolve(new_path, &mnt_new, new83);
    if (ret != 0) return ret;

    /* Both must be on same mount */
    if (mnt_old != mnt_new)
        return ERR_ACCESS_DENIED;

    return fat16_rename(&mnt_old->fs, old83, new83);
}

/* ─── List directory ─── */
void vfs_dir(const char *path)
{
    mount_entry_t *mnt;
    uint8_t name83[12];
    fat16_dir_t dir;
    fat16_dirent_t dirent;
    char hex[11];
    uint16_t count;
    uint16_t ret;

    ret = vfs_resolve(path, &mnt, name83);
    if (ret != 0) {
        serial_puts("Path not found\n");
        return;
    }

    /* For now, only root directory listing works */
    if (name83[0] == ' ' && name83[1] == ' ' && name83[2] == ' '
        && name83[3] == ' ' && name83[4] == ' ' && name83[5] == ' '
        && name83[6] == ' ' && name83[7] == ' ') {
        /* Root directory */
        fat16_opendir(&mnt->fs, &dir);
    } else {
        serial_puts("Subdirectory listing not yet supported\n");
        return;
    }

    count = 0;
    while (fat16_readdir(&dir, &dirent) == 0) {
        uint8_t i;

        for (i = 0; i < 8 && dirent.name[i] != ' '; i++)
            serial_putchar((char)dirent.name[i]);
        if (dirent.ext[0] != ' ') {
            serial_putchar('.');
            for (i = 0; i < 3 && dirent.ext[i] != ' '; i++)
                serial_putchar((char)dirent.ext[i]);
        }

        if (dirent.attrs & FAT16_ATTR_DIRECTORY)
            serial_puts("  <DIR>");
        else {
            serial_putchar(' ');
            hex_to_str(dirent.file_size, hex);
            serial_puts(hex);
        }
        serial_putchar('\n');
        count++;
    }

    fat16_closedir(&dir);
    hex_to_str(count, hex);
    serial_puts("Total: ");
    serial_puts(hex);
    serial_puts(" files\n");
}