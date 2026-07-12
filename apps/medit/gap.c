/* gap.c -- Gap buffer in a far segment allocated via SYSCALL_ALLOC.
 * Replaces DOS _dos_allocmem/_dos_setblock with Harvac INT 0x40 calls.
 * _fmemcpy/_fmemmove replaced with far_copy/far_move from far.h. */

#include "medit.h"
#include "types.h"
#include "stdlib.h"
#include "gap.h"

static u8 __far *buf;
static u16 cap;             /* total bytes in far segment */
static u16 gs, ge;          /* gap = [gs, ge) */

int gb_init(void)
{
    /* Prefer 48000 bytes; fall back to 32 KB then 16 KB on low-memory
     * machines (256 KB IBM 5155). SYSCALL_ALLOC returns 0 when the block
     * would not fit in physical RAM. */
    static const u16 want[3] = { GB_WANT_PARAS, 0x0800, 0x0400 };
    u16 seg = 0;
    u16 paras = 0;
    u8 i;

    for (i = 0; i < 3 && seg == 0; i++) {
        paras = want[i];
        seg = alloc_paras(paras);
    }
    if (seg == 0)
        return -1;
    buf = (u8 __far *)MK_FP(seg, 0);
    cap = (u16)(paras << 4);
    gs = 0;
    ge = cap;
    return 0;
}

void gb_reset(void)
{
    gs = 0;
    ge = cap;
}

u16 gb_len(void)
{
    return cap - (ge - gs);
}

u16 gb_cap(void)
{
    return cap;
}

u8 gb_at(u16 pos)
{
    return pos < gs ? buf[pos] : buf[pos + (ge - gs)];
}

static void gb_move(u16 pos)
{
    u16 gap = ge - gs;
    if (pos == gs)
        return;
    if (pos < gs)
        far_move(buf + pos + gap, buf + pos, gs - pos);
    else
        far_move(buf + gs, buf + ge, pos - gs);
    gs = pos;
    ge = pos + gap;
}

int gb_insert(u16 pos, u8 c)
{
    if (gs == ge)
        return -1;
    gb_move(pos);
    buf[gs++] = c;
    return 0;
}

int gb_insert_block(u16 pos, const u8 __far *src, u16 n)
{
    if ((u16)(ge - gs) < n)
        return -1;
    gb_move(pos);
    far_copy(buf + gs, src, n);
    gs += n;
    return 0;
}

void gb_del(u16 pos, u16 n)
{
    u16 len = gb_len();
    if (pos >= len)
        return;
    if (n > len - pos)
        n = len - pos;
    gb_move(pos);
    ge += n;
}

void gb_copy_out(u16 pos, u16 n, u8 __far *dst)
{
    while (n--)
        *dst++ = gb_at(pos++);
}

u8 __far *gb_raw(void)
{
    gb_move(gb_len());
    return buf;
}

void gb_set_len(u16 n)
{
    if (n > cap)
        n = cap;
    gs = n;
    ge = cap;
}
