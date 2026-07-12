/* fileops.c -- High-level file/directory operations for HarvaC userspace.
 * Built on the POSIX libc; mirrors the recursive logic from apps/ncd/fs.c
 * so that cp, rm, and other tools share one implementation. */

#include "types.h"
#include "fcntl.h"
#include "unistd.h"
#include "dirent.h"
#include "sys/stat.h"
#include "string.h"
#include "fileops.h"

/* ─── I/O buffer (static: one at a time; single-threaded OS) ─── */
static uint8_t _fop_buf[512];

/* ─── copy_file ─── */
int copy_file(const char *src, const char *dst)
{
    struct stat st;
    int sh, dh;
    int nr;
    uint16_t src_date = 0, src_time = 0;

    /* Preserve source mtime */
    if (stat(src, &st) == 0) {
        src_date = st.st_mdate;
        src_time = st.st_mtime;
    }

    sh = open(src, O_RDONLY);
    if (sh < 0) return 1;

    unlink(dst);        /* ignore error — may not exist */
    dh = open(dst, O_WRONLY | O_CREAT);
    if (dh < 0) { close(sh); return 1; }

    while (1) {
        nr = read(sh, _fop_buf, sizeof(_fop_buf));
        if (nr <= 0) break;
        write(dh, _fop_buf, (uint16_t)nr);
    }

    close(sh);
    close(dh);

    /* Restore timestamp on destination */
    if (src_date != 0 || src_time != 0)
        utime(dst, src_date, src_time);

    return 0;
}

/* ─── copy_tree (recursive) ─── */
int copy_tree(const char *src, const char *dst)
{
    struct stat st;
    DIR *dp;
    struct dirent *de;
    char cwd_save[FOP_PATH_MAX];
    char name[NAME_MAX];
    char dst_path[FOP_PATH_MAX];
    int ret = 0;

    if (stat(src, &st) != 0) return 1;

    if (S_ISDIR(st.st_attr)) {
        /* Create destination directory */
        mkdir(dst);

        /* Save CWD, descend into src */
        getcwd(cwd_save, sizeof(cwd_save));
        if (chdir(src) != 0) return 1;

        dp = opendir(".");
        if (dp) {
            while ((de = readdir(dp)) != NULL) {
                if (de->d_name[0] == '\0' || de->d_name[0] == (char)0xE5)
                    continue;
                if (de->d_name[0] == '.' &&
                    (de->d_name[1] == '\0' ||
                     (de->d_name[1] == '.' && de->d_name[2] == '\0')))
                    continue;

                strcpy(name, de->d_name);

                /* Build dst_path = dst + "/" + name */
                strcpy(dst_path, dst);
                {
                    uint16_t dlen = strlen(dst_path);
                    if (dlen > 0 && dst_path[dlen - 1] != '/')
                        strcat(dst_path, "/");
                }
                strcat(dst_path, name);

                ret |= copy_tree(name, dst_path);
            }
            closedir(dp);
        }

        chdir(cwd_save);
        return ret;
    }

    /* Regular file */
    return copy_file(src, dst);
}

/* ─── remove_tree (recursive) ─── */
int remove_tree(const char *path)
{
    struct stat st;
    DIR *dp;
    struct dirent *de;
    char cwd_save[FOP_PATH_MAX];
    char name[NAME_MAX];

    if (stat(path, &st) != 0) return 1;

    if (S_ISDIR(st.st_attr)) {
        getcwd(cwd_save, sizeof(cwd_save));
        if (chdir(path) != 0) return 1;

        dp = opendir(".");
        if (dp) {
            while ((de = readdir(dp)) != NULL) {
                if (de->d_name[0] == '\0' || de->d_name[0] == (char)0xE5)
                    continue;
                if (de->d_name[0] == '.' &&
                    (de->d_name[1] == '\0' ||
                     (de->d_name[1] == '.' && de->d_name[2] == '\0')))
                    continue;
                strcpy(name, de->d_name);
                remove_tree(name);
            }
            closedir(dp);
        }

        chdir(cwd_save);
        return rmdir(path);
    }

    return unlink(path);
}
