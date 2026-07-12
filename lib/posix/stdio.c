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
