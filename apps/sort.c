/* sort.c -- Sort lines of text (ascending lexicographic order).
 * Usage: SORT [FILE]
 * Reads stdin if no file given.  Caps at SORT_MAX_LINES / SORT_BUFSZ bytes.
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

#define SORT_MAX_LINES  256
#define SORT_BUFSZ      9216    /* ~36 bytes avg per line at 256 lines */

/* Static buffers — in BSS, zeroed by kernel at load */
static char  data[SORT_BUFSZ];
static char *lines[SORT_MAX_LINES];
static int   nlines;
static int   dpos;

static char linebuf[128];

/* Forward declarations */
static void sort_fd(int fd);

void __far _main(void)
{
    char *argv[8];
    int argc = args_parse(argv, 8);
    int first = args_first_nonoption(argc, argv);

    if (first >= argc) {
        sort_fd(STDIN_FILENO);
        return;
    }

    {
        int fd = open(argv[first], O_RDONLY);
        if (fd < 0) {
            eputstr("sort: cannot open: ");
            eputstr(argv[first]);
            eputstr("\r\n");
            return;
        }
        sort_fd(fd);
        close(fd);
    }
}

static void sort_fd(int fd)
{
    int r;
    nlines = 0;
    dpos   = 0;

    while (nlines < SORT_MAX_LINES && dpos < SORT_BUFSZ - 1) {
        uint16_t avail = (uint16_t)(SORT_BUFSZ - dpos - 1);
        r = getline_fd(fd, linebuf, (avail < sizeof(linebuf)) ? avail : sizeof(linebuf));
        if (r < 0) break;
        lines[nlines++] = data + dpos;
        {
            uint16_t i;
            for (i = 0; linebuf[i]; i++)
                data[dpos++] = linebuf[i];
            data[dpos++] = '\0';
        }
    }

    /* Insertion sort */
    {
        int i, j;
        char *tmp;
        for (i = 1; i < nlines; i++) {
            tmp = lines[i];
            j = i - 1;
            while (j >= 0 && strcmp(lines[j], tmp) > 0) {
                lines[j + 1] = lines[j];
                j--;
            }
            lines[j + 1] = tmp;
        }
    }

    {
        int i;
        for (i = 0; i < nlines; i++) {
            fputs(lines[i]);
            fputs("\r\n");
        }
    }
}
