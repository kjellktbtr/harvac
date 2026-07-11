/* clip.c -- Internal clipboard via SYSCALL_ALLOC far segment.
 * SYSCALL_FREE is a no-op in Harvac; all allocations are freed when the
 * process exits and return_to_parent_ resets g_next_seg. */

#include "medit.h"
#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "far.h"
#include "gap.h"
#include "clip.h"

static u16 cseg;
static u16 clen;
static u8  have;

int clip_set(u16 pos, u16 n)
{
    /* Old segment is abandoned (SYSCALL_FREE is no-op; freed on process exit) */
    have = 0;
    clen = 0;

    if (n == 0)
        return 0;

    cseg = (u16)syscall_int40(SYSCALL_ALLOC, 0, (n + 15) >> 4, 0, 0, 0, 0);
    if (cseg == 0)
        return -1;

    have = 1;
    clen = n;
    gb_copy_out(pos, n, (u8 __far *)MK_FP(cseg, 0));
    return 0;
}

u16 clip_len(void)
{
    return clen;
}

u8 __far *clip_ptr(void)
{
    return have ? (u8 __far *)MK_FP(cseg, 0) : (u8 __far *)0;
}
