/* shell.c -- Kernel shell: boot SHELL.COM from filesystem, fallback REPL */

#include "kernel.h"
#include "shell.h"
#include "builtin.h"
#include "exec.h"
#include "syscall.h"
#include "drivers/video.h"
#include "drivers/serial.h"
#include "drivers/keyboard.h"
#include "drivers/fat16.h"
#include "lib/strings.h"
#include "fs/vfs.h"

/* ─── Forward declarations ─── */
static void read_line(char *buf, uint16_t maxlen);
static void kernel_shell(void);

/* ─── Boot shell: try SHELL.COM from filesystem, fall back ─── */
void boot_shell(void)
{
    fat16_fs_t *root_fs = syscall_get_root_fs();
    uint8_t shell_name[12] = "SHELL   COM";

    /* Finalize any stdout redirect from a previous exec'd .COM.
     * Must run before exec_com (which far-jumps and never returns). */
    syscall_finalize_redirect();

    /* Initialize VFS before booting user-space shell, since
     * kernel_shell() won't be reached when exec_com succeeds. */
    vfs_init();

    serial_puts("Booting SHELL.COM...\n");
    if (exec_com(root_fs, shell_name) == 0) {
        /* exec_com only returns on error (success never returns) */
        serial_puts("exec_com returned (error)\n");
    }
    serial_puts("SHELL.COM not found; starting built-in shell\n");
    kernel_shell();
}

/* ─── Main kernel shell REPL loop ─── */
static void kernel_shell(void)
{
    char line[SHELL_MAX_LINE];

    serial_puts("Harvac Shell v0.2\n");

    /* Mount VFS root (calls mount_init internally) */
    vfs_init();
    serial_puts("VFS ready\n");

    for (;;) {
        /* Print prompt with CWD */
        char cwd[VFS_MAX_CWD];
        uint16_t cwd_len;
        vfs_getcwd(cwd, VFS_MAX_CWD);
        serial_puts(cwd);
        serial_puts(SHELL_PROMPT);
        video_write(24, 0, cwd, VIDEO_ATTR_NORMAL);
        cwd_len = strlen(cwd);
        video_write(24, cwd_len, SHELL_PROMPT, VIDEO_ATTR_NORMAL);

        /* Read a line from keyboard */
        read_line(line, SHELL_MAX_LINE);

        /* Echo the newline */
        serial_putchar('\n');

        /* Dispatch */
        builtin_dispatch(line);
    }
}

/* ─── Read one line from keyboard with minimal editing ─── */
static void read_line(char *buf, uint16_t maxlen)
{
    uint16_t pos = 0;

    for (;;) {
        char c = (char)(uint8_t)keyboard_getkey();

        if (c == '\r' || c == '\n') {
            buf[pos] = '\0';
            serial_putchar('\n');  /* echo newline to serial */
            return;
        }

        if (c == '\b' || c == 0x7F) {  /* backspace or DEL */
            if (pos > 0) {
                pos--;
                serial_puts("\b \b");  /* erase on serial */
            }
            continue;
        }

        if (pos < maxlen - 1) {
            buf[pos++] = c;
            serial_putchar(c);  /* echo to serial */
        }
    }
}