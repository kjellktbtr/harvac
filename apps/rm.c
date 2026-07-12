/* rm.c -- Remove files or directories.
 * Usage: RM [-r] FILE [FILE ...]
 * -r: recursive (delete directory tree)
 */

#include "types.h"
#include "stdio.h"
#include "unistd.h"
#include "args.h"
#include "fileops.h"

void __far _main(void)
{
    char *argv[16];
    int argc = args_parse(argv, 16);
    int rflag = args_has_flag(argc, argv, 'r');
    int first = args_first_nonoption(argc, argv);
    int i;

    if (first >= argc) {
        eputstr("Usage: rm [-r] FILE [FILE ...]\r\n");
        return;
    }

    for (i = first; i < argc; i++) {
        int r;
        if (rflag)
            r = remove_tree(argv[i]);
        else
            r = unlink(argv[i]);
        if (r != 0) {
            eputstr("rm: cannot remove: ");
            eputstr(argv[i]);
            eputstr("\r\n");
        }
    }
}
