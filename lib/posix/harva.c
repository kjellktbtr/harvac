/* harva.c -- HarvaC OS-specific syscall wrappers for userspace.
 * Thin veneers so apps never call syscall_int40() directly. */

#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "harva.h"

void sys_clear_screen(void)
{
    syscall_int40(SYSCALL_CLEAR_SCREEN, 0, 0, 0, 0, 0, 0);
}

void sys_write_vga(const char *s)
{
    syscall_int40(SYSCALL_WRITE_VGA, 0, 0, 0, 0, (uint16_t)s, 0);
}

void sys_get_version(uint8_t *ver)
{
    syscall_int40(SYSCALL_GET_VERSION, 0, (uint16_t)ver, 0, 0, 0, 0);
}

void sys_statfs(uint16_t *buf)
{
    syscall_int40(SYSCALL_STATFS, 0, 0, (uint16_t)buf, 0, 0, 0);
}

void sys_mem_info(uint16_t *buf)
{
    syscall_int40(SYSCALL_MEM_INFO, 0, 0, (uint16_t)buf, 0, 0, 0);
}
