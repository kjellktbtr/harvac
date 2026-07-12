#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"
#include "drivers/fat16.h"

/* Install the INT 0x40 handler in the IVT */
void syscall_init(void);

/* Return pointer to root filesystem (mounted by syscall_init) */
fat16_fs_t *syscall_get_root_fs(void);

/* Close any active stdout redirect; called by boot_shell on exec re-entry */
void syscall_finalize_redirect(void);

/* Close any active stdin redirect; called by boot_shell on exec re-entry */
void syscall_finalize_stdin(void);

/* Dispatch a syscall. Called from int40_entry in syscall.asm */
uint16_t syscall_handler_c(uint16_t ax, uint16_t bx, uint16_t cx,
                            uint16_t dx, uint16_t si, uint16_t di,
                            uint16_t caller_ds);

#endif /* SYSCALL_H */