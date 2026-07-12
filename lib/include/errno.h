/* errno.h -- POSIX-like error codes for HarvaC userspace.
 * Maps kernel ERR_* constants to POSIX errno values.
 * The global errno is set by failing libc wrappers (open/read/write etc.).
 * Wrappers return -1 (or NULL) on failure and set errno. */
#ifndef ERRNO_H
#define ERRNO_H

/* Errno codes — match POSIX names; values chosen for clarity, not DOS compat */
#define ENOENT       2    /* No such file or directory  (ERR_NOT_FOUND)     */
#define EACCES       13   /* Permission denied          (ERR_ACCESS_DENIED) */
#define EBADF        9    /* Bad file descriptor        (ERR_INVALID_HANDLE) */
#define EINVAL       22   /* Invalid argument           (ERR_INVALID_PARAM)  */
#define ENOMEM       12   /* Out of memory              (ERR_NO_MEMORY)      */
#define EIO          5    /* I/O error                  (ERR_DISK_ERROR)     */
#define EEXIST       17   /* File exists                (ERR_FILE_EXISTS)    */
#define EMFILE       24   /* Too many open files        (ERR_TOO_MANY_FILES) */
#define ENOSYS       38   /* Function not implemented   (ERR_NOT_IMPLEMENTED)*/
#define ENODEV       19   /* No such device                                  */
#define EFAULT       14   /* Bad address (NULL or invalid path)              */

/* The global errno variable — defined in lib/posix/errno.c */
extern int errno;

/* Map a HarvaC kernel error code (ERR_* from constants.h) to errno.
 * Returns -1 so callers can write: return set_errno(err); */
int set_errno(unsigned err);

#endif /* ERRNO_H */
