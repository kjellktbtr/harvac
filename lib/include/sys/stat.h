/* sys/stat.h -- POSIX-like file status for HarvaC userspace. */
#ifndef SYS_STAT_H
#define SYS_STAT_H

#include "types.h"

/* FAT16 attribute bit tests (file type helpers) */
#define S_IFMT      0x0030  /* type mask (subset of FAT16 attrs) */
#define S_IFREG     0x0000  /* regular file */
#define S_IFDIR     0x0010  /* directory (FAT16 ATTR_DIRECTORY) */

#define S_ISDIR(m)  (((m) & 0x10) != 0)
#define S_ISREG(m)  (((m) & 0x18) == 0)   /* not volume, not dir */

struct stat {
    uint32_t  st_size;   /* file size in bytes */
    uint8_t   st_attr;   /* FAT16 attribute byte */
    uint16_t  st_mtime;  /* FAT16 packed modification time */
    uint16_t  st_mdate;  /* FAT16 packed modification date */
};

/* Stat a file by path. Returns 0 on success, -1 on error (errno set). */
int stat(const char *path, struct stat *st);

/* Stat an open file by fd. Returns 0 on success, -1 on error. */
int fstat(int fd, struct stat *st);

/* Create a directory. Returns 0 on success, -1 on error. */
int mkdir_p(const char *path);   /* mkdir() clashes with unistd.h on some hosts */

#endif /* SYS_STAT_H */
