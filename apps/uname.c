/* uname.c -- Print system version info via GET_VERSION syscall.
 * Uses libc stdio/string helpers.
 */

#include "types.h"
#include "harva.h"
#include "stdio.h"
#include "string.h"

void __far _main(void)
{
    uint8_t ver[4];
    char buf[8];

    sys_get_version(ver);

    fputs("Harvac v");
    utoa(ver[0], buf);
    fputs(buf);
    putchar('.');
    utoa(ver[1], buf);
    fputs(buf);
    puts("");   /* trailing newline */
}
