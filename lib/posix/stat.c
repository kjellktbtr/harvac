/* stat.c -- POSIX stat/fstat/mkdir for HarvaC userspace.
 * Wraps SYSCALL_STAT, which fills a raw fat16_dirent_t (32 bytes) in user
 * space. Converts to struct stat. */

#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "errno.h"
#include "fd.h"
#include "sys/stat.h"

/* Raw fat16_dirent_t layout (32 bytes, offsets):
 *   0-7  name[8]          11  attrs
 *  12-21 reserved[10]     22  time(2)
 *  24    date(2)          28  file_size(4)  */

int stat(const char *path, struct stat *st)
{
    /* Static: SS!=DS during potential caller context; safe as long as stat()
     * is not called re-entrantly (single-threaded real-mode OS). */
    static uint8_t raw[32];
    uint16_t r;

    if (!path || !st) { errno = EFAULT; return -1; }

    r = (uint16_t)syscall_int40(SYSCALL_STAT, 0,
                                (uint16_t)path,
                                (uint16_t)raw,
                                0, 0, 0);
    if (r != 0) return set_errno(r);

    st->st_attr  = raw[11];
    st->st_mtime = (uint16_t)raw[22] | ((uint16_t)raw[23] << 8);
    st->st_mdate = (uint16_t)raw[24] | ((uint16_t)raw[25] << 8);
    st->st_size  = (uint32_t)raw[28]
                 | ((uint32_t)raw[29] << 8)
                 | ((uint32_t)raw[30] << 16)
                 | ((uint32_t)raw[31] << 24);
    return 0;
}

int fstat(int fd, struct stat *st)
{
    /* No kernel FSTAT syscall exists; route through stat using a temporary
     * name query — not a true fstat. For now: unsupported. */
    (void)fd;
    (void)st;
    errno = ENOSYS;
    return -1;
}

int mkdir_p(const char *path)
{
    uint16_t r = (uint16_t)syscall_int40(SYSCALL_MKDIR, 0,
                                         (uint16_t)path, 0, 0, 0, 0);
    if (r != 0) return set_errno(r);
    return 0;
}
