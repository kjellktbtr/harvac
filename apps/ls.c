/* ls.c -- List directory contents, DOS-style.
 * Two passes: directories first, then files. Fixed 8+1+3 name column.
 *
 * Compile: wcc -ms -os -s -zl -d0 -bt=dos -i=src/include ls.c
 * Link: wlink system com name LS.COM file ls.obj start _main
 */

#include "types.h"
#include "constants.h"
#include "port_io.h"

static uint8_t dir_buf[sizeof(uint16_t) * 6 + sizeof(void *)];
static uint8_t dirent_buf[32];

/* Forward declarations */
static void putch(uint8_t c);
static void putstr(const char *s);
static void print_rj(uint16_t n, uint8_t width);
static void do_opendir(void);
static uint16_t do_readdir(void);
static void do_closedir(void);
static void print_entry(uint8_t is_dir);

/* _main must be first in the binary (wlink com entry = offset 0x100) */
void __far _main(void)
{
    uint16_t count = 0;

    /* Pass 1: directories */
    do_opendir();
    for (;;) {
        if (do_readdir() != 0)
            break;
        if (dirent_buf[11] & 0x10) {
            print_entry(1);
            count++;
        }
    }
    do_closedir();

    /* Pass 2: files */
    do_opendir();
    for (;;) {
        if (do_readdir() != 0)
            break;
        if (!(dirent_buf[11] & 0x10)) {
            print_entry(0);
            count++;
        }
    }
    do_closedir();

    putstr("        ");
    print_rj(count, 1);
    putstr(" file(s)\r\n");
}

/* Helpers */

static void putch(uint8_t c)
{
    syscall_int40(SYSCALL_WRITE_CHAR, c, 0, 0, 0, 0, 0);
}

static void putstr(const char *s)
{
    syscall_int40(SYSCALL_WRITE_STDOUT, 0, 0, 0, 0, (uint16_t)s, 0);
}

/* Print n right-justified in a field of `width` chars. */
static void print_rj(uint16_t n, uint8_t width)
{
    uint8_t digits[6];
    uint8_t d = 0;
    uint8_t sp;
    do {
        digits[d++] = (uint8_t)('0' + (n % 10));
        n /= 10;
    } while (n > 0 && d < 6);
    sp = (width > d) ? (uint8_t)(width - d) : 0;
    while (sp--)
        putch(' ');
    while (d > 0)
        putch(digits[--d]);
}

static void do_opendir(void)
{
    syscall_int40(SYSCALL_OPENDIR, 0, 0, (uint16_t)dir_buf, 0, 0, 0);
}

static uint16_t do_readdir(void)
{
    return syscall_int40(SYSCALL_READDIR, 0, 0,
                         (uint16_t)dir_buf, (uint16_t)dirent_buf, 0, 0);
}

static void do_closedir(void)
{
    syscall_int40(SYSCALL_CLOSEDIR, 0, 0, (uint16_t)dir_buf, 0, 0, 0);
}

/* Print one entry: name (8 bytes) + sp + ext (3 bytes) + suffix. */
static void print_entry(uint8_t is_dir)
{
    uint8_t i;
    uint16_t fsize;

    for (i = 0; i < 8; i++)
        putch(dirent_buf[i]);
    putch(' ');
    for (i = 8; i < 11; i++)
        putch(dirent_buf[i]);

    if (is_dir) {
        putstr("  <DIR>\r\n");
    } else {
        /* file_size is at offset 28 in fat16_dirent_t; low 16 bits suffice */
        fsize = (uint16_t)dirent_buf[28] | ((uint16_t)dirent_buf[29] << 8);
        print_rj(fsize, 7);
        putstr("\r\n");
    }
}
