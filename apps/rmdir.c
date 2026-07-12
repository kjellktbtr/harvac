/* rmdir.c -- Remove empty directories.
 * Usage: RMDIR DIR [DIR ...]
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
        eputstr("Usage: rmdir DIR [DIR ...]\r\n");
        return;
    }

    for (i = 0; i < argc; i++) {
        if (argv[i][0] == '-') continue;
        if (rmdir(argv[i]) != 0) {
            eputstr("rmdir: cannot remove: ");
            eputstr(argv[i]);
            eputstr("\r\n");
        }
    }
}
