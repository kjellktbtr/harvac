/* exec.c -- Load and execute .COM files from FAT16 */

#include "kernel.h"
#include "drivers/fat16.h"
#include "drivers/serial.h"

/* Segment where SHELL.COM is loaded (resident) */
#define COM_SEGMENT    0x3000

/* exec_far_jump_sp: far-jump to target_seg:target_off with custom SS:SP.
 *   DX = target_seg, AX = target_off, BX = return_seg, CX = return_off
 *   SI = sp_init (initial stack pointer for the new process)
 * Sets SS:SP = target_seg:sp_init, pushes return address, far-jumps. */
#pragma aux exec_far_jump_sp = \
    "cli" \
    "mov ss, dx" \
    "mov sp, si" \
    "sti" \
    "mov ds, dx" \
    "mov es, dx" \
    "push bx" \
    "push cx" \
    "push dx" \
    "push ax" \
    "retf" \
    parm [dx] [ax] [bx] [cx] [si] modify [ax bx cx dx si];

void exec_far_jump_sp(uint16_t target_seg, uint16_t target_off,
                      uint16_t return_seg, uint16_t return_off,
                      uint16_t sp_init);

/* exec_reentry: defined in exec_stub.asm.
 * Restores kernel SS:SP = KERNEL_SEG:0xE000 and re-enters boot_shell. */
extern void exec_reentry(void);

/* set_next_seg: defined in syscall.asm.
 * Writes the CS-relative g_next_seg segment allocator. */
extern void set_next_seg(uint16_t v);

/* ─── Load a .COM file and execute it ─── */
/* Returns non-zero on error. On success the .COM program runs; never returns. */
uint16_t exec_com(fat16_fs_t *fs, const uint8_t *name_83)
{
    fat16_file_t file;
    uint8_t buf[512];
    uint16_t bytes;
    uint16_t load_off;
    uint16_t reentry_off;
    uint16_t shell_sp;
    uint16_t alloc_paras;

    if (fat16_open(fs, name_83, &file) != 0)
        return 1;

    /* Load the entire file to COM_SEGMENT:0x0100 sector by sector */
    load_off = 0x0100;
    while ((bytes = fat16_read(&file, buf, 512)) > 0) {
        uint16_t i;
        for (i = 0; i < bytes; i++)
            write_far_b(COM_SEGMENT, load_off + i, buf[i]);
        load_off += bytes;
        if (bytes < 512)
            break;
    }

    /* Allocate PROC_PARAS (16 KB) for the shell, same convention as
     * SYSCALL_EXEC children: code + BSS + stack at the top. Kept small
     * so a 256 KB machine has room for children above the shell. */
    alloc_paras = PROC_PARAS;
    set_next_seg(COM_SEGMENT + alloc_paras);

    /* Initial SP: top of the shell's allocation */
    shell_sp = PROC_SP;

    reentry_off = (uint16_t)(void (__near *)(void))&exec_reentry;

    exec_far_jump_sp(COM_SEGMENT, 0x0100, KERNEL_SEGMENT, reentry_off, shell_sp);

    /* Never reached */
    return 0;
}
