/* fileops.h -- High-level file/directory operations built on the POSIX libc.
 * Provides copy, recursive copy, and recursive delete helpers shared by
 * cp, rm, and future tools — factored out of the NCD file manager. */
#ifndef FILEOPS_H
#define FILEOPS_H

#include "types.h"

/* Maximum path length for recursive operations. */
#define FOP_PATH_MAX  64

/* Copy a single file from src to dst.
 * Deletes dst first if it exists.  Preserves the source modification date/time.
 * Returns 0 on success, non-zero on error. */
int copy_file(const char *src, const char *dst);

/* Recursively copy src to dst.
 * If src is a directory, dst is created and each entry copied in turn.
 * If src is a file, behaves like copy_file().
 * Returns 0 on success, non-zero if any step fails. */
int copy_tree(const char *src, const char *dst);

/* Recursively delete path.
 * If path is a directory, all contents are deleted before the directory.
 * If path is a file, it is unlinked directly.
 * Returns 0 on success, non-zero on error. */
int remove_tree(const char *path);

#endif /* FILEOPS_H */
