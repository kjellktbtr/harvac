/* cat.c -- Concatenate and display file contents.
 * Usage: CAT [FILE ...]
 * With no file arguments reads from stdin (enables piping).
 * Supports multiple file arguments.
 *
 * NOTE: _main MUST be the first function defined (wlink COM places functions
 * in source order; file offset 0 = entry point at memory 0x0100).
 */

#include "types.h"
#include "fcntl.h"
#include "unistd.h"
#include "stdio.h"
#include "args.h"

static uint8_t buf[512];

/* Forward declarations */
static void cat_fd(int fd);

void __far _main(void)
{
    char *argv[16];
    int argc = args_parse(argv, 16);
    int i;
    int first;

    /* Find first non-option argument */
    first = args_first_nonoption(argc, argv);

    if (first >= argc) {
        /* No file args: cat stdin */
        cat_fd(STDIN_FILENO);
        return;
    }

    for (i = first; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            eputstr("cat: cannot open: ");
            eputstr(argv[i]);
            eputstr("\r\n");
            continue;
        }
        cat_fd(fd);
        close(fd);
    }
}

/* cat_fd: dump all bytes from fd to stdout */
static void cat_fd(int fd)
{
    int bytes;
    while ((bytes = read(fd, buf, sizeof(buf))) > 0)
        write(STDOUT_FILENO, buf, (uint16_t)bytes);
}
