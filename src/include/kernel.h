/* kernel.h -- Main kernel header. Include this from all kernel C files. */

#ifndef KERNEL_H
#define KERNEL_H

#include "types.h"
#include "constants.h"
#include "port_io.h"

/* kmain -- kernel entry point called from entry.asm */
void kmain(void);

#endif /* KERNEL_H */
