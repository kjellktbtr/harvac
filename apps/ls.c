/* ls.c -- List directory contents.
 * Two passes: directories first (with <DIR> tag), then files with size.
 * Uses libc opendir/readdir/closedir, string helpers, and stdio.
 */

#include "types.h"
#include "dirent.h"
#include "stdio.h"
#include "string.h"
#include "harva.h"

/* _main must be the first function (wlink source-order entry point rule). */
void __far _main(void)
{
    DIR *dp;
    struct dirent *de;
    char name12[13];
    char sizebuf[8];
    uint16_t count = 0;

    /* Pass 1: directories */
    dp = opendir(".");
    if (!dp) { eputstr("ls: cannot open directory\r\n"); return; }
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
    if (!dp) return;
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
}
