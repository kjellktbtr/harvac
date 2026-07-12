/* errno.c -- Global errno variable + kernel error code mapper. */

#include "constants.h"
#include "errno.h"

int errno = 0;

/* Map a kernel ERR_* code to a POSIX errno value and return -1.
 * Used by all POSIX wrappers: return set_errno(kernel_err); */
int set_errno(unsigned err)
{
    switch (err) {
    case 0:  errno = 0;       break;   /* ERR_NONE */
    case 1:  errno = ENOENT;  break;   /* ERR_NOT_FOUND */
    case 2:  errno = EACCES;  break;   /* ERR_ACCESS_DENIED */
    case 3:  errno = EBADF;   break;   /* ERR_INVALID_HANDLE */
    case 4:  errno = EINVAL;  break;   /* ERR_INVALID_PARAM */
    case 5:  errno = ENOMEM;  break;   /* ERR_NO_MEMORY */
    case 6:  errno = EIO;     break;   /* ERR_DISK_ERROR */
    case 7:  errno = EEXIST;  break;   /* ERR_FILE_EXISTS */
    case 8:  errno = EMFILE;  break;   /* ERR_TOO_MANY_FILES */
    case 10: errno = ENOSYS;  break;   /* ERR_NOT_IMPLEMENTED */
    default: errno = EIO;     break;
    }
    return -1;
}
