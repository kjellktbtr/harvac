/* builtin.c -- Built-in shell commands */

#include "kernel.h"
#include "shell.h"
#include "exec.h"
#include "drivers/video.h"
#include "drivers/serial.h"
#include "drivers/fat16.h"
#include "lib/strings.h"
#include "fs/vfs.h"

/* ─── Convert a user-typed filename ("FILE.TXT" or "FILE") to 8.3 format ─── */
static void name_to_83(const char *arg, uint8_t *out83)
{
    uint8_t i, dot;

    for (i = 0; i < 11; i++)
        out83[i] = ' ';
    out83[11] = '\0';

    for (dot = 0; arg[dot] != '\0' && arg[dot] != '.' && arg[dot] != ' '; dot++)
        ;
    for (i = 0; i < dot && i < 8; i++)
        out83[i] = (uint8_t)arg[i];
    if (arg[dot] == '.') {
        dot++;
        for (i = 0; i < 3 && arg[dot + i] != '\0' && arg[dot + i] != ' '; i++)
            out83[8 + i] = (uint8_t)arg[dot + i];
    }
}

/* ─── help: list available commands ─── */
static void cmd_help(void)
{
    serial_puts("Harvac shell v0.2\n");
    serial_puts("Commands:\n");
    serial_puts("  help      - Show this help\n");
    serial_puts("  cls       - Clear screen\n");
    serial_puts("  echo      - Print arguments\n");
    serial_puts("  ver       - Show kernel version\n");
    serial_puts("  dir/ls    - List directory\n");
    serial_puts("  cat/type  - Print file contents\n");
    serial_puts("  touch     - Create empty file\n");
    serial_puts("  rm/del    - Delete file\n");
    serial_puts("  mv/ren    - Rename file or directory\n");
    serial_puts("  mkdir/md  - Create directory\n");
    serial_puts("  rmdir/rd  - Remove empty directory\n");
    serial_puts("  cd        - Change directory\n");
    serial_puts("  pwd       - Print working directory\n");
    serial_puts("  exec      - Run a .COM program\n");
    serial_puts("  date      - Show current date/time\n");
}

/* ─── cls: clear screen ─── */
static void cmd_cls(void)
{
    video_clear();
}

/* ─── echo: print arguments ─── */
static void cmd_echo(const char *args)
{
    if (args && *args)
        serial_puts(args);
    serial_putchar('\n');
}

/* ─── ver: show version ─── */
static void cmd_ver(void)
{
    serial_puts("Harvac kernel v0.2 (OpenWatcom C)\n");
}

/* ─── dir: list directory ─── */
static void cmd_dir(const char *args)
{
    if (args && *args)
        vfs_dir(args);
    else
        vfs_dir(".");
}

/* ─── cat: print file to stdout ─── */
static void cmd_cat(const char *args)
{
    fat16_file_t file;
    uint8_t buf[256];
    uint16_t bytes;
    uint16_t i;

    if (!args || *args == '\0') {
        serial_puts("Usage: cat <filename>\n");
        return;
    }

    if (vfs_open(args, &file) != 0) {
        serial_puts("File not found\n");
        return;
    }

    while ((bytes = fat16_read(&file, buf, 256)) > 0) {
        for (i = 0; i < bytes; i++)
            serial_putchar((char)buf[i]);
    }
}

/* ─── touch: create empty file ─── */
static void cmd_touch(const char *args)
{
    fat16_file_t file;

    if (!args || *args == '\0') { serial_puts("Usage: touch <filename>\n"); return; }

    if (vfs_create(args, &file) != 0)
        serial_puts("Create failed\n");
}

/* ─── rm: delete file ─── */
static void cmd_rm(const char *args)
{
    if (!args || *args == '\0') { serial_puts("Usage: rm <filename>\n"); return; }

    if (vfs_delete(args) != 0)
        serial_puts("Delete failed\n");
}

/* ─── mv: rename file or directory ─── */
static void cmd_mv(const char *args)
{
    const char *space;

    if (!args || *args == '\0') { serial_puts("Usage: mv <old> <new>\n"); return; }

    /* Find space between old and new names */
    space = args;
    while (*space && *space != ' ') space++;
    if (*space == '\0') { serial_puts("Usage: mv <old> <new>\n"); return; }

    /* Extract old name */
    {
        char old_buf[64], new_buf[64];
        uint16_t len = (uint16_t)(space - args);
        uint8_t i;
        if (len > 63) len = 63;
        for (i = 0; i < len; i++) old_buf[i] = args[i];
        old_buf[len] = '\0';

        /* Skip spaces to find new name */
        while (*space == ' ') space++;
        if (*space == '\0') { serial_puts("Usage: mv <old> <new>\n"); return; }
        {
            uint16_t j;
            for (j = 0; j < 63 && space[j] != '\0'; j++)
                new_buf[j] = space[j];
            new_buf[j] = '\0';
        }

        if (vfs_rename(old_buf, new_buf) != 0)
            serial_puts("Rename failed\n");
    }
}

/* ─── mkdir: create directory ─── */
static void cmd_mkdir(const char *args)
{
    if (!args || *args == '\0') { serial_puts("Usage: mkdir <dirname>\n"); return; }

    if (vfs_mkdir(args) != 0)
        serial_puts("Mkdir failed\n");
}

/* ─── rmdir: remove directory ─── */
static void cmd_rmdir(const char *args)
{
    if (!args || *args == '\0') { serial_puts("Usage: rmdir <dirname>\n"); return; }

    if (vfs_rmdir(args) != 0)
        serial_puts("Rmdir failed\n");
}

/* ─── cd: change directory ─── */
static void cmd_cd(const char *args)
{
    if (!args || *args == '\0')
        vfs_chdir("/");
    else if (vfs_chdir(args) != 0)
        serial_puts("Directory not found\n");
}

/* ─── pwd: print working directory ─── */
static void cmd_pwd(void)
{
    char cwd[VFS_MAX_CWD];
    vfs_getcwd(cwd, VFS_MAX_CWD);
    serial_puts(cwd);
    serial_putchar('\n');
}

/* ─── date: show current date/time ─── */
static void cmd_date(void)
{
    serial_puts("Harvac has no RTC driver yet\n");
}

/* ─── Command dispatch ─── */
void builtin_dispatch(const char *line)
{
    const char *cmd;
    const char *args;
    uint8_t i;

    /* Skip leading spaces */
    cmd = line;
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0')
        return;

    /* Find end of command word */
    args = cmd;
    while (*args && *args != ' ') args++;

    /* Copy command name */
    {
        char cmd_buf[16];
        uint16_t len = (uint16_t)(args - cmd);

        if (len > 15) len = 15;
        for (i = 0; i < len; i++)
            cmd_buf[i] = cmd[i];
        cmd_buf[len] = '\0';

        /* Skip spaces to find args */
        while (*args == ' ') args++;
        if (*args == '\0') args = NULL;

        if (strcmp(cmd_buf, "help") == 0)
            cmd_help();
        else if (strcmp(cmd_buf, "cls") == 0)
            cmd_cls();
        else if (strcmp(cmd_buf, "echo") == 0)
            cmd_echo(args);
        else if (strcmp(cmd_buf, "ver") == 0)
            cmd_ver();
        else if (strcmp(cmd_buf, "date") == 0)
            cmd_date();
        else if (strcmp(cmd_buf, "pwd") == 0)
            cmd_pwd();
        else if (strcmp(cmd_buf, "cd") == 0)
            cmd_cd(args);
        else if (strcmp(cmd_buf, "dir") == 0 || strcmp(cmd_buf, "ls") == 0)
            cmd_dir(args);
        else if (strcmp(cmd_buf, "cat") == 0 || strcmp(cmd_buf, "type") == 0)
            cmd_cat(args);
        else if (strcmp(cmd_buf, "touch") == 0)
            cmd_touch(args);
        else if (strcmp(cmd_buf, "rm") == 0 || strcmp(cmd_buf, "del") == 0)
            cmd_rm(args);
        else if (strcmp(cmd_buf, "mv") == 0 || strcmp(cmd_buf, "ren") == 0)
            cmd_mv(args);
        else if (strcmp(cmd_buf, "mkdir") == 0 || strcmp(cmd_buf, "md") == 0)
            cmd_mkdir(args);
        else if (strcmp(cmd_buf, "rmdir") == 0 || strcmp(cmd_buf, "rd") == 0)
            cmd_rmdir(args);
        else if (strcmp(cmd_buf, "exec") == 0) {
            /* exec <filename> — run a .COM program */
            if (args && *args) {
                mount_entry_t *mnt;
                uint8_t ex_name[12];

                if (vfs_resolve(args, &mnt, ex_name) == 0) {
                    if (exec_com(&mnt->fs, ex_name) != 0)
                        serial_puts("Exec failed\n");
                } else {
                    serial_puts("File not found\n");
                }
            } else {
                serial_puts("Usage: exec <filename>\n");
            }
        } else {
            /* Unknown command — try executing as .COM file */
            if (args || *args == '\0') {
                mount_entry_t *mnt;
                uint8_t ex_name[12];

                if (vfs_resolve(cmd_buf, &mnt, ex_name) == 0) {
                    if (exec_com(&mnt->fs, ex_name) == 0)
                        return;  /* exec_com transferred control */
                }
            }
            serial_puts("Unknown command: ");
            serial_puts(cmd_buf);
            serial_putchar('\n');
        }
    }
}