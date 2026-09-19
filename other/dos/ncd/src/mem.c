/* mem.c -- Memory allocation for DOS NCD
 * Uses standard malloc/free from C library.
 */

#include "dos_ncd.h"
#include <stdlib.h>

void *mem_alloc(size_t size)
{
    return malloc(size);
}

void mem_free(void *ptr)
{
    free(ptr);
}