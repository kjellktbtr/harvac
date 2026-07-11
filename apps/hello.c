/* hello.c -- Prints "Hello from Harvac!" via INT 40h then returns to shell.
 * Compile: wcc -ms -os -s -zl -d0 -bt=dos -i=src/include hello.c
 * Link: wlink system com name HELLO.COM file hello.obj start _main
 */

#include "types.h"
#include "constants.h"
#include "port_io.h"

void __far _main(void)
{
    syscall_int40(SYSCALL_WRITE_STDOUT, 0, 0, 0, 0,
                  (uint16_t)"Hello from Harvac!\r\n", 0);
}