/* less.c -- Page through text output.
 * Usage: LESS [FILE]
 * Keys: Space/PgDn=next page, b/PgUp=prev page, j/Down=next line,
 *       k/Up=prev line, q/ESC=quit.
 * Reads stdin if no file given.
 * Max LESS_MAX_LINES lines of LESS_LINE_WIDTH chars each.
 *
 * NOTE: _main MUST be the first function defined (wlink COM places functions
 * in source order; file offset 0 = entry point at memory 0x0100).
 */

#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "stdio.h"
#include "fcntl.h"
#include "unistd.h"
#include "string.h"
#include "args.h"
#include "hdk_keys.h"

#define LESS_MAX_LINES   150
#define LESS_LINE_WIDTH  80
#define PAGE_ROWS        23     /* content rows per screen (row 23 = status) */

/* Static line buffer in BSS (zeroed by kernel) */
static char lines[LESS_MAX_LINES][LESS_LINE_WIDTH];
static int  nlines;

/* ─── Input buffer for reading ─── */
static char rdline[LESS_LINE_WIDTH];

/* Forward declarations */
static void     read_lines(int fd);
static unsigned getkey(void);
static void     draw_page(int top);

void __far _main(void)
{
    char *argv[8];
    int argc = args_parse(argv, 8);
    int first = args_first_nonoption(argc, argv);
    int top = 0;

    if (first < argc) {
        int fd = open(argv[first], O_RDONLY);
        if (fd < 0) {
            eputstr("less: cannot open: ");
            eputstr(argv[first]);
            eputstr("\r\n");
            return;
        }
        read_lines(fd);
        close(fd);
    } else {
        if (isatty(STDIN_FILENO)) {
            eputstr("less: usage: LESS FILE  (or: CMD | LESS)\r\n");
            return;
        }
        read_lines(STDIN_FILENO);
    }

    if (nlines == 0) return;

    for (;;) {
        unsigned key;

        draw_page(top);
        key = getkey();

        /* Quit: q, Q, ESC */
        if (key == 'q' || key == 'Q' || key == K_ESC)
            break;

        /* Next page: Space, Enter, PgDn */
        if (key == ' ' || key == '\r' || key == K_PGDN) {
            int next = top + PAGE_ROWS;
            if (next >= nlines) next = nlines - 1;
            if (next < 0) next = 0;
            top = next;
        }
        /* Prev page: b, B, PgUp */
        else if (key == 'b' || key == 'B' || key == K_PGUP) {
            top -= PAGE_ROWS;
            if (top < 0) top = 0;
        }
        /* Down one line: j, Down arrow */
        else if (key == 'j' || key == 'J' || key == K_DOWN) {
            if (top < nlines - 1) top++;
        }
        /* Up one line: k, Up arrow */
        else if (key == 'k' || key == 'K' || key == K_UP) {
            if (top > 0) top--;
        }
    }

    /* Clear screen on exit */
    syscall_int40(SYSCALL_CLEAR_SCREEN, 0, 0, 0, 0, 0, 0);
}

/* ─── Read all lines from fd into the lines[] buffer ─── */
static void read_lines(int fd)
{
    int r;
    nlines = 0;
    while (nlines < LESS_MAX_LINES) {
        uint16_t i;
        r = getline_fd(fd, rdline, sizeof(rdline));
        if (r < 0) break;
        for (i = 0; i < LESS_LINE_WIDTH - 1 && rdline[i]; i++)
            lines[nlines][i] = rdline[i];
        lines[nlines][LESS_LINE_WIDTH - 1] = '\0';
        nlines++;
    }
}

/* ─── Read one keypress, normalized via HDK kbd_get ─── */
static unsigned getkey(void)
{
    return kbd_get();
}

/* ─── Display one page starting at line `top` ─── */
static void draw_page(int top)
{
    int i;
    int end = top + PAGE_ROWS;
    char numbuf[8];

    if (end > nlines) end = nlines;

    syscall_int40(SYSCALL_CLEAR_SCREEN, 0, 0, 0, 0, 0, 0);

    /* Print content lines */
    for (i = top; i < end; i++) {
        fputs(lines[i]);
        fputs("\r\n");
    }

    /* Pad to fill content rows (so status is always at the same position) */
    for (i = end - top; i < PAGE_ROWS; i++)
        fputs("\r\n");

    /* Status bar on row 23 */
    fputs("-- lines ");
    utoa((uint16_t)(top + 1), numbuf);
    fputs(numbuf);
    fputs("-");
    utoa((uint16_t)end, numbuf);
    fputs(numbuf);
    fputs("/");
    utoa((uint16_t)nlines, numbuf);
    fputs(numbuf);
    if (end >= nlines)
        fputs("  (END)  q=quit");
    else
        fputs("  q=quit spc=next b=prev");
}
