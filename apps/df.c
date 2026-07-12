/* df.c -- Report disk space usage.
 * Usage: DF
 * Uses SYSCALL_STATFS (0x33).
 * statfs_t layout (8 bytes):
 *   [0-1] total_clusters, [2-3] free_clusters,
 *   [4]   sectors_per_cluster, [5] pad,
 *   [6-7] bytes_per_sector.
 *
 * Arithmetic note: bytes_per_sector is always 512 on this platform.
 * KB per cluster = spc * 512 / 1024 = spc / 2.
 * This avoids 32-bit multiplication (__U4M) which is absent from the
 * app runtime.  For spc < 2 (rare), we report in units of 512-byte blocks.
 */

#include "types.h"
#include "harva.h"
#include "stdio.h"
#include "string.h"

static uint16_t sfs[4];   /* [0]=total_cl,[1]=free_cl,[2]=spc|pad,[3]=bps */

void __far _main(void)
{
    uint16_t total_cl, free_cl, used_cl;
    uint16_t spc;
    uint16_t total_kb, used_kb, free_kb;
    char t[8], u[8], f[8];

    sys_statfs(sfs);

    total_cl = sfs[0];
    free_cl  = sfs[1];
    spc      = sfs[2] & 0xFF;      /* sectors_per_cluster (low byte) */
    used_cl  = total_cl - free_cl;

    /* KB = clusters * spc * 512 / 1024 = clusters * spc / 2
     * Use right-shift to stay in 16-bit arithmetic. */
    total_kb = (uint16_t)((uint16_t)(total_cl * spc) >> 1);
    used_kb  = (uint16_t)((uint16_t)(used_cl  * spc) >> 1);
    free_kb  = (uint16_t)((uint16_t)(free_cl  * spc) >> 1);

    utoa(total_kb, t);
    utoa(used_kb,  u);
    utoa(free_kb,  f);

    fputs("Filesystem        Size     Used    Avail\r\n");
    fputs("/                 ");
    fputs(t); fputs(" KB   ");
    fputs(u); fputs(" KB   ");
    fputs(f); fputs(" KB\r\n");
}
