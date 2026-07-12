/* dirent.h -- POSIX-like directory iteration for HarvaC userspace.
 * Hides the raw fat16_dir_t / fat16_dirent_t byte layout from apps.
 * The kernel's OPENDIR/READDIR/CLOSEDIR syscalls operate in the CWD. */
#ifndef DIRENT_H
#define DIRENT_H

#include "types.h"

/* Maximum 8.3 filename + null terminator */
#define NAME_MAX    13

/* Directory entry as seen by userspace */
struct dirent {
    char     d_name[NAME_MAX];  /* null-terminated 8.3 name ("FILE.TXT") */
    uint8_t  d_attr;            /* FAT16 attribute byte (DIR_ATTR_* from constants.h) */
    uint16_t d_date;            /* FAT16 packed date */
    uint16_t d_time;            /* FAT16 packed time */
    uint32_t d_size;            /* File size in bytes (0 for directories) */
};

/* Opaque directory handle.  Sized to hold the kernel's fat16_dir_t
 * (7 x uint16_t fields = 14 bytes) plus one dirent for readdir prefetch. */
typedef struct {
    uint16_t _raw[7];           /* kernel fat16_dir_t (opaque) */
    struct dirent _cur;         /* last entry fetched by readdir */
    uint8_t  _valid;            /* _cur is populated */
    uint8_t  _done;             /* end of directory */
} DIR;

/* Open the current working directory for reading.
 * Returns pointer to a statically allocated DIR, or NULL on error. */
DIR *opendir(const char *path);

/* Read the next directory entry. Returns pointer to a static struct dirent
 * (overwritten on each call), or NULL at end / on error. */
struct dirent *readdir(DIR *dp);

/* Close a directory stream. Returns 0 on success. */
int closedir(DIR *dp);

#endif /* DIRENT_H */
