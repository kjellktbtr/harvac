/* doc.c -- Document layer for Harvac port of MEDIT.
 * File I/O goes through POSIX open/read/write/close. A small static near
 * buffer is used as intermediary for read_far/write_far; far_copy moves data
 * between that buffer and the far gap segment. */

#include "medit.h"
#include "types.h"
#include "fcntl.h"
#include "unistd.h"
#include "gap.h"
#include "doc.h"

u16 doc_line_home(u16 pos)
{
    while (pos > 0 && gb_at(pos - 1) != '\n')
        pos--;
    return pos;
}

u16 doc_line_end(u16 pos)
{
    u16 n = gb_len();
    u8 c;
    while (pos < n) {
        c = gb_at(pos);
        if (c == '\r' || c == '\n')
            break;
        pos++;
    }
    return pos;
}

u16 doc_next_line(u16 endpos)
{
    u16 n = gb_len();
    if (endpos >= n)
        return n;
    if (gb_at(endpos) == '\r') {
        endpos++;
        if (endpos < n && gb_at(endpos) == '\n')
            endpos++;
    } else {
        endpos++;
    }
    return endpos;
}

u16 doc_prev_line(u16 start)
{
    u16 p;
    if (start == 0)
        return 0;
    p = start - 1;
    if (gb_at(p) == '\n' && p > 0 && gb_at(p - 1) == '\r')
        p--;
    return doc_line_home(p);
}

/* Read up to n bytes from fd into far buffer dst. Returns bytes read. */
static u16 read_far(int h, u8 __far *dst, u16 n)
{
    static u8 tmp[256];
    u16 total = 0, chunk;
    int nr;
    while (n > 0) {
        chunk = (n > 256) ? 256 : n;
        nr = read(h, tmp, chunk);
        if (nr <= 0)
            break;
        far_copy(dst + total, (u8 __far *)tmp, (u16)nr);
        total += (u16)nr;
        n -= (u16)nr;
    }
    return total;
}

/* Write n bytes from far buffer src to fd. Returns bytes written. */
static u16 write_far(int h, const u8 __far *src, u16 n)
{
    static u8 tmp[256];
    u16 total = 0, chunk;
    int nw;
    while (n > 0) {
        chunk = (n > 256) ? 256 : n;
        far_copy((u8 __far *)tmp, src + total, chunk);
        nw = write(h, tmp, chunk);
        if (nw <= 0)
            break;
        total += (u16)nw;
        n -= chunk;
    }
    return total;
}

int doc_load(const char *path)
{
    int h;
    u16 got;
    u8 __far *raw;

    h = open(path, O_RDONLY);
    if (h < 0)
        return DOC_ERR_OPEN;

    gb_reset();
    raw = gb_raw();
    /* Max text = capacity minus 1000 bytes gap slack; equals GB_MAX_TEXT
     * for the full-size buffer, proportionally less for the low-memory
     * fallback sizes (see gb_init). Read one extra byte to detect
     * too-large files. */
    {
        u16 maxt = (u16)(gb_cap() - 1000);
        got = read_far(h, raw, (u16)(maxt + 1));
        close(h);

        if (got > maxt) {
            gb_reset();
            return DOC_ERR_BIG;
        }
    }
    gb_set_len(got);
    return DOC_OK;
}

int doc_save(const char *path)
{
    int h;
    u16 put, len;
    u8 __far *raw;

    h = creat(path);
    if (h < 0)
        return DOC_ERR_OPEN;

    len = gb_len();
    raw = gb_raw();
    if (len > 0) {
        put = write_far(h, raw, len);
        close(h);
        if (put != len)
            return DOC_ERR_IO;
    } else {
        close(h);
    }
    return DOC_OK;
}
