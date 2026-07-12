/* mkdir.c -- Create one or more directories.
 * Usage: MKDIR DIR [DIR ...]
 */

#include "types.h"
#include "stdio.h"
#include "unistd.h"
#include "args.h"

void __far _main(void)
{
    char *argv[16];
    int argc = args_parse(argv, 16);
    int i;

    if (argc == 0) {
        eputstr("Usage: mkdir DIR [DIR ...]\r\n");
        return;
    }

    for (i = 0; i < argc; i++) {
        if (argv[i][0] == '-') continue;
        if (mkdir(argv[i]) != 0) {
            eputstr("mkdir: cannot create: ");
            eputstr(argv[i]);
            eputstr("\r\n");
        }
    }
}
