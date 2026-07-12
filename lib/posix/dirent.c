/* dirent.c -- POSIX directory iteration for HarvaC userspace.
 * The kernel's OPENDIR/READDIR/CLOSEDIR take fat16_dir_t / fat16_dirent_t
 * pointers (opaque byte arrays). This wrapper converts to/from struct dirent.
 *
 * opendir(path): if path != "." the CWD is changed to path (not restored on
 * closedir; callers must manage their own CWD if needed).
 * Limitation: only 2 simultaneous open directory streams. */

#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "errno.h"
#include "dirent.h"
#include "unistd.h"
#include "string.h"

/* fat16_dir_t is 7 x uint16_t = 14 bytes (kernel opaque struct).
 * fat16_dirent_t is 32 bytes:
 *   name[8], ext[3], attrs, reserved[10], time(2), date(2),
 *   first_cluster(2), file_size(4). */
#define RAW_DIR_BYTES   14
#define RAW_ENT_BYTES   32

/* Internal DIR pool — 2 simultaneous streams */
#define DIR_POOL_SIZE  2

typedef struct {
    uint8_t  rawdir[RAW_DIR_BYTES];   /* fat16_dir_t (kernel opaque) */
    uint8_t  rawent[RAW_ENT_BYTES];   /* fat16_dirent_t for READDIR */
    struct dirent cur;                /* converted last entry */
    uint8_t  in_use;
    uint8_t  done;
} dir_slot_t;

static dir_slot_t _dir_pool[DIR_POOL_SIZE];

/* Build d_name from FAT16 name[8]+ext[3] fields in a raw dirent. */
static void build_name(const uint8_t *raw, char *out)
{
    int i, j = 0;
    for (i = 0; i < 8 && raw[i] != ' '; i++)
        out[j++] = (char)raw[i];
    if (raw[8] != ' ') {
        out[j++] = '.';
        for (i = 0; i < 3 && raw[8 + i] != ' '; i++)
            out[j++] = (char)raw[8 + i];
    }
    out[j] = '\0';
}

DIR *opendir(const char *path)
{
    int i;
    dir_slot_t *s = (dir_slot_t *)0;
    uint16_t r;

    /* Find a free slot */
    for (i = 0; i < DIR_POOL_SIZE; i++) {
        if (!_dir_pool[i].in_use) { s = &_dir_pool[i]; break; }
    }
    if (!s) { errno = EMFILE; return (DIR *)0; }

    /* Change to the requested directory if not "." */
    if (path && !(path[0] == '.' && path[1] == '\0')) {
        if (chdir(path) != 0)
            return (DIR *)0;   /* errno set by chdir */
    }

    memset(s->rawdir, 0, RAW_DIR_BYTES);
    r = (uint16_t)syscall_int40(SYSCALL_OPENDIR, 0, 0,
                                (uint16_t)s->rawdir, 0, 0, 0);
    if (r != 0) { errno = ENOENT; return (DIR *)0; }

    s->in_use = 1;
    s->done   = 0;
    return (DIR *)s;
}

struct dirent *readdir(DIR *dp)
{
    dir_slot_t *s = (dir_slot_t *)dp;
    uint16_t r;
    const uint8_t *re;

    if (!s || !s->in_use) { errno = EBADF; return (struct dirent *)0; }
    if (s->done) return (struct dirent *)0;

    for (;;) {
        r = (uint16_t)syscall_int40(SYSCALL_READDIR, 0, 0,
                                    (uint16_t)s->rawdir,
                                    (uint16_t)s->rawent, 0, 0);
        if (r != 0) { s->done = 1; return (struct dirent *)0; }

        re = s->rawent;

        /* Skip deleted entries (0xE5) and end-of-dir (0x00) */
        if (re[0] == 0) { s->done = 1; return (struct dirent *)0; }
        if (re[0] == (uint8_t)0xE5) continue;

        /* Skip volume labels */
        if (re[11] & 0x08) continue;

        /* Skip "." self-entry (name[0]=='.' name[1]==' ') */
        if (re[0] == '.' && re[1] == ' ') continue;

        break;
    }

    /* Convert raw FAT16 dirent to struct dirent */
    build_name(re, s->cur.d_name);
    s->cur.d_attr = re[11];
    /* time at byte 22, date at byte 24, file_size at byte 28 (little-endian) */
    s->cur.d_time = (uint16_t)re[22] | ((uint16_t)re[23] << 8);
    s->cur.d_date = (uint16_t)re[24] | ((uint16_t)re[25] << 8);
    s->cur.d_size = (uint32_t)re[28]
                  | ((uint32_t)re[29] << 8)
                  | ((uint32_t)re[30] << 16)
                  | ((uint32_t)re[31] << 24);

    return &s->cur;
}

int closedir(DIR *dp)
{
    dir_slot_t *s = (dir_slot_t *)dp;
    if (!s || !s->in_use) { errno = EBADF; return -1; }
    syscall_int40(SYSCALL_CLOSEDIR, 0, 0, (uint16_t)s->rawdir, 0, 0, 0);
    s->in_use = 0;
    return 0;
}
