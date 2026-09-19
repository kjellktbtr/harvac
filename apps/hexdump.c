/* hexdump.c -- Display file contents in hexadecimal format.
 * Usage: HEXDUMP [FILE ...]
 * With no file arguments reads from stdin (enables piping).
 *
 * NOTE: _main MUST be the first function defined (wlink COM places functions
 * in source order; file offset 0 = entry point at memory 0x0100).
 */

#include "types.h"
#include "fcntl.h"
#include "unistd.h"
#include "stdio.h"
#include "args.h"

#define BYTES_PER_LINE 16

static uint8_t buf[BYTES_PER_LINE];
static uint8_t prev_buf[BYTES_PER_LINE];
static char line[80];
static uint8_t prev_count;

static const char hex_chars[] = "0123456789abcdef";

/* Forward declarations */
static void byte_to_hex(uint8_t b, char *out);
static void offset_to_hex(uint32_t off, char *out);
static void hexdump_fd(int fd);

void __far _main(void)
{
    char *argv[16];
    int argc = args_parse(argv, 16);
    int i;
    int first;

    first = args_first_nonoption(argc, argv);

    if (first >= argc) {
        hexdump_fd(STDIN_FILENO);
        return;
    }

    for (i = first; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            eputstr("hexdump: cannot open: ");
            eputstr(argv[i]);
            eputstr("\r\n");
            continue;
        }
        hexdump_fd(fd);
        close(fd);
    }
}

static void byte_to_hex(uint8_t b, char *out)
{
    out[0] = hex_chars[b >> 4];
    out[1] = hex_chars[b & 0x0F];
}

static void offset_to_hex(uint32_t off, char *out)
{
    uint8_t i;
    for (i = 0; i < 8; i++) {
        out[7 - i] = hex_chars[off & 0x0F];
        off >>= 4;
    }
    out[8] = '\0';
}

static void hexdump_fd(int fd)
{
    uint32_t offset = 0;
    int bytes;
    int show_repeat = 0;
    int i;

    prev_count = 0;

    while ((bytes = read(fd, buf, BYTES_PER_LINE)) > 0) {
        int is_repeat = 0;
        uint8_t pos;
        uint8_t j;

        if (bytes == prev_count) {
            is_repeat = 1;
            for (i = 0; i < bytes; i++) {
                if (buf[i] != prev_buf[i]) {
                    is_repeat = 0;
                    break;
                }
            }
        }

        if (is_repeat) {
            if (!show_repeat) {
                fputs("*\r\n");
                show_repeat = 1;
            }
        } else {
            show_repeat = 0;

            /* offset */
            offset_to_hex(offset, line);
            pos = 8;
            line[pos++] = ' ';
            line[pos++] = ' ';

            /* hex bytes: 8 groups, extra space, 8 groups */
            for (j = 0; j < BYTES_PER_LINE; j++) {
                if (j == 8) {
                    line[pos++] = ' ';
                }
                if (j < bytes) {
                    byte_to_hex(buf[j], line + pos);
                    pos += 2;
                } else {
                    line[pos++] = ' ';
                    line[pos++] = ' ';
                }
                line[pos++] = ' ';
            }

            /* ascii sidebar */
            line[pos++] = ' ';
            line[pos++] = '|';
            for (j = 0; j < bytes; j++) {
                uint8_t c = buf[j];
                line[pos++] = (c >= 32 && c <= 126) ? c : '.';
            }
            line[pos++] = '|';
            line[pos] = '\0';

            fputs(line);
            fputs("\r\n");
        }

        /* save for next comparison */
        for (i = 0; i < bytes; i++) {
            prev_buf[i] = buf[i];
        }
        prev_count = (uint8_t)bytes;
        offset += (uint32_t)bytes;
    }

    /* final offset line */
    if (offset > 0 || prev_count == 0) {
        offset_to_hex(offset, line);
        fputs(line);
        fputs("\r\n");
    }
}