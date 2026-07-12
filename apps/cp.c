/* cp.c -- Copy files or directories.
 * Usage: CP [-r] SRC DST
 * -r: recursive copy (directories).
 * Without -r, copying a directory prints an error.
 * With -r, if DST exists as a directory the copy goes into DST/basename(SRC)
 * (Unix semantics); otherwise DST becomes the new copy.
 *
 * NOTE: _main MUST be the first function defined (wlink COM places functions
 * in source order; file offset 0 = entry point at memory 0x0100).
 */

#include "types.h"
#include "stdio.h"
#include "string.h"
#include "unistd.h"
#include "sys/stat.h"
#include "args.h"
#include "fileops.h"

/* Forward declaration — definition is after _main (entry-point guard). */
static const char *path_basename(const char *path);

void __far _main(void)
{
    char *argv[8];
    int argc = args_parse(argv, 8);
    int rflag = args_has_flag(argc, argv, 'r');
    int first = args_first_nonoption(argc, argv);
    const char *src;
    const char *dst;
    char dst_path[FOP_PATH_MAX];
    struct stat src_st;
    struct stat dst_st;

    if (argc - first < 2) {
        eputstr("Usage: cp [-r] SRC DST\r\n");
        return;
    }

    src = argv[first];
    dst = argv[first + 1];

    if (!rflag) {
        /* Without -r: refuse to copy a directory */
        if (stat(src, &src_st) == 0 && S_ISDIR(src_st.st_attr)) {
            eputstr("cp: -r not specified; omitting directory '");
            eputstr(src);
            eputstr("'\r\n");
            return;
        }
        if (copy_file(src, dst) != 0)
            eputstr("cp: copy failed\r\n");
        return;
    }

    /* Recursive copy */
    if (stat(src, &src_st) == 0 && S_ISDIR(src_st.st_attr)) {
        /* If DST exists as a directory, copy INTO it (Unix semantics) */
        if (stat(dst, &dst_st) == 0 && S_ISDIR(dst_st.st_attr)) {
            uint16_t dlen;
            strcpy(dst_path, dst);
            dlen = strlen(dst_path);
            if (dlen > 0 && dst_path[dlen - 1] != '/')
                strcat(dst_path, "/");
            strcat(dst_path, path_basename(src));
            dst = dst_path;
        }
        if (copy_tree(src, dst) != 0)
            eputstr("cp: copy failed\r\n");
    } else {
        /* SRC is a regular file */
        if (copy_file(src, dst) != 0)
            eputstr("cp: copy failed\r\n");
    }
}

/* Return pointer to last path component (after the final '/'). */
static const char *path_basename(const char *path)
{
    const char *p = path;
    const char *last = path;
    while (*p) {
        if (*p == '/') last = p + 1;
        p++;
    }
    return last;
}
