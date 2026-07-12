/* du.c -- Estimate disk usage of files and directories.
 * Usage: DU [PATH ...]
 * Recursively sums file sizes in the given path(s) (default: ".").
 * Prints total in KB for each argument, one line per argument.
 *
 * NOTE: _main MUST be the first function defined (wlink COM places functions
 * in source order; file offset 0 = entry point at memory 0x0100).
 */

#include "types.h"
#include "stdio.h"
#include "string.h"
#include "unistd.h"
#include "dirent.h"
#include "sys/stat.h"
#include "args.h"

static char pathbuf[64];

/* Forward declarations */
static uint32_t du_dir(const char *path);
static void print_du(uint32_t bytes, const char *label);

void __far _main(void)
{
    char *argv[16];
    int argc = args_parse(argv, 16);
    int first = args_first_nonoption(argc, argv);
    int i;

    if (first >= argc) {
        /* No args: show CWD usage */
        print_du(du_dir("."), ".");
        return;
    }

    for (i = first; i < argc; i++) {
        print_du(du_dir(argv[i]), argv[i]);
    }
}

/* ─── Print one du line: "KB\tLABEL\r\n" ─── */
static void print_du(uint32_t bytes, const char *label)
{
    uint32_t kb;
    char buf[12];

    kb = (bytes + 1023) / 1024;   /* round up to KB */
    u32toa(kb, buf);
    fputs(buf);
    fputs(" KB\t");
    fputs(label);
    fputs("\r\n");
}

/* ─── Recursive size accumulator ─── */
/* Returns total bytes under path. Assumes CWD is already set to parent. */
static uint32_t du_dir(const char *path)
{
    struct stat st;
    DIR *dp;
    struct dirent *de;
    char cwd_save[64];
    char name[NAME_MAX];
    uint32_t total = 0;

    if (stat(path, &st) == 0 && !S_ISDIR(st.st_attr)) {
        return st.st_size;  /* regular file: return its size */
    }
    /* Directory (or stat failed for special paths like "." at root): descend */
    getcwd(cwd_save, sizeof(cwd_save));
    if (chdir(path) != 0) return 0;

    dp = opendir(".");
    if (dp) {
        while ((de = readdir(dp)) != NULL) {
            /* Skip ".." (POSIX readdir already skips ".") */
            if (de->d_name[0] == '.' && de->d_name[1] == '.' && de->d_name[2] == '\0')
                continue;
            strcpy(name, de->d_name);
            if (de->d_attr & 0x10)      /* directory */
                total += du_dir(name);
            else
                total += de->d_size;
        }
        closedir(dp);
    }

    chdir(cwd_save);
    return total;
}
