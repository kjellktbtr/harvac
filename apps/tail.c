/* tail.c -- Output the last N lines of files or stdin.
 * Usage: TAIL [-n N] [FILE ...]
 * Default N = 10.  Caps at TAIL_MAX lines.
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

/* Ring buffer: stores up to TAIL_MAX lines */
#define TAIL_MAX    100
#define TAIL_WIDTH  128

static char ring[TAIL_MAX][TAIL_WIDTH];
static int  ring_count;     /* lines stored (may wrap to TAIL_MAX) */
static int  ring_head;      /* index of oldest line when wrapped */
static int  ring_full;

static char linebuf[TAIL_WIDTH];

/* Forward declarations */
static void ring_push(const char *line, int n);
static void ring_flush(int n);
static void tail_fd(int fd, int n);

void __far _main(void)
{
    char *argv[16];
    int argc = args_parse(argv, 16);
    const char *nval = args_flag_val(argc, argv, 'n');
    int n = nval ? atoi(nval) : 10;
    int first = args_first_nonoption(argc, argv);
    int i;

    if (n <= 0 || n > TAIL_MAX) n = 10;

    if (first >= argc) {
        tail_fd(STDIN_FILENO, n);
        return;
    }

    for (i = first; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            eputstr("tail: cannot open: ");
            eputstr(argv[i]);
            eputstr("\r\n");
            continue;
        }
        tail_fd(fd, n);
        close(fd);
    }
}

static void ring_push(const char *line, int n)
{
    int slot;
    uint16_t i;

    if (ring_count < n) {
        slot = ring_count;
        ring_count++;
    } else {
        slot = ring_head;
        ring_head = (ring_head + 1 < n) ? ring_head + 1 : 0;
        ring_full = 1;
    }

    for (i = 0; i < TAIL_WIDTH - 1 && line[i]; i++)
        ring[slot][i] = line[i];
    ring[slot][i] = '\0';
}

static void ring_flush(int n)
{
    int i, idx;
    int total = ring_full ? n : ring_count;

    for (i = 0; i < total; i++) {
        idx = (ring_head + i) % n;
        fputs(ring[idx]);
        fputs("\r\n");
    }
}

static void tail_fd(int fd, int n)
{
    int r;
    ring_count = 0;
    ring_head  = 0;
    ring_full  = 0;

    if (n > TAIL_MAX) n = TAIL_MAX;

    while ((r = getline_fd(fd, linebuf, sizeof(linebuf))) >= 0)
        ring_push(linebuf, n);

    ring_flush(n);
}
