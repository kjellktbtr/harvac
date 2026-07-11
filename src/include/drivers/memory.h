#ifndef DRIVERS_MEMORY_H
#define DRIVERS_MEMORY_H

#include "types.h"

/* Initialize the bitmap heap allocator */
void memory_init(void);

/* Allocate a 16-byte page. Returns 0 if out of memory. */
uint32_t mem_alloc(void);

/* Free a previously allocated page */
void mem_free(uint32_t page);

#endif /* DRIVERS_MEMORY_H */