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

/* ─── Build a normalized absolute path from a possibly-relative one ─── */
void vfs_abspath(const char *path, char *dst, uint16_t dstlen)
{
    char full[VFS_MAX_PATH];
    uint16_t i, j;

    if (path[0] == '/') {
        normalize_path(path, dst, dstlen);
        return;
    }

    /* Relative path: combine with cwd */
    i = strlen(cwd);
    for (j = 0; j < i && j < VFS_MAX_PATH - 1; j++)
        full[j] = cwd[j];
    if (j == 0 || full[j - 1] != '/')
        full[j++] = '/';
    for (i = 0; path[i] != '\0' && j < VFS_MAX_PATH - 1; i++, j++)
        full[j] = path[i];
    full[j] = '\0';
    normalize_path(full, dst, dstlen);
}

/* ─── Pack one path component into 8.3 format ─── */
/* Reads comp up to '\0' or '/'; out must be 12 bytes ("NAME    EXT" + NUL). */
void vfs_name_to_83(const char *comp, uint8_t *out)
{
    uint16_t i;
    uint8_t ni = 0, ei = 0;
    uint8_t dot = 0;

    for (i = 0; i < 11; i++)
        out[i] = ' ';
    out[11] = '\0';

    for (i = 0; comp[i] != '\0' && comp[i] != '/'; i++) {
        uint8_t ch = (uint8_t)comp[i];
        if (ch == '.' && !dot) {
            dot = 1;
            continue;
        }
        if (ch >= 'a' && ch <= 'z')
            ch -= 32;
        if (!dot) {
            if (ni < 8) out[ni++] = ch;
        } else {
            if (ei < 3) out[8 + ei++] = ch;
        }
    }
}

/* ─── Resolve an absolute path to a directory cluster ─── */
uint16_t vfs_resolve_dir(const char *abs_path,
                         mount_entry_t **mnt_out,
                         uint16_t *dir_cluster_out)
{
    mount_entry_t *mnt;
    /* Statics per kernel syscall convention (see syscalls.c) */
    static const char *rel;
    static fat16_dirent_t dirent;
    static uint8_t name_83[12];
    uint16_t cluster = 0;
    uint16_t i;

    mnt = mount_resolve(abs_path, &rel);
    if (!mnt)
        return ERR_NOT_FOUND;

    /* Walk the path components below the mount point */
    i = 0;
    while (rel[i] != '\0') {
        while (rel[i] == '/') i++;
        if (rel[i] == '\0') break;

        vfs_name_to_83(rel + i, name_83);
        if (fat16_find_in_dir(&mnt->fs, cluster, name_83, &dirent,
                              NULL, NULL) != 0)
            return ERR_NOT_FOUND;
        if (!(dirent.attrs & FAT16_ATTR_DIRECTORY))
            return ERR_ACCESS_DENIED;
        cluster = dirent.first_cluster;

        while (rel[i] != '\0' && rel[i] != '/') i++;
    }

    if (mnt_out)
        *mnt_out = mnt;
    if (dir_cluster_out)
        *dir_cluster_out = cluster;
    return 0;
}

/* ─── Change working directory ─── */
uint16_t vfs_chdir(const char *path)
{
    char resolved[VFS_MAX_CWD];
    uint16_t ret;

    vfs_abspath(path, resolved, VFS_MAX_CWD);

    /* Check that the directory exists (walks all components) */
    ret = vfs_resolve_dir(resolved, NULL, NULL);
    if (ret != 0)
        return ret;

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
    char resolved[VFS_MAX_CWD];
    const char *rel;
    const char *last;
    const char *p;
    mount_entry_t *mnt;

    vfs_abspath(path, resolved, VFS_MAX_CWD);

    /* Resolve through mount table */
    mnt = mount_resolve(resolved, &rel);
    if (!mnt)
        return ERR_NOT_FOUND;

    if (mount_out)
        *mount_out = mnt;

    /* Pack the last component into 8.3 format */
    last = rel;
    for (p = rel; *p; p++) {
        if (*p == '/')
            last = p + 1;
    }
    vfs_name_to_83(last, name_83_out);

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