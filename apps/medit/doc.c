/* doc.c -- Document layer for Harvac port of MEDIT.
 * File I/O uses SYSCALL_OPEN/READ/WRITE/CREATE/CLOSE. Because SYSCALL_READ
 * and SYSCALL_WRITE copy through caller_ds (the process segment), a small
 * static near buffer is used as an intermediary; far_copy transfers data
 * between that buffer and the far gap segment. */

#include "medit.h"
#include "types.h"
#include "constants.h"
#include "port_io.h"
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

/* Read up to n bytes from handle into far buffer dst. Returns bytes read. */
static u16 read_far(u16 h, u8 __far *dst, u16 n)
{
    static u8 tmp[256];
    u16 total = 0, chunk, nr;
    while (n > 0) {
        chunk = (n > 256) ? 256 : n;
        nr = (u16)syscall_int40(SYSCALL_READ, 0, h, (u16)tmp, chunk, 0, 0);
        if (nr == 0)
            break;
        far_copy(dst + total, (u8 __far *)tmp, nr);
        total += nr;
        n -= nr;
    }
    return total;
}

/* Write n bytes from far buffer src to handle. Returns bytes written. */
static u16 write_far(u16 h, const u8 __far *src, u16 n)
{
    static u8 tmp[256];
    u16 total = 0, chunk, nw;
    while (n > 0) {
        chunk = (n > 256) ? 256 : n;
        far_copy((u8 __far *)tmp, src + total, chunk);
        nw = (u16)syscall_int40(SYSCALL_WRITE, 0, h, (u16)tmp, chunk, 0, 0);
        if (nw == 0)
            break;
        total += nw;
        n -= chunk;
    }
    return total;
}

int doc_load(const char *path)
{
    u16 h, got;
    u8 __far *raw;

    h = (u16)syscall_int40(SYSCALL_OPEN, 0, (u16)path, 0, 0, 0, 0);
    if (h == 0xFFFFu)
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
        syscall_int40(SYSCALL_CLOSE, 0, h, 0, 0, 0, 0);

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
    u16 h, put, len;
    u8 __far *raw;

    h = (u16)syscall_int40(SYSCALL_CREATE, 0, (u16)path, 0, 0, 0, 0);
    if (h == 0xFFFFu)
        return DOC_ERR_OPEN;

    len = gb_len();
    raw = gb_raw();
    if (len > 0) {
        put = write_far(h, raw, len);
        syscall_int40(SYSCALL_CLOSE, 0, h, 0, 0, 0, 0);
        if (put != len)
            return DOC_ERR_IO;
    } else {
        syscall_int40(SYSCALL_CLOSE, 0, h, 0, 0, 0, 0);
    }
    return DOC_OK;
}
