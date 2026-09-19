/* shell.c -- Shell execution for DOS NCD
 * Uses DOS INT 21h EXEC function (4Bh) to run programs.
 */

#include "dos_ncd.h"
#include <dos.h>
#include <string.h>

/* Execute a program and wait for it to finish */
int shell_exec(const char *cmd, const char *args)
{
    union REGS regs;
    struct SREGS sregs;
    char cmdline[128];
    char full_cmd[DOS_PATH_MAX];
    char *env = NULL;

    /* Build command line: program name + args */
    if (args && *args) {
        snprintf(cmdline, sizeof(cmdline), "%s %s", cmd, args);
    } else {
        snprintf(cmdline, sizeof(cmdline), "%s", cmd);
    }

    /* Find the executable */
    if (_searchenv(cmd, "PATH", full_cmd) != 0) {
        /* Not found in PATH, try as-is */
        strncpy(full_cmd, cmd, sizeof(full_cmd) - 1);
        full_cmd[sizeof(full_cmd) - 1] = '\0';
    }

    /* Save video state */
    vid_done();

    /* Execute program (AH=4Bh, AL=00h = load and execute) */
    segread(&sregs);
    regs.h.ah = 0x4B;
    regs.h.al = 0x00;
    regs.x.dx = (u16)full_cmd;
    regs.x.bx = (u16)&cmdline[1];  /* Parameter block: offset 1 = command tail */
    cmdline[0] = strlen(cmdline + 1);  /* Length of command tail */
    sregs.es = sregs.ds;
    intdosx(&regs, &regs, &sregs);

    /* Restore video */
    vid_init();
    vid_dirty_all();

    if (regs.x.cflag) {
        return -1;  /* Error */
    }

    return 0;
}

/* Execute a batch file via COMMAND.COM */
int shell_exec_batch(const char *batch_file)
{
    char cmdline[128];
    snprintf(cmdline, sizeof(cmdline), "COMMAND.COM /C %s", batch_file);
    return shell_exec("COMMAND.COM", cmdline + 12);  /* Skip "COMMAND.COM " */
}