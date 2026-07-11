/* uname.c -- Print system version info via GET_VERSION syscall.
 *
 * Compile: wcc -ms -os -s -zl -d0 -bt=dos -i=src/include uname.c
 * Link: wlink system com name UNAME.COM file uname.obj start _main
 */

#include "types.h"
#include "constants.h"
#include "port_io.h"

void __far _main(void)
{
    /* GET_VERSION: AH=0x82, BX=pointer to 4-byte version buffer */
    uint8_t ver[4];

    syscall_int40(SYSCALL_GET_VERSION, 0, (uint16_t)ver, 0, 0, 0, 0);

    syscall_int40(SYSCALL_WRITE_STDOUT, 0, 0, 0, 0,
                  (uint16_t)"Harvac v", 0);
    syscall_int40(SYSCALL_WRITE_CHAR, (uint8_t)('0' + ver[0]), 0, 0, 0, 0, 0);
    syscall_int40(SYSCALL_WRITE_CHAR, '.', 0, 0, 0, 0, 0);
    syscall_int40(SYSCALL_WRITE_CHAR, (uint8_t)('0' + ver[1]), 0, 0, 0, 0, 0);
    syscall_int40(SYSCALL_WRITE_STDOUT, 0, 0, 0, 0,
                  (uint16_t)"\r\n", 0);
}