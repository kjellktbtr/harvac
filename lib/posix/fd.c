/* fd.c -- POSIX fd table for HarvaC userspace.
 * Pre-wires fds 0/1/2 as console (stdin/stdout/stderr).
 * open() allocates fd 3+; close() frees the slot. */

#include "types.h"
#include "errno.h"
#include "fd.h"

/* Static fd table — freestanding, no heap needed */
libc_fd_t _fd_table[LIBC_MAX_FDS] = {
    /* fd 0 = stdin  (console read) */
    { FK_CONSOLE, 0, 0, 0xFFFF },
    /* fd 1 = stdout (console write) */
    { FK_CONSOLE, 1, 0, 0xFFFF },
    /* fd 2 = stderr (console write, never redirected) */
    { FK_CONSOLE, 2, 0, 0xFFFF },
    /* fds 3..15 = free */
};

int _fd_alloc(void)
{
    int i;
    for (i = 3; i < LIBC_MAX_FDS; i++) {
        if (_fd_table[i].kind == FK_FREE)
            return i;
    }
    errno = EMFILE;
    return -1;
}

void _fd_free(int fd)
{
    if (fd >= 0 && fd < LIBC_MAX_FDS)
        _fd_table[fd].kind = FK_FREE;
}

libc_fd_t *_fd_get(int fd)
{
    if (fd < 0 || fd >= LIBC_MAX_FDS || _fd_table[fd].kind == FK_FREE) {
        errno = EBADF;
        return (libc_fd_t *)0;
    }
    return &_fd_table[fd];
}
