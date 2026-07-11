#ifndef DRIVERS_DISK_H
#define DRIVERS_DISK_H

#include "types.h"

/* Reset disk controller */
uint16_t disk_reset(uint8_t drive);

/* Read sectors with retry. Returns 0 on success, nonzero on failure. */
uint16_t disk_read_sectors(uint8_t drive, uint32_t lba, uint16_t count,
                           uint16_t segment, uint16_t offset);

/* Write sectors with retry. Returns 0 on success, nonzero on failure. */
uint16_t disk_write_sectors(uint8_t drive, uint32_t lba, uint16_t count,
                            uint16_t segment, uint16_t offset);

/* Get drive parameters (CHS). Returns 0 on success. */
uint16_t disk_get_params(uint8_t drive, uint16_t *sectors,
                         uint16_t *heads);

#endif /* DRIVERS_DISK_H */