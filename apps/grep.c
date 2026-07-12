/* grep.c -- Search for a pattern in files or stdin.
 * Usage: GREP [-i] PATTERN [FILE ...]
 * -i: case-insensitive matching (plain substring, no regex)
 *
 * NOTE: _main MUST be the first function defined (wlink COM places functions
 * in source order; file offset 0 = entry point at memory 0x0100).
 */

#include "types.h"
#include "stdio.h"
#include "fcntl.h"
#include "unistd.h"
#include "string.h"
#include "args.h"

static char linebuf[256];

/* Forward declarations */
static uint8_t fold(uint8_t c);
static int     contains(const char *haystack, const char *needle, int icase);
static void    grep_fd(int fd, const char *pattern, int icase);

void __far _main(void)
{
    char *argv[16];
    int argc = args_parse(argv, 16);
    int icase = args_has_flag(argc, argv, 'i');
    int first = args_first_nonoption(argc, argv);
    const char *pattern;
    int i;

    if (first >= argc) {
        eputstr("Usage: grep [-i] PATTERN [FILE ...]\r\n");
        return;
    }

    pattern = argv[first];
    first++;

    if (first >= argc) {
        /* No files: grep stdin */
        grep_fd(STDIN_FILENO, pattern, icase);
        return;
    }

    for (i = first; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            eputstr("grep: cannot open: ");
            eputstr(argv[i]);
            eputstr("\r\n");
            continue;
        }
        grep_fd(fd, pattern, icase);
        close(fd);
    }
}

/* ─── Case-fold helper ─── */
static uint8_t fold(uint8_t c)
{
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c + 32);
    return c;
}

/* ─── Substring search (plain, optional case-insensitive) ─── */
static int contains(const char *haystack, const char *needle, int icase)
{
    uint16_t hlen = strlen(haystack);
    uint16_t nlen = strlen(needle);
    uint16_t i, j;

    if (nlen == 0) return 1;
    if (hlen < nlen) return 0;

    for (i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        for (j = 0; j < nlen; j++) {
            uint8_t h = (uint8_t)haystack[i + j];
            uint8_t n = (uint8_t)needle[j];
            if (icase) { h = fold(h); n = fold(n); }
            if (h != n) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

/* ─── grep one fd ─── */
static void grep_fd(int fd, const char *pattern, int icase)
{
    int n;
    while ((n = getline_fd(fd, linebuf, sizeof(linebuf))) >= 0) {
        if (contains(linebuf, pattern, icase)) {
            fputs(linebuf);
            fputs("\r\n");
        }
    }
}
