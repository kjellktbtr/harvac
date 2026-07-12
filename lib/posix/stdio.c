/* stdio.c -- Minimal console I/O wrappers for HarvaC userspace.
 * Routes through fd 1 (stdout) and fd 0 (stdin) using the POSIX read/write. */

#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "stdio.h"
#include "string.h"

int putchar(int c)
{
    uint8_t b = (uint8_t)c;
    syscall_int40(SYSCALL_WRITE_CHAR, b, 0, 0, 0, 0, 0);
    return c;
}

int puts(const char *s)
{
    syscall_int40(SYSCALL_WRITE_STDOUT, 0, 0, 0, 0, (uint16_t)s, 0);
    syscall_int40(SYSCALL_WRITE_CHAR, '\n', 0, 0, 0, 0, 0);
    return 0;
}

int fputs(const char *s)
{
    syscall_int40(SYSCALL_WRITE_STDOUT, 0, 0, 0, 0, (uint16_t)s, 0);
    return 0;
}

char *gets(char *buf, uint16_t max)
{
    uint16_t n;
    if (!buf || max == 0) return (char *)0;
    n = (uint16_t)syscall_int40(SYSCALL_READ_STDIN, 0, 0, 0, 0,
                                (uint16_t)buf, max);
    (void)n;
    return buf;
}

int getchar(void)
{
    uint16_t k = (uint16_t)syscall_int40(SYSCALL_READ_CHAR, 0, 0, 0, 0, 0, 0);
    return (int)(k & 0xFF);
}

/* ─── Buffered read helpers for getline_fd ─── */
/* File fds and piped stdin are read in 512-byte chunks to avoid per-byte
 * disk I/O.  Interactive tty stdin is still read 1 byte at a time so the
 * kernel line-editor semantics are preserved.
 * _stdin_is_tty is queried once via SYSCALL_ISATTY on the first call with
 * fd=0, then cached.  Initialized to -1 (unqueried) in the data segment. */
#define GLFD_BUFSZ 512
static int      _glfd_fd       = -2;  /* fd this buffer belongs to (-2 = none) */
static uint8_t  _glfd_buf[GLFD_BUFSZ];
static uint16_t _glfd_pos;
static uint16_t _glfd_end;
static int8_t   _stdin_is_tty  = -1;  /* -1=unqueried, 1=tty, 0=piped */

static int glfd_getc(int fd)
{
    extern int read(int, void *, uint16_t);
    if (fd == 0) {
        /* First call: ask the kernel whether stdin is interactive or piped. */
        if (_stdin_is_tty < 0)
            _stdin_is_tty = (int8_t)(uint16_t)
                syscall_int40(SYSCALL_ISATTY, 0, 0, 0, 0, 0, 0);

        if (_stdin_is_tty) {
            /* Interactive tty: read 1 byte at a time (preserves line editor) */
            uint8_t c;
            int r = read(0, &c, 1);
            return (r > 0) ? (int)c : -1;
        }
        /* Piped stdin: sector-sized buffer (reuses _glfd_buf, tagged fd=0) */
        if (fd != _glfd_fd || _glfd_pos >= _glfd_end) {
            int n = read(0, _glfd_buf, GLFD_BUFSZ);
            _glfd_fd  = (n > 0) ? 0 : -2;
            _glfd_pos = 0;
            _glfd_end = (n > 0) ? (uint16_t)n : 0;
            if (n <= 0) return -1;
        }
        return (int)(uint8_t)_glfd_buf[_glfd_pos++];
    }
    /* File fd: use sector-sized buffer */
    if (fd != _glfd_fd || _glfd_pos >= _glfd_end) {
        int n = read(fd, _glfd_buf, GLFD_BUFSZ);
        _glfd_fd  = (n > 0) ? fd : -2;
        _glfd_pos = 0;
        _glfd_end = (n > 0) ? (uint16_t)n : 0;
        if (n <= 0) return -1;
    }
    return (int)(uint8_t)_glfd_buf[_glfd_pos++];
}

int getline_fd(int fd, char *buf, uint16_t max)
{
    uint16_t n = 0;
    int got = 0;

    while (n < max - 1) {
        int c = glfd_getc(fd);
        if (c < 0) break;
        got = 1;
        if ((uint8_t)c == '\r') continue;
        if ((uint8_t)c == '\n') break;
        buf[n++] = (char)c;
    }
    buf[n] = '\0';
    return got ? (int)n : -1;
}

int eputstr(const char *s)
{
    /* WRITE_STDERR (0x04) — always goes to serial+VGA, never redirected.
     * If the kernel hasn't implemented it yet it falls through to default
     * (ERR_NOT_IMPLEMENTED) and we fall back to WRITE_STDOUT. */
    uint16_t r = (uint16_t)syscall_int40(SYSCALL_WRITE_STDERR, 0, 0, 0, 0,
                                          (uint16_t)s, 0);
    if (r != 0) {
        /* Fallback: use plain WRITE_STDOUT until Phase-2 kernel lands */
        syscall_int40(SYSCALL_WRITE_STDOUT, 0, 0, 0, 0, (uint16_t)s, 0);
    }
    return 0;
}
