/* stdlib.h -- Minimal standard utilities for HarvaC userspace. */
#ifndef STDLIB_H
#define STDLIB_H

#include "types.h"

/* Exit the current process with status code (does not return). */
void  exit(int status);

/* Return the absolute value of n. */
int   abs(int n);

/* Allocate n paragraphs (16-byte units) of far memory via SYSCALL_ALLOC.
 * Returns the base segment, or 0 on failure. */
uint16_t alloc_paras(uint16_t n);

/* Release a segment previously returned by alloc_paras().
 * In current Harvac, all segments are freed on process exit; this call is
 * a no-op in the kernel but documents intent and keeps call sites clean. */
void free_paras(uint16_t seg);

#endif /* STDLIB_H */
