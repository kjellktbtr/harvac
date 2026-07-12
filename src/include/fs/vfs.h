#ifndef FS_VFS_H
#define FS_VFS_H

#include "types.h"
#include "drivers/fat16.h"
#include "fs/mount.h"

/* ─── Path limits ─── */
#define VFS_MAX_PATH    128
#define VFS_MAX_CWD     64

/* Initialize VFS layer. Call after mount_init(). */
void vfs_init(void);

/* ─── Working directory ─── */

/* Change working directory. path can be absolute ("/DIR") or relative ("DIR").
 * Returns 0 on success. */
uint16_t vfs_chdir(const char *path);

/* Get current working directory into buf (up to VFS_MAX_CWD bytes). */
void vfs_getcwd(char *buf, uint16_t buflen);

/* ─── Path resolution ─── */

/* Build a normalized absolute path from a possibly-relative one
 * (resolves against CWD; collapses ".", "..", double slashes). */
void vfs_abspath(const char *path, char *dst, uint16_t dstlen);

/* Pack one path component (up to '\0' or '/') into 8.3 format.
 * out must be 12 bytes; receives "NAME    EXT" + NUL. */
void vfs_name_to_83(const char *comp, uint8_t *out);

/* Resolve an absolute path that names a directory to its first cluster.
 * *dir_cluster_out receives 0 for the mount root, else the cluster.
 * Either out pointer may be NULL. Returns 0 on success. */
uint16_t vfs_resolve_dir(const char *abs_path,
                         mount_entry_t **mnt_out,
                         uint16_t *dir_cluster_out);

/* Resolve a path to an 8.3 filename within a mount.
 * If path is relative, it's resolved against CWD.
 * mount_out receives the mount entry.
 * name_83_out receives the 8.3 filename (must be 12 bytes).
 * Returns 0 on success. */
uint16_t vfs_resolve(const char *path,
                      mount_entry_t **mount_out,
                      uint8_t *name_83_out);

/* ─── File operations (thin VFS wrappers) ─── */

uint16_t vfs_open(const char *path, fat16_file_t *file);
uint16_t vfs_create(const char *path, fat16_file_t *file);
uint16_t vfs_delete(const char *path);
uint16_t vfs_mkdir(const char *path);
uint16_t vfs_rmdir(const char *path);
uint16_t vfs_rename(const char *old_path, const char *new_path);
void     vfs_dir(const char *path);  /* list directory */

#endif /* FS_VFS_H */