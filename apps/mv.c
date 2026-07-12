/* mv.c -- Move or rename files and directories.
 * Usage: MV SRC DST
 * Tries rename() first; if that fails (cross-directory), copies then deletes.
 */

#include "types.h"
#include "stdio.h"
#include "unistd.h"
#include "args.h"
#include "fileops.h"

void __far _main(void)
{
    char *argv[8];
    int argc = args_parse(argv, 8);
    int first = args_first_nonoption(argc, argv);
    const char *src, *dst;

    if (argc - first < 2) {
        eputstr("Usage: mv SRC DST\r\n");
        return;
    }

    src = argv[first];
    dst = argv[first + 1];

    /* Try rename first (same-directory, fast path) */
    if (rename(src, dst) == 0)
        return;

    /* Fall back to copy + delete (cross-directory move) */
    if (copy_tree(src, dst) != 0) {
        eputstr("mv: copy failed\r\n");
        return;
    }
    if (remove_tree(src) != 0)
        eputstr("mv: source delete failed\r\n");
}
