/* unistd.h -- POSIX-like low-level I/O and process API for HarvaC userspace.
 * All I/O goes through the fd table defined in lib/posix/fd.c.
 * stdin=0, stdout=1, stderr=2 are pre-wired to console syscalls. */
#ifndef UNISTD_H
#define UNISTD_H

#include "types.h"

/* Standard fd constants */
#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

/* Seek whence (match constants.h, duplicated for POSIX include style) */
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

/* off_t: 32-bit file offset (widens from the 16-bit types.h off_t) */
typedef int32_t off_t;

/* --- I/O --- */
/* Read up to count bytes from fd into buf. Returns bytes read, 0 at EOF,
 * -1 on error. */
int    read(int fd, void *buf, uint16_t count);

/* Write count bytes from buf to fd. Returns bytes written or -1 on error. */
int    write(int fd, const void *buf, uint16_t count);

/* Close fd. Returns 0 on success, -1 on error. */
int    close(int fd);

/* Seek fd to position (whence: SEEK_SET/CUR/END).
 * Returns new file position or -1 on error.
 * Note: SEEK_CUR/SEEK_END require kernel whence support (Phase 2). */
off_t  lseek(int fd, off_t offset, int whence);

/* Returns 1 if fd refers to a terminal (console/serial), 0 otherwise. */
int    isatty(int fd);

/* --- Process --- */
/* Exit the current process (does not return). */
void   _exit(int status);

/* Return the current process id (minimal; slot index). */
int    getpid(void);

/* Execute a program. cmd = path to .COM (relative or absolute).
 * args = argument string (may be NULL). Does not return on success. */
int    spawn(const char *cmd, const char *args);

/* --- Filesystem --- */
int    unlink(const char *path);
int    rmdir(const char *path);
int    mkdir(const char *path);
int    chdir(const char *path);
char  *getcwd(char *buf, uint16_t size);
int    rename(const char *oldpath, const char *newpath);

/* Set FAT16 modification date/time on a file by path.
 * date and time are FAT16-packed fields (same encoding as struct stat
 * st_mdate / st_mtime).  Returns 0 on success, -1 on error. */
int    utime(const char *path, uint16_t date, uint16_t time);

#endif /* UNISTD_H */
