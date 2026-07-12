/* head.c -- Output the first N lines of files or stdin.
 * Usage: HEAD [-n N] [FILE ...]
 * Default N = 10.
 *
 * NOTE: _main MUST be the first function defined (wlink COM places functions
 * in source order; file offset 0 = entry point at memory 0x0100).
 */

#include "types.h"
#include "stdio.h"
#include "fcntl.h"
#include "unistd.h"
#include "string.h"
#include "args.h"

static char linebuf[256];

/* Forward declarations */
static void head_fd(int fd, int n);

void __far _main(void)
{
    char *argv[16];
    int argc = args_parse(argv, 16);
    const char *nval = args_flag_val(argc, argv, 'n');
    int n = nval ? atoi(nval) : 10;
    int first = args_first_nonoption(argc, argv);
    int i;

    if (n <= 0) n = 10;

    if (first >= argc) {
        head_fd(STDIN_FILENO, n);
        return;
    }

    for (i = first; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            eputstr("head: cannot open: ");
            eputstr(argv[i]);
            eputstr("\r\n");
            continue;
        }
        head_fd(fd, n);
        close(fd);
    }
}

static void head_fd(int fd, int n)
{
    int count = 0;
    int r;
    while (count < n && (r = getline_fd(fd, linebuf, sizeof(linebuf))) >= 0) {
        fputs(linebuf);
        fputs("\r\n");
        count++;
    }
}
