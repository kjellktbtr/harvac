#ifndef EXEC_H
#define EXEC_H

#include "types.h"
#include "drivers/fat16.h"

/* Load and execute a .COM file from FAT16.
 * Returns 0 on error (file not found, etc.).
 * On success, never returns — the .COM program runs instead. */
uint16_t exec_com(fat16_fs_t *fs, const uint8_t *name_83);

#endif /* EXEC_H */