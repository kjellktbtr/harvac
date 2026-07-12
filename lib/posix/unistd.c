/* unistd.c -- POSIX low-level I/O and process/fs API for HarvaC userspace.
 * Dispatches through the fd table (fd.h): console fds use WRITE_STDOUT/
 * READ_STDIN/WRITE_STDERR; file/device fds use SYSCALL_READ/WRITE. */

#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "errno.h"
#include "fd.h"
#include "unistd.h"

/* ─── read() ─── */
int read(int fd, void *buf, uint16_t count)
{
    libc_fd_t *e = _fd_get(fd);
    if (!e) return -1;

    if (e->kind == FK_CONSOLE) {
        /* Console read: use READ_STDIN line editor (SI=buf, DI=count) */
        uint16_t n = (uint16_t)syscall_int40(SYSCALL_READ_STDIN, 0,
                                             0, 0, 0,
                                             (uint16_t)buf, count);
        return (int)n;
    }

    /* File/device read */
    {
        uint16_t n = (uint16_t)syscall_int40(SYSCALL_READ, 0,
                                             e->khandle,
                                             (uint16_t)buf,
                                             count, 0, 0);
        if (e->khandle >= 16) return set_errno(ERR_INVALID_HANDLE);
        return (int)n;
    }
}

/* ─── write() ─── */
int write(int fd, const void *buf, uint16_t count)
{
    libc_fd_t *e = _fd_get(fd);
    uint16_t n;
    if (!e) return -1;

    if (e->kind == FK_CONSOLE) {
        uint16_t i;
        const uint8_t *p = (const uint8_t *)buf;
        uint8_t sc = (e->console_no == 2)
                      ? SYSCALL_WRITE_STDERR
                      : SYSCALL_WRITE_STDOUT;
        /* Write char-by-char using WRITE_CHAR (byte in AL) since WRITE_STDOUT
         * takes a NUL-terminated string (SI), not a counted buffer. */
        (void)sc;   /* WRITE_CHAR is simpler for counted writes */
        for (i = 0; i < count; i++)
            syscall_int40(SYSCALL_WRITE_CHAR, p[i], 0, 0, 0, 0, 0);
        return (int)count;
    }

    n = (uint16_t)syscall_int40(SYSCALL_WRITE, 0,
                                e->khandle,
                                (uint16_t)buf,
                                count, 0, 0);
    return (int)n;
}

/* ─── close() ─── */
int close(int fd)
{
    libc_fd_t *e = _fd_get(fd);
    if (!e) return -1;

    if (e->kind != FK_CONSOLE) {
        uint16_t r = (uint16_t)syscall_int40(SYSCALL_CLOSE, 0,
                                             e->khandle, 0, 0, 0, 0);
        if (r != 0) { set_errno(r); _fd_free(fd); return -1; }
    }
    _fd_free(fd);
    return 0;
}

/* ─── lseek() ─── */
/* AL encodes whence; CX:DX is the 32-bit offset (DX=high, CX=low).
 * SEEK (0x14): AL=whence, BX=handle, CX=pos_low, DX=pos_high.
 * Returns new position or -1 on error.
 * Note: SEEK_CUR/SEEK_END require the Phase-2 kernel whence extension.
 * Without it, only SEEK_SET works. */
off_t lseek(int fd, off_t offset, int whence)
{
    libc_fd_t *e = _fd_get(fd);
    uint16_t r;
    if (!e) return (off_t)-1;

    if (e->kind == FK_CONSOLE) { errno = EINVAL; return (off_t)-1; }

    r = (uint16_t)syscall_int40(SYSCALL_SEEK,
                                (uint8_t)whence,          /* AL = whence */
                                e->khandle,               /* BX = handle */
                                (uint16_t)(uint32_t)offset,          /* CX = low16 */
                                (uint16_t)((uint32_t)offset >> 16),  /* DX = high16 */
                                0, 0);
    if (r != 0) return set_errno(r);

    /* Get new position via TELL (Phase 2). Until then, return offset for
     * SEEK_SET (approximate for SEEK_CUR/END). */
    {
        uint32_t pos_buf = 0;
        uint16_t tr = (uint16_t)syscall_int40(SYSCALL_TELL, 0,
                                              e->khandle,
                                              (uint16_t)&pos_buf,
                                              0, 0, 0);
        if (tr != 0)
            return (off_t)offset;   /* fallback: return requested offset */
        return (off_t)pos_buf;
    }
}

/* ─── isatty() ─── */
int isatty(int fd)
{
    libc_fd_t *e = _fd_get(fd);
    if (!e) return 0;
    return (e->kind == FK_CONSOLE) ? 1 : 0;
}

/* ─── _exit() ─── */
void _exit(int status)
{
    (void)status;
    syscall_int40(SYSCALL_EXIT, 0, 0, 0, 0, 0, 0);
    /* not reached */
    for (;;) {}
}

/* ─── getpid() ─── */
int getpid(void)
{
    return (int)(uint16_t)syscall_int40(SYSCALL_GET_PID, 0, 0, 0, 0, 0, 0);
}

/* ─── spawn() ─── */
int spawn(const char *cmd, const char *args)
{
    uint16_t r = (uint16_t)syscall_int40(SYSCALL_EXEC, 0,
                                         (uint16_t)cmd,
                                         (uint16_t)args,
                                         0, 0, 0);
    if (r != 0) return set_errno(r);
    return 0;
}

/* ─── Filesystem wrappers ─── */

int unlink(const char *path)
{
    uint16_t r = (uint16_t)syscall_int40(SYSCALL_DELETE, 0,
                                         (uint16_t)path, 0, 0, 0, 0);
    if (r != 0) return set_errno(r);
    return 0;
}

int rmdir(const char *path)
{
    uint16_t r = (uint16_t)syscall_int40(SYSCALL_RMDIR, 0,
                                         (uint16_t)path, 0, 0, 0, 0);
    if (r != 0) return set_errno(r);
    return 0;
}

int mkdir(const char *path)
{
    uint16_t r = (uint16_t)syscall_int40(SYSCALL_MKDIR, 0,
                                         (uint16_t)path, 0, 0, 0, 0);
    if (r != 0) return set_errno(r);
    return 0;
}

int chdir(const char *path)
{
    uint16_t r = (uint16_t)syscall_int40(SYSCALL_CHDIR, 0,
                                         (uint16_t)path, 0, 0, 0, 0);
    if (r != 0) return set_errno(r);
    return 0;
}

char *getcwd(char *buf, uint16_t size)
{
    syscall_int40(SYSCALL_GETCWD, 0, 0, (uint16_t)buf, size, 0, 0);
    return buf;
}

int rename(const char *oldpath, const char *newpath)
{
    uint16_t r = (uint16_t)syscall_int40(SYSCALL_RENAME, 0, 0, 0, 0,
                                         (uint16_t)oldpath,
                                         (uint16_t)newpath);
    if (r != 0) return set_errno(r);
    return 0;
}
