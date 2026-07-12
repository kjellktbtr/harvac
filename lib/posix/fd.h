/* fd.h -- Internal libc file descriptor table for HarvaC userspace.
 * Maps POSIX fd numbers to kernel handles or special console kinds.
 * Not a public API; included by fcntl.c, unistd.c, dirent.c etc. */
#ifndef LIBC_FD_H
#define LIBC_FD_H

#include "types.h"

/* fd kinds */
#define FK_FREE     0   /* slot is unused */
#define FK_CONSOLE  1   /* routes to console syscalls (stdin/out/err) */
#define FK_FILE     2   /* regular file: khandle = kernel open_files index */
#define FK_DEVICE   3   /* device fd: khandle = kernel open_files index (Phase 2) */

/* Max simultaneous open fds (libc side; kernel table is also 16 slots) */
#define LIBC_MAX_FDS  16

typedef struct {
    uint8_t  kind;       /* FK_* */
    uint8_t  console_no; /* FK_CONSOLE: 0=stdin, 1=stdout, 2=stderr */
    uint8_t  flags;      /* O_* flags from open() */
    uint16_t khandle;    /* kernel handle (FK_FILE/FK_DEVICE); 0xFFFF = none */
} libc_fd_t;

/* The fd table — fds 0/1/2 pre-allocated as FK_CONSOLE in fd.c */
extern libc_fd_t _fd_table[LIBC_MAX_FDS];

/* Allocate the next free libc fd (>= 3). Returns fd index or -1 if full. */
int _fd_alloc(void);

/* Release libc fd slot (does not close kernel handle). */
void _fd_free(int fd);

/* Validate fd: returns pointer to entry or NULL (sets errno = EBADF). */
libc_fd_t *_fd_get(int fd);

#endif /* LIBC_FD_H */
