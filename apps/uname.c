/* uname.c -- Print system version info via GET_VERSION syscall.
 * Uses libc stdio/string helpers.
 */

#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "stdio.h"
#include "string.h"

void __far _main(void)
{
    uint8_t ver[4];
    char buf[8];

    syscall_int40(SYSCALL_GET_VERSION, 0, (uint16_t)ver, 0, 0, 0, 0);

    fputs("Harvac v");
    utoa(ver[0], buf);
    fputs(buf);
    putchar('.');
    utoa(ver[1], buf);
    fputs(buf);
    puts("");   /* trailing newline */
}
