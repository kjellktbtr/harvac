/* fs.c -- Filesystem syscall wrappers for NCD.
 * Wraps INT 0x40 syscalls. Implements recursive copy/delete.
 * Name/date/time formatting delegated to lib/hdk/fmt.c via ncd_format_* macros. */

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

/* --- Recursive file operations --- */

static u8 io_buf[512];

u16 ncd_copy_file(const char *src, const char *dst)
{
    u16 sh, dh;
    u16 nr;

    sh = ncd_open(src);
    if (sh >= 16) return 1;

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

    ncd_getcwd(cwd_save, sizeof(cwd_save));

    if (ncd_stat(src, &st) == 0 && (st.attrs & DIR_ATTR_DIRECTORY)) {
        ncd_mkdir(dst);
        ncd_chdir(src);

        if (ncd_opendir(&dir) == 0) {
            while (ncd_readdir(&dir, &ent) == 0) {
                if (ent.name[0] == 0 || ent.name[0] == 0xE5)
                    continue;
                if (ncd_is_dot_entry(&ent))
                    continue;

                ncd_format_name(&ent, name, sizeof(name));

                strcpy(src_path, name);
                strcpy(dst_path, dst);
                strcat(dst_path, "/");
                strcat(dst_path, name);

                ncd_copy_recursive(src_path, dst_path);
            }
            ncd_closedir(&dir);
        }

        ncd_chdir(cwd_save);
        return 0;
    } else {
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

    ncd_getcwd(cwd_save, sizeof(cwd_save));

    if (ncd_stat(path, &st) == 0 && (st.attrs & DIR_ATTR_DIRECTORY)) {
        ncd_chdir(path);

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

        ncd_chdir(cwd_save);
        ncd_rmdir(path);
        return 0;
    } else {
        return ncd_delete(path);
    }
}
