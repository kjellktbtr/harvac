/* free.c -- Display total, used, and free memory.
 * Usage: FREE
 * Uses SYSCALL_MEM_INFO (0x52): returns total and used RAM in paragraphs.
 * Layout: meminfo[0]=total paras, meminfo[1]=used paras.
 */

#include "types.h"
#include "harva.h"
#include "stdio.h"
#include "string.h"

static uint16_t meminfo[2];

void __far _main(void)
{
    uint16_t total_paras, used_paras, free_paras;
    uint16_t total_kb, used_kb, free_kb;
    char t[8], u[8], f[8];

    sys_mem_info(meminfo);
    total_paras = meminfo[0];
    used_paras  = meminfo[1];
    free_paras  = (used_paras <= total_paras) ? total_paras - used_paras : 0;

    /* Paragraphs to KB: 1 para = 16 bytes, 1 KB = 1024 bytes → /64 */
    total_kb = (uint16_t)(total_paras >> 6);
    used_kb  = (uint16_t)(used_paras  >> 6);
    free_kb  = (uint16_t)(free_paras  >> 6);

    utoa(total_kb, t);
    utoa(used_kb,  u);
    utoa(free_kb,  f);

    fputs("          total    used    free\r\n");
    fputs("Mem:  ");
    fputs(t); fputs(" KB total   ");
    fputs(u); fputs(" KB used   ");
    fputs(f); fputs(" KB free\r\n");
}
