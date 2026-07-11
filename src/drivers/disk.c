/* disk.c -- INT 13h CHS disk I/O with retry
 *
 * Uses AH=02h/03h CHS transfers (supported by every x86 BIOS, including
 * pre-EDD 386/XT-era BIOSes and XTIDE). The EDD extensions (AH=42h/43h)
 * only exist on ~1995+ BIOSes and are NOT available on the target
 * hardware, even though QEMU/SeaBIOS supports them. */

#include "kernel.h"
#include "drivers/disk.h"

/* Number of retries for disk operations */
#define DISK_RETRIES    3

/* BIOS-reported geometry, queried once (INT 13h AH=08h).
 * spt == 0 means "not yet queried". */
static uint8_t disk_spt;
static uint8_t disk_nheads;

static void disk_ensure_geometry(uint8_t drive)
{
    uint16_t r;

    if (disk_spt != 0)
        return;

    r = disk_query_geom_int(drive);
    disk_spt = (uint8_t)(r & 0x3F);
    disk_nheads = (uint8_t)(r >> 8) + 1;
    if (disk_spt == 0) {
        /* AH=08h unsupported or returned garbage: fall back */
        disk_spt = 63;
        disk_nheads = 16;
    }
}

/* One CHS transfer loop shared by read and write.
 * Transfers one sector at a time so track boundaries never split a
 * single BIOS call. The 32 MB disk means every LBA fits in 16 bits. */
static uint16_t disk_chs_op(uint8_t write, uint8_t drive, uint32_t lba,
                            uint16_t count, uint16_t segment,
                            uint16_t offset)
{
    uint16_t i, l, track, cyl, head, sec, cx, dx, ret;
    uint8_t retry;

    disk_ensure_geometry(drive);

    for (i = 0; i < count; i++) {
        l = (uint16_t)lba + i;
        track = l / disk_spt;
        sec = (l % disk_spt) + 1;           /* 1-based sector */
        head = track % disk_nheads;
        cyl = track / disk_nheads;

        /* INT 13h packing: CH = cyl low 8, CL = sector | cyl bits 9:8
         * in bits 7:6, DH = head, DL = drive */
        cx = (uint16_t)(cyl << 8) | (uint16_t)((cyl & 0x0300) >> 2) | sec;
        dx = (uint16_t)(head << 8) | drive;

        for (retry = 0; retry < DISK_RETRIES; retry++) {
            if (write)
                ret = disk_chs_write_int(cx, dx, segment, offset);
            else
                ret = disk_chs_read_int(cx, dx, segment, offset);
            if (ret == 0)
                break;
            disk_reset_int(drive);
        }
        if (ret != 0)
            return 1;

        segment += 32;      /* advance 512 bytes without offset overflow */
    }
    return 0;
}

/* INT 13h AH=00h: reset disk controller */
uint16_t disk_reset(uint8_t drive)
{
    disk_reset_int(drive);
    return 0;
}

uint16_t disk_read_sectors(uint8_t drive, uint32_t lba, uint16_t count,
                            uint16_t segment, uint16_t offset)
{
    return disk_chs_op(0, drive, lba, count, segment, offset);
}

uint16_t disk_write_sectors(uint8_t drive, uint32_t lba, uint16_t count,
                             uint16_t segment, uint16_t offset)
{
    return disk_chs_op(1, drive, lba, count, segment, offset);
}

uint16_t disk_get_params(uint8_t drive, uint16_t *sectors,
                          uint16_t *heads)
{
    disk_ensure_geometry(drive);

    if (sectors)
        *sectors = disk_spt;
    if (heads)
        *heads = disk_nheads;
    return 0;
}
