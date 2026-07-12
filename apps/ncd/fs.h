#ifndef FS_H
#define FS_H

#include "ncd.h"

/* ─── Recursive file operations ─── */
/* These delegate to lib/posix/fileops (copy_tree / remove_tree).
 * main.c uses them; panel.c and viewer.c call POSIX functions directly. */
u16 ncd_copy_recursive(const char *src, const char *dst);
u16 ncd_delete_recursive(const char *path);

#endif /* FS_H */
