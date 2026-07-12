/* ls.c -- List directory contents.
 * Usage: LS [PATH]
 * Two passes: directories first (with <DIR> tag), then files with size.
 * If PATH is given, changes to it, lists, then restores CWD.
 * Uses libc opendir/readdir/closedir, string helpers, and stdio.
 *
 * NOTE: _main MUST be the first function defined (wlink COM places functions
 * in source order; file offset 0 = entry point at memory 0x0100).
 */

#include "types.h"
#include "dirent.h"
#include "stdio.h"
#include "string.h"
#include "unistd.h"
#include "args.h"
#include "harva.h"

/* _main must be the first function (wlink source-order entry point rule). */
void __far _main(void)
{
    DIR *dp;
    struct dirent *de;
    char name12[13];
    char sizebuf[8];
    uint16_t count = 0;
    char cwd_save[64];
    char *argv[4];
    int argc;
    int first;
    int changed;

    argc    = args_parse(argv, 4);
    first   = args_first_nonoption(argc, argv);
    changed = 0;

    if (first < argc) {
        /* Path argument: cd into it, list, then restore CWD */
        getcwd(cwd_save, sizeof(cwd_save));
        if (chdir(argv[first]) != 0) {
            eputstr("ls: cannot access '");
            eputstr(argv[first]);
            eputstr("'\r\n");
            return;
        }
        changed = 1;
    }

    /* Pass 1: directories */
    dp = opendir(".");
    if (!dp) { eputstr("ls: cannot open directory\r\n"); goto done; }
    while ((de = readdir(dp)) != NULL) {
        if (!(de->d_attr & FAT_ATTR_DIRECTORY)) continue;
        fat_format_name12(de->d_name, name12);
        fputs(name12);
        puts("  <DIR>");
        count++;
    }
    closedir(dp);

    /* Pass 2: regular files */
    dp = opendir(".");
    if (!dp) goto done;
    while ((de = readdir(dp)) != NULL) {
        if (de->d_attr & FAT_ATTR_DIRECTORY) continue;
        if (de->d_attr & FAT_ATTR_VOLUME)    continue;
        fat_format_name12(de->d_name, name12);
        fputs(name12);
        putchar(' ');
        format_size(de->d_size, sizebuf);
        puts(sizebuf);
        count++;
    }
    closedir(dp);

    fputs("        ");
    utoa(count, sizebuf);
    fputs(sizebuf);
    puts(" file(s)");

done:
    if (changed)
        chdir(cwd_save);
}
