#ifndef FS_H
#define FS_H

#include "ncd.h"

/* FAT16 directory entry (32 bytes, from fat16.h) */
typedef struct {
    u8   name[8];
    u8   ext[3];
    u8   attrs;
    u8   reserved[10];
    u16  time;
    u16  date;
    u16  first_cluster;
    u32  file_size;
} ncd_dirent_t;

/* FAT16 directory handle (from fat16.h) */
typedef struct {
    u16 fs_ptr;       /* pointer to fs struct (kernel-side) */
    u16 sector;
    u16 entry_idx;
    u16 abs_idx;
    u16 root_sectors;
    u16 dir_cluster;
    u16 current_cluster;
} ncd_dir_t;

/* Filesystem syscall wrappers */
u16 ncd_opendir(ncd_dir_t *dir);
u16 ncd_readdir(ncd_dir_t *dir, ncd_dirent_t *ent);
void ncd_closedir(ncd_dir_t *dir);
void ncd_chdir(const char *path);
void ncd_getcwd(char *buf, u16 maxlen);
u16 ncd_open(const char *path);
void ncd_close(u16 handle);
u16 ncd_read(u16 handle, void *buf, u16 count);
u16 ncd_write(u16 handle, const void *buf, u16 count);
u16 ncd_create(const char *path);
u16 ncd_delete(const char *path);
u16 ncd_mkdir(const char *path);
u16 ncd_rmdir(const char *path);
u16 ncd_rename(const char *oldname, const char *newname);
u16 ncd_stat(const char *path, ncd_dirent_t *ent);
u16 ncd_exec(const char *cmd, const char *args);
void ncd_exit(void);

/* Format FAT16 8.3 name to readable string */
void ncd_format_name(const ncd_dirent_t *ent, char *buf, u16 maxlen);

/* Format "FILE.TXT" as "FILE     TXT" (8+1+3 cols); buf >= 13 bytes */
void ncd_format_name12(const char *name, char *buf);

/* Format FAT16 date word as "YYYY.MM.DD" (blank if zero); buf >= 11 bytes */
void ncd_format_date(u16 date, char *buf);

/* Format FAT16 time word as "HH:MM" (blank if no timestamp); buf >= 6 bytes */
void ncd_format_time(u16 date, u16 time, char *buf);

/* Check if dirent name is "." or ".." (current/parent directory) */
int ncd_is_dot_entry(const ncd_dirent_t *ent);

/* Recursive file operations */
u16 ncd_copy_file(const char *src, const char *dst);
u16 ncd_copy_recursive(const char *src, const char *dst);
u16 ncd_delete_recursive(const char *path);

#endif /* FS_H */
