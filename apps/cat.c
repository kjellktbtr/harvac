/* cat.c -- Read a file and print its contents.
 * Usage: CAT filename
 * Reads filename from PSP command line at 0x0082; falls back to README.TXT.
 *
 * Compile: wcc -ms -os -s -zl -d0 -bt=dos -i=src/include cat.c
 * Link: wlink system com name CAT.COM file cat.obj start _main
 */

#include "types.h"
#include "constants.h"
#include "port_io.h"

/* Buffer for file data */
static uint8_t buf[512];

void __far _main(void)
{
    uint16_t handle;
    uint16_t bytes;
    uint16_t i;
    const char *fname;
    static const char default_name[] = "README.TXT";

    /* Read filename from PSP argv area (written by kernel SYSCALL_EXEC).
     * DS = COM_SEGMENT, so near pointer 0x0082 is correct. */
    fname = (const char *)0x0082;
    if (*fname == '\0')
        fname = default_name;

    /* Open file: AH=0x10, AL=O_RDONLY(0), BX=name pointer */
    handle = syscall_int40(SYSCALL_OPEN, 0, (uint16_t)fname, 0, 0, 0, 0);

    if (handle >= 16) {
        syscall_int40(SYSCALL_WRITE_STDOUT, 0, 0, 0, 0,
                      (uint16_t)"File not found\r\n", 0);
        return;
    }

    /* Read loop: AH=0x12, BX=handle, CX=buffer, DX=count */
    for (;;) {
        bytes = syscall_int40(SYSCALL_READ, 0, handle, (uint16_t)buf, 512, 0, 0);
        if (bytes == 0)
            break;

        for (i = 0; i < bytes; i++) {
            syscall_int40(SYSCALL_WRITE_CHAR, buf[i], 0, 0, 0, 0, 0);
        }
    }

    /* Close file: AH=0x11, BX=handle */
    syscall_int40(SYSCALL_CLOSE, 0, handle, 0, 0, 0, 0);

    syscall_int40(SYSCALL_WRITE_STDOUT, 0, 0, 0, 0,
                  (uint16_t)"\r\n", 0);
}
