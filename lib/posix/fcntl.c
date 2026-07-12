/* fcntl.c -- open() and creat() for HarvaC userspace.
 * Maps POSIX open flags onto kernel SYSCALL_OPEN / SYSCALL_CREATE.
 * Recognises device paths ("CON", "/dev/tty", "/dev/com1") and creates
 * FK_CONSOLE or FK_DEVICE fd entries accordingly. */

#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "fcntl.h"
#include "errno.h"
#include "fd.h"
#include "string.h"

/* Device path detection — returns FK_CONSOLE (console), FK_DEVICE (serial),
 * or FK_FREE (not a device). */
static uint8_t detect_device(const char *path)
{
    if (strcasecmp(path, "CON") == 0)    return FK_CONSOLE;
    if (strcasecmp(path, "/dev/tty") == 0) return FK_CONSOLE;
    if (strcasecmp(path, "/dev/com1") == 0) return FK_DEVICE;
    return FK_FREE;
}

int open(const char *path, int flags)
{
    uint16_t khandle;
    int fd;
    uint8_t devkind;

    if (path == (char *)0) { errno = EFAULT; return -1; }

    devkind = detect_device(path);
    if (devkind == FK_CONSOLE) {
        /* Console device: allocate a libc fd wired to stdout (fd 1) for
         * write-only opens, or stdin (fd 0) for read-only. */
        fd = _fd_alloc();
        if (fd < 0) return -1;
        _fd_table[fd].kind       = FK_CONSOLE;
        _fd_table[fd].console_no = (flags & O_WRONLY) ? 1 : 0;
        _fd_table[fd].flags      = (uint8_t)flags;
        _fd_table[fd].khandle    = 0xFFFF;
        return fd;
    }

    if (flags & O_CREAT) {
        /* CREATE: kernel SYSCALL_CREATE opens the file for writing */
        khandle = (uint16_t)syscall_int40(SYSCALL_CREATE, 0,
                                          (uint16_t)path, 0, 0, 0, 0);
    } else {
        /* OPEN: AL = flags byte */
        khandle = (uint16_t)syscall_int40(SYSCALL_OPEN, (uint8_t)flags,
                                          (uint16_t)path, 0, 0, 0, 0);
    }

    if (khandle == 0xFFFF || khandle >= 16) {
        errno = ENOENT;
        return -1;
    }

    fd = _fd_alloc();
    if (fd < 0) {
        /* Close the kernel handle we just opened before returning error */
        syscall_int40(SYSCALL_CLOSE, 0, khandle, 0, 0, 0, 0);
        return -1;
    }

    _fd_table[fd].kind    = (devkind == FK_DEVICE) ? FK_DEVICE : FK_FILE;
    _fd_table[fd].flags   = (uint8_t)flags;
    _fd_table[fd].khandle = khandle;
    return fd;
}

int creat(const char *path)
{
    return open(path, O_WRONLY | O_CREAT);
}
