/* cat.c -- Read a file and print its contents.
 * Usage: CAT filename
 * Reads filename from PSP command line at 0x0082; falls back to README.TXT.
 * Uses libc open/read/write/close and stdio.
 */

#include "types.h"
#include "fcntl.h"
#include "unistd.h"
#include "stdio.h"

static uint8_t buf[512];

void __far _main(void)
{
    const char *fname;
    int fd;
    int bytes;

    /* Read filename from PSP argv area */
    fname = (const char *)0x0082;
    while (*fname == ' ') fname++;
    if (*fname == '\0')
        fname = "README.TXT";

    fd = open(fname, O_RDONLY);
    if (fd < 0) {
        eputstr("cat: file not found\r\n");
        return;
    }

    while ((bytes = read(fd, buf, sizeof(buf))) > 0) {
        write(STDOUT_FILENO, buf, (uint16_t)bytes);
    }

    close(fd);
    fputs("\r\n");
}
