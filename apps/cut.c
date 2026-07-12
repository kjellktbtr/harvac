/* cut.c -- Extract a field from each line.
 * Usage: CUT -d DELIM -f N [FILE]
 * -d: field delimiter (default: tab)
 * -f: 1-based field number to print
 * Reads stdin if no file given.
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
static void cut_fd(int fd, char delim, int field);

void __far _main(void)
{
    char *argv[16];
    int argc = args_parse(argv, 16);
    const char *dval = args_flag_val(argc, argv, 'd');
    const char *fval = args_flag_val(argc, argv, 'f');
    char delim = dval ? dval[0] : '\t';
    int field  = fval ? atoi(fval) : 1;
    int first  = args_first_nonoption(argc, argv);

    if (field <= 0) field = 1;

    if (first >= argc) {
        cut_fd(STDIN_FILENO, delim, field);
        return;
    }

    {
        int fd = open(argv[first], O_RDONLY);
        if (fd < 0) {
            eputstr("cut: cannot open: ");
            eputstr(argv[first]);
            eputstr("\r\n");
            return;
        }
        cut_fd(fd, delim, field);
        close(fd);
    }
}

static void cut_fd(int fd, char delim, int field)
{
    int r;
    while ((r = getline_fd(fd, linebuf, sizeof(linebuf))) >= 0) {
        char *p = linebuf;
        int cur = 1;

        while (cur < field) {
            while (*p && (uint8_t)*p != (uint8_t)delim) p++;
            if (!*p) break;     /* delimiter not found */
            p++;                /* skip delimiter */
            cur++;
        }
        if (cur == field) {
            /* Print from p to next delimiter or EOL */
            char *end = p;
            while (*end && (uint8_t)*end != (uint8_t)delim) end++;
            {
                char save = *end;
                *end = '\0';
                fputs(p);
                *end = save;
            }
        }
        fputs("\r\n");
    }
}
