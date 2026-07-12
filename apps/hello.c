/* hello.c -- Prints "Hello from Harvac!" using the libc stdio API.
 * Compile: wcc -ms -os -s -zl -d0 -bt=dos -i=src/include -i=lib/include hello.c
 * Link: wlink system com name HELLO.COM file hello.obj ... lib objs
 */

#include "types.h"
#include "stdio.h"

void __far _main(void)
{
    puts("Hello from Harvac!");
}
