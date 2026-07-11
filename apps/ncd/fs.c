/* fs.c -- Filesystem syscall wrappers for NCD.
 * Wraps INT 0x40 syscalls. Implements recursive copy/delete. */

#include "ncd.h"
#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "fs.h"

/* --- Basic syscall wrappers --- */

u16 ncd_opendir(ncd_dir_t *dir)
{
    memset(dir, 0, sizeof(*dir));
    return (u16)syscall_int40(SYSCALL_OPENDIR, 0, 0, (u16)dir, 0, 0, 0);
}

u16 ncd_readdir(ncd_dir_t *dir, ncd_dirent_t *ent)
{
    return (u16)syscall_int40(SYSCALL_READDIR, 0, 0, (u16)dir, (u16)ent, 0, 0);
}

void ncd_closedir(ncd_dir_t *dir)
{
    syscall_int40(SYSCALL_CLOSEDIR, 0, 0, (u16)dir, 0, 0, 0);
}

void ncd_chdir(const char *path)
{
    syscall_int40(SYSCALL_CHDIR, 0, (u16)path, 0, 0, 0, 0);
}

void ncd_getcwd(char *buf, u16 maxlen)
{
    syscall_int40(SYSCALL_GETCWD, 0, 0, (u16)buf, maxlen, 0, 0);
}

u16 ncd_open(const char *path)
{
    return (u16)syscall_int40(SYSCALL_OPEN, 0, (u16)path, 0, 0, 0, 0);
}

void ncd_close(u16 handle)
{
    syscall_int40(SYSCALL_CLOSE, 0, handle, 0, 0, 0, 0);
}

u16 ncd_read(u16 handle, void *buf, u16 count)
{
    return (u16)syscall_int40(SYSCALL_READ, 0, handle, (u16)buf, count, 0, 0);
}

u16 ncd_write(u16 handle, const void *buf, u16 count)
{
    return (u16)syscall_int40(SYSCALL_WRITE, 0, handle, (u16)buf, count, 0, 0);
}

u16 ncd_create(const char *path)
{
    return (u16)syscall_int40(SYSCALL_CREATE, 0, (u16)path, 0, 0, 0, 0);
}

u16 ncd_delete(const char *path)
{
    return (u16)syscall_int40(SYSCALL_DELETE, 0, (u16)path, 0, 0, 0, 0);
}

u16 ncd_mkdir(const char *path)
{
    return (u16)syscall_int40(SYSCALL_MKDIR, 0, (u16)path, 0, 0, 0, 0);
}

u16 ncd_rmdir(const char *path)
{
    return (u16)syscall_int40(SYSCALL_RMDIR, 0, (u16)path, 0, 0, 0, 0);
}

u16 ncd_rename(const char *oldname, const char *newname)
{
    return (u16)syscall_int40(SYSCALL_RENAME, 0, 0, 0, 0, (u16)oldname, (u16)newname);
}

u16 ncd_stat(const char *path, ncd_dirent_t *ent)
{
    return (u16)syscall_int40(SYSCALL_STAT, 0, (u16)path, (u16)ent, 0, 0, 0);
}

u16 ncd_exec(const char *cmd, const char *args)
{
    return (u16)syscall_int40(SYSCALL_EXEC, 0, (u16)cmd, (u16)args, 0, 0, 0);
}

void ncd_exit(void)
{
    syscall_int40(SYSCALL_EXIT, 0, 0, 0, 0, 0, 0);
}

/* --- Name formatting --- */

void ncd_format_name(const ncd_dirent_t *ent, char *buf, u16 maxlen)
{
    u16 i, j = 0;

    (void)maxlen;  /* assume buf >= 13 bytes */

    for (i = 0; i < 8 && ent->name[i] != ' '; i++)
        buf[j++] = (char)ent->name[i];
    if (ent->ext[0] != ' ') {
        buf[j++] = '.';
        for (i = 0; i < 3 && ent->ext[i] != ' '; i++)
            buf[j++] = (char)ent->ext[i];
    }
    buf[j] = '\0';
}

void ncd_format_datetime(u16 date, u16 time, char *buf, u16 maxlen)
{
    u16 year, month, day, hour, minute;
    u16 tlen;
    char tmp[6];

    (void)maxlen;  /* assume buf >= 16 bytes */

    /* FAT16 date: bits 0-4=day, 5-8=month, 9-15=year offset from 1980 */
    day   = date & 0x1F;
    month = (date >> 5) & 0x0F;
    year  = (date >> 9) + 1980;

    /* FAT16 time: bits 0-4=seconds/2, 5-10=minute, 11-15=hour */
    hour   = time >> 11;
    minute = (time >> 5) & 0x3F;

    /* Format as YYYY-MM-DD HH:MM */
    m_u32toa((u32)year, tmp);
    tlen = (u16)strlen(tmp);
    memcpy(buf, tmp, tlen);
    buf[tlen++] = '-';
    m_itoa(month, tmp);
    memcpy(buf + tlen, tmp, strlen(tmp));
    tlen += (u16)strlen(tmp);
    buf[tlen++] = '-';
    m_itoa(day, tmp);
    memcpy(buf + tlen, tmp, strlen(tmp));
    tlen += (u16)strlen(tmp);
    buf[tlen++] = ' ';
    m_itoa(hour, tmp);
    memcpy(buf + tlen, tmp, strlen(tmp));
    tlen += (u16)strlen(tmp);
    buf[tlen++] = ':';
    m_itoa(minute, tmp);
    memcpy(buf + tlen, tmp, strlen(tmp));
    tlen += (u16)strlen(tmp);
    buf[tlen] = '\0';
}

int ncd_is_dot_entry(const ncd_dirent_t *ent)
{
    /* "." entry: name[0] == '.' and nothing else */
    if (ent->name[0] == '.' && ent->name[1] == ' ')
        return 1;
    /* ".." entry: name[0] == '.' and name[1] == '.' */
    if (ent->name[0] == '.' && ent->name[1] == '.' && ent->name[2] == ' ')
        return 1;
    return 0;
}

/* --- Recursive file operations --- */

/* Static buffer for read/write operations */
static u8 io_buf[512];

u16 ncd_copy_file(const char *src, const char *dst)
{
    u16 sh, dh;
    u16 nr;

    sh = ncd_open(src);
    if (sh >= 16) return 1;  /* open failed */

    /* Delete existing dst if any, then create */
    ncd_delete(dst);
    dh = ncd_create(dst);
    if (dh >= 16) { ncd_close(sh); return 1; }

    while (1) {
        nr = ncd_read(sh, io_buf, sizeof(io_buf));
        if (nr == 0) break;
        ncd_write(dh, io_buf, nr);
    }

    ncd_close(sh);
    ncd_close(dh);
    return 0;
}

u16 ncd_copy_recursive(const char *src, const char *dst)
{
    ncd_dir_t dir;
    ncd_dirent_t ent;
    ncd_dirent_t st;
    char name[13];
    char src_path[PATH_MAX];
    char dst_path[PATH_MAX];
    char cwd_save[PATH_MAX];
    u16 ret;

    /* Save current directory */
    ncd_getcwd(cwd_save, sizeof(cwd_save));

    /* Check if src is a directory by trying to stat it */
    if (ncd_stat(src, &st) == 0 && (st.attrs & DIR_ATTR_DIRECTORY)) {
        /* src is a directory: create dst directory, recurse into entries */
        ncd_mkdir(dst);

        /* Change to src directory to enumerate entries */
        ncd_chdir(src);

        if (ncd_opendir(&dir) == 0) {
            while (ncd_readdir(&dir, &ent) == 0) {
                if (ent.name[0] == 0 || ent.name[0] == 0xE5)
                    continue;
                if (ncd_is_dot_entry(&ent))
                    continue;

                ncd_format_name(&ent, name, sizeof(name));

                /* Build src/dst paths */
                strcpy(src_path, name);
                strcpy(dst_path, dst);
                strcat(dst_path, "/");
                strcat(dst_path, name);

                ncd_copy_recursive(src_path, dst_path);
            }
            ncd_closedir(&dir);
        }

        /* Restore CWD */
        ncd_chdir(cwd_save);
        return 0;
    } else {
        /* src is a file: copy single file */
        ret = ncd_copy_file(src, dst);
        return ret;
    }
}

u16 ncd_delete_recursive(const char *path)
{
    ncd_dir_t dir;
    ncd_dirent_t ent;
    char name[13];
    char cwd_save[PATH_MAX];
    ncd_dirent_t st;

    /* Save CWD */
    ncd_getcwd(cwd_save, sizeof(cwd_save));

    /* Check if path is a directory */
    if (ncd_stat(path, &st) == 0 && (st.attrs & DIR_ATTR_DIRECTORY)) {
        /* Change into the directory */
        ncd_chdir(path);

        /* Delete all entries recursively */
        if (ncd_opendir(&dir) == 0) {
            while (ncd_readdir(&dir, &ent) == 0) {
                if (ent.name[0] == 0 || ent.name[0] == 0xE5)
                    continue;
                if (ncd_is_dot_entry(&ent))
                    continue;

                ncd_format_name(&ent, name, sizeof(name));
                ncd_delete_recursive(name);
            }
            ncd_closedir(&dir);
        }

        /* Restore CWD */
        ncd_chdir(cwd_save);

        /* Now remove the (empty) directory */
        ncd_rmdir(path);
        return 0;
    } else {
        /* Delete single file */
        return ncd_delete(path);
    }
}
