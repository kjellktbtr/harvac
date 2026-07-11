/* kmain.c -- Kernel C entry point. Called from entry.asm after setup. */

#include "kernel.h"
#include "syscall.h"
#include "drivers/video.h"
#include "drivers/serial.h"
#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "drivers/memory.h"
#include "drivers/disk.h"
#include "drivers/fat16.h"
#include "lib/strings.h"
#include "fs/mount.h"
#include "fs/vfs.h"
#include "shell.h"

/* Write single bright char to VGA top-left area for early boot diagnosis.
 * Called before video_init() clears the screen; shows which stage hung. */
#define DBG_VGA(col, ch) \
    write_far_w(0xB800, (uint16_t)((col) * 2), (uint16_t)((ch) | (0x0F << 8)))

void kmain(void)
{
    DBG_VGA(0, 'A');   /* reached kmain */
    serial_init();
    DBG_VGA(1, 'B');   /* serial OK */
    video_init();
    /* screen is now clear; write to row 0 directly */
    DBG_VGA(0, 'C');   /* video OK */
    keyboard_init();
    DBG_VGA(1, 'D');   /* keyboard OK */
    timer_init();
    DBG_VGA(2, 'E');   /* timer OK */
    memory_init();
    DBG_VGA(3, 'F');   /* memory OK */

    /* Initialize VFS and mount table */
    mount_init();
    DBG_VGA(4, 'G');   /* mount OK */
    vfs_init();
    DBG_VGA(5, 'H');   /* vfs OK */

    /* Install syscall handler (also mounts FAT16) */
    syscall_init();
    DBG_VGA(6, 'I');   /* syscall OK */

    /* Enable interrupts (needed for timer IRQ0) */
    enable_interrupts();

    /* Boot banner: to both serial and VGA TTY */
    serial_puts("Harvac kernel booted (OpenWatcom C)\n");
    video_tty_puts("Harvac kernel booted (OpenWatcom C)\n");
    serial_puts("Drivers OK\n");
    video_tty_puts("Drivers OK\n");

    /* Boot the shell (try SHELL.COM from filesystem, fall back to built-in) */
    boot_shell();

    /* Never reached */
    for (;;)
        ;
}