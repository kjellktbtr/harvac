/* stdlib.c -- Minimal standard utilities for HarvaC userspace. */

#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "stdlib.h"

void exit(int status)
{
    (void)status;
    syscall_int40(SYSCALL_EXIT, 0, 0, 0, 0, 0, 0);
    for (;;) {}
}

int abs(int n)
{
    return (n < 0) ? -n : n;
}

uint16_t alloc_paras(uint16_t n)
{
    return (uint16_t)syscall_int40(SYSCALL_ALLOC, 0, n, 0, 0, 0, 0);
}

void free_paras(uint16_t seg)
{
    syscall_int40(SYSCALL_FREE, 0, seg, 0, 0, 0, 0);
}
