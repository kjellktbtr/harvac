/* fs.c -- Filesystem helpers for NCD.
 * Thin delegators to lib/posix/fileops.c so NCD does not duplicate
 * copy/delete logic.  All direct syscall_int40 usage has been replaced
 * by POSIX wrappers; see lib/posix/{unistd,fcntl,dirent,stat,fileops}.c. */

#include "ncd.h"
#include "types.h"
#include "unistd.h"
#include "fileops.h"
#include "fs.h"

/* ─── Recursive file operations ─── */

/* Recursively copy src to dst.  Delegates to lib/posix/fileops copy_tree,
 * which handles file→file, dir→new dir, and dir-into-existing-dir cases. */
u16 ncd_copy_recursive(const char *src, const char *dst)
{
    return (u16)copy_tree(src, dst);
}

/* Recursively delete path.  Delegates to lib/posix/fileops remove_tree. */
u16 ncd_delete_recursive(const char *path)
{
    return (u16)remove_tree(path);
}
