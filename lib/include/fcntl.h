/* fcntl.h -- File open flags and open/creat wrappers for HarvaC userspace.
 * The kernel already defines O_RDONLY etc. in constants.h; this header
 * re-exports them and adds the open()/creat() API. */
#ifndef FCNTL_H
#define FCNTL_H

#include "types.h"

/* File access mode flags (match constants.h, reused here for POSIX style) */
#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     4
#define O_APPEND    8

/* Open a file or device. Returns a non-negative file descriptor on success,
 * -1 on failure (errno set). Recognises device paths: "/dev/com1", "/dev/tty",
 * "CON" → serial/console device fd.
 * flags: O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_APPEND */
int open(const char *path, int flags);

/* Create or truncate a file (equiv. open with O_WRONLY|O_CREAT).
 * Returns fd or -1. */
int creat(const char *path);

#endif /* FCNTL_H */
