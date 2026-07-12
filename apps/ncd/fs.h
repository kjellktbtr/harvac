#ifndef FS_H
#define FS_H

#include "ncd.h"

/* Raw FAT16 directory entry — typedef alias for fat_dirent_t (from harva.h).
 * Layout: name[8]+ext[3]+attrs+reserved[10]+time+date+cluster+size = 32 bytes. */
typedef fat_dirent_t ncd_dirent_t;

/* Raw FAT16 directory handle — the 7 uint16_t fields the kernel maintains. */
typedef struct {
    u16 fs_ptr;
    u16 sector;
    u16 entry_idx;
    u16 abs_idx;
    u16 root_sectors;
    u16 dir_cluster;
    u16 current_cluster;
} ncd_dir_t;

/* ─── Filesystem syscall wrappers ─── */
u16  ncd_opendir(ncd_dir_t *dir);
u16  ncd_readdir(ncd_dir_t *dir, ncd_dirent_t *ent);
void ncd_closedir(ncd_dir_t *dir);
void ncd_chdir(const char *path);
void ncd_getcwd(char *buf, u16 maxlen);
u16  ncd_open(const char *path);
void ncd_close(u16 handle);
u16  ncd_read(u16 handle, void *buf, u16 count);
u16  ncd_write(u16 handle, const void *buf, u16 count);
u16  ncd_create(const char *path);
u16  ncd_delete(const char *path);
u16  ncd_mkdir(const char *path);
u16  ncd_rmdir(const char *path);
u16  ncd_rename(const char *oldname, const char *newname);
u16  ncd_stat(const char *path, ncd_dirent_t *ent);
u16  ncd_exec(const char *cmd, const char *args);
void ncd_exit(void);

/* ─── Name and date/time formatting ─── */
/* Delegated to shared fat_format_* in lib/hdk/fmt.c (via harva.h). */
#define ncd_format_name(ent, buf, len)  fat_format_name((ent), (buf))
#define ncd_format_name12               fat_format_name12
#define ncd_format_date                 fat_format_date
#define ncd_format_time                 fat_format_time
#define ncd_is_dot_entry                fat_is_dot_entry

/* ─── Recursive file operations ─── */
u16 ncd_copy_file(const char *src, const char *dst);
u16 ncd_copy_recursive(const char *src, const char *dst);
u16 ncd_delete_recursive(const char *path);

#endif /* FS_H */
