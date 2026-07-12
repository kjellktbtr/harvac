/* syscalls.c -- Syscall dispatch table for INT 0x40 */

#include "kernel.h"
#include "syscall.h"
#include "constants.h"
#include "drivers/video.h"
#include "drivers/serial.h"
#include "drivers/keyboard.h"
#include "drivers/disk.h"
#include "drivers/fat16.h"
#include "drivers/serial.h"
#include "lib/strings.h"
#include "fs/vfs.h"
#include "fs/mount.h"

/* ─── Open file table ─── */
#define MAX_OPEN_FILES  16

/* open_file_entry_t.kind values */
#define OF_KIND_FILE    0   /* regular FAT16 file */
#define OF_KIND_SERIAL  1   /* /dev/com1, /dev/tty, CON device fd */

typedef struct {
    uint16_t      used;   /* 0 = free */
    uint8_t       kind;   /* OF_KIND_FILE or OF_KIND_SERIAL */
    fat16_file_t  file;
} open_file_entry_t;

static open_file_entry_t open_files[MAX_OPEN_FILES];

/* ─── Stdout redirect ─── */
/* g_redirect: 0xFFFF = serial (normal), else index into open_files[].
 * g_ob: must be static (SS!=DS during syscalls). */
static uint16_t g_redirect = 0xFFFF;
static uint8_t  g_ob;

static void out_byte(uint8_t c)
{
    if (g_redirect < MAX_OPEN_FILES && open_files[g_redirect].used) {
        g_ob = c;
        fat16_write(&open_files[g_redirect].file, &g_ob, 1);
    } else {
        serial_putchar((char)c);
        video_tty_putchar((char)c);
    }
}

/* Close any active redirect; called by boot_shell() on exec re-entry. */
void syscall_finalize_redirect(void)
{
    if (g_redirect < MAX_OPEN_FILES) {
        open_files[g_redirect].used = 0;
    }
    g_redirect = 0xFFFF;
}

/* ─── Stdin redirect ─── */
/* g_stdin: 0xFFFF = keyboard (normal), else index into open_files[].
 * Enables pipe support: shell sets g_stdin to a temp file before exec. */
static uint16_t g_stdin = 0xFFFF;

/* Close any active stdin redirect; called by boot_shell() on exec re-entry. */
void syscall_finalize_stdin(void)
{
    if (g_stdin < MAX_OPEN_FILES) {
        open_files[g_stdin].used = 0;
    }
    g_stdin = 0xFFFF;
}

/* ─── Mounted FAT16 filesystem (single partition, no VFS yet) ─── */
static fat16_fs_t root_fs;

/* Return pointer to root filesystem (for booting SHELL.COM) */
fat16_fs_t *syscall_get_root_fs(void)
{
    return &root_fs;
}

/* Segment where SHELL.COM is loaded (resident) */
#define COM_SEGMENT    0x3000

/* Defined in exec_stub.asm: restores kernel stack and re-enters shell. */
extern void exec_reentry(void);
/* Defined in syscall.asm: push parent context onto pctx stack; AX = child_seg. */
extern void exec_save_parent_ctx(uint16_t child_seg);
/* Defined in syscall.asm: child return trampoline; falls through to return_to_parent. */
extern void child_return(void);
/* Defined in syscall.asm: pop pctx and iret to parent, or restart shell. Never returns. */
extern void return_to_parent(void);
/* Defined in syscall.asm: read/write the CS-relative g_next_seg allocator. */
extern uint16_t get_next_seg(void);
extern void set_next_seg(uint16_t v);

/* Far jump to user .COM with caller-supplied initial SP.
 * Sets SS:SP = target_seg:sp_init, pushes return seg:off, far-jumps.
 * parm: target_seg=DX, target_off=AX, return_seg=BX, return_off=CX, sp_init=SI */
void exec_far_jump_sp(uint16_t target_seg, uint16_t target_off,
                      uint16_t return_seg, uint16_t return_off,
                      uint16_t sp_init);
#pragma aux exec_far_jump_sp = \
    "cli" \
    "mov ss, dx" \
    "mov sp, si" \
    "sti" \
    "mov ds, dx" \
    "mov es, dx" \
    "push bx" \
    "push cx" \
    "push dx" \
    "push ax" \
    "retf" \
    parm [dx] [ax] [bx] [cx] [si] modify [ax bx cx dx si];

/* ─── Convert "NAME.EXT" to 8.3 packed format ─── */
/* Reads the filename from segment:offset using read_far_b.
 * If the string contains '/', only the last path component is packed. */
static void name_to_83(uint16_t seg, uint16_t off, uint8_t *out)
{
    /* Static: kernel syscall convention (see SYSCALL_READ). */
    static char comp[32];
    uint16_t i;
    uint16_t last = 0;
    uint8_t c;

    /* Skip to the character after the last '/' */
    for (i = 0; ; i++) {
        c = read_far_b(seg, off + i);
        if (c == '\0')
            break;
        if (c == '/')
            last = i + 1;
    }

    for (i = 0; i < sizeof(comp) - 1; i++) {
        c = read_far_b(seg, off + last + i);
        comp[i] = (char)c;
        if (c == '\0')
            break;
    }
    comp[sizeof(comp) - 1] = '\0';

    vfs_name_to_83(comp, out);
}

/* ─── Resolve a user path to {fs, directory cluster, 8.3 name} ─── */
/* Reads the path string from seg:off, makes it absolute against the CWD,
 * resolves all directory components and packs the final component in 8.3.
 * *dir_cluster_out receives 0 for the root directory.
 * Returns 0 on success, ERR_NOT_FOUND if a directory component is missing,
 * ERR_INVALID_PARAM if the path has no final component (e.g. "/"). */
static uint16_t resolve_user_path(uint16_t seg, uint16_t off,
                                  fat16_fs_t **fs_out,
                                  uint16_t *dir_cluster_out,
                                  uint8_t *name83_out)
{
    /* Static: kernel syscall convention (see SYSCALL_READ). */
    static char upath[VFS_MAX_PATH];
    static char apath[VFS_MAX_PATH];
    static mount_entry_t *mnt;
    static uint16_t dir_cluster;
    uint16_t i;
    uint16_t last;

    for (i = 0; i < VFS_MAX_PATH - 1; i++) {
        uint8_t c = read_far_b(seg, off + i);
        upath[i] = (char)c;
        if (c == 0)
            break;
    }
    upath[VFS_MAX_PATH - 1] = '\0';

    vfs_abspath(upath, apath, VFS_MAX_PATH);

    /* Split into directory part and final component */
    last = 0;
    for (i = 0; apath[i] != '\0'; i++) {
        if (apath[i] == '/')
            last = i;
    }
    if (apath[last + 1] == '\0')
        return ERR_INVALID_PARAM;   /* no final component ("/", "/TMP/") */

    vfs_name_to_83(apath + last + 1, name83_out);

    /* Terminate the directory part (keep at least "/") */
    if (last == 0)
        apath[1] = '\0';
    else
        apath[last] = '\0';

    if (vfs_resolve_dir(apath, &mnt, &dir_cluster) != 0)
        return ERR_NOT_FOUND;

    if (fs_out)
        *fs_out = &mnt->fs;
    *dir_cluster_out = dir_cluster;
    return 0;
}

/* ─── Resolve the VFS CWD to a directory cluster ─── */
/* Returns 1 and sets *cluster_out when the CWD is a subdirectory;
 * returns 0 when the CWD is the root (or cannot be resolved). */
static uint16_t cwd_dir_cluster(uint16_t *cluster_out)
{
    /* Static: kernel syscall convention (see SYSCALL_READ). */
    static char cwd_path[VFS_MAX_CWD];
    static uint16_t cluster;

    vfs_getcwd(cwd_path, VFS_MAX_CWD);
    if (vfs_resolve_dir(cwd_path, NULL, &cluster) != 0 || cluster == 0)
        return 0;

    *cluster_out = cluster;
    return 1;
}

/* ─── Device path detection ─── */
/* Returns 1 if the NUL-terminated path in caller_ds:path_off refers to a
 * recognised device (serial/console), 0 otherwise. Used by SYSCALL_OPEN to
 * create device fds without going through the FAT16 layer. */
static int is_device_path(uint16_t caller_ds, uint16_t path_off)
{
    /* Read up to 12 characters to compare */
    static char buf[16];
    uint16_t i;
    uint8_t c;
    const char *devs[] = { "CON", "/dev/tty", "/dev/com1", "/dev/CON" };
    uint16_t ndvs = 4;
    uint16_t d;

    for (i = 0; i < 15; i++) {
        c = read_far_b(caller_ds, path_off + i);
        buf[i] = (char)c;
        if (c == 0) break;
    }
    buf[15] = '\0';

    /* Case-insensitive compare against known device names */
    for (d = 0; d < ndvs; d++) {
        const char *dev = devs[d];
        uint16_t j;
        for (j = 0; buf[j] && dev[j]; j++) {
            uint8_t a = (uint8_t)buf[j];
            uint8_t b = (uint8_t)dev[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
        }
        if (buf[j] == '\0' && dev[j] == '\0')
            return 1;
    }
    return 0;
}

/* ─── Install INT 0x40 handler in the IVT at address 0x0000:0x0100 ─── */
void syscall_init(void)
{
    extern void _int40_entry(void);
    uint16_t seg = KERNEL_SEGMENT;
    uint16_t off = (uint16_t)&_int40_entry;

    write_far_w(0x0000, 0x0100, off);              /* offset */
    write_far_w(0x0000, 0x0102, seg);              /* segment */

    /* Mount the root FAT16 partition (partition starts at LBA 63) */
    fat16_mount(0x80, 63, &root_fs);
}

/* ─── Syscall dispatch ─── */
uint16_t syscall_handler_c(uint16_t ax, uint16_t bx, uint16_t cx,
                            uint16_t dx, uint16_t si, uint16_t di,
                            uint16_t caller_ds)
{
    uint8_t ah;
    uint8_t al;
    uint16_t ret;

    (void)bx;
    (void)cx;
    (void)dx;

    ah = (uint8_t)(ax >> 8);
    al = (uint8_t)(ax & 0xFF);
    ret = 0;

    switch (ah) {
    /* Console I/O (0x00-0x0F) */
    case SYSCALL_WRITE_STDOUT:
        if (si != 0) {
            /* Read string from caller's DS byte by byte */
            uint16_t off = si;
            uint8_t c;
            do {
                c = read_far_b(caller_ds, off);
                if (c == 0) break;
                out_byte(c);
                off++;
            } while (1);
        }
        break;

    case SYSCALL_WRITE_CHAR:
        out_byte(al);
        break;

    case SYSCALL_WRITE_STDERR:
        /* SI = NUL-terminated string; always to serial+VGA, never redirected. */
        if (si != 0) {
            uint16_t off = si;
            uint8_t c;
            do {
                c = read_far_b(caller_ds, off);
                if (c == 0) break;
                serial_putchar((char)c);
                video_tty_putchar((char)c);
                off++;
            } while (1);
        }
        break;

    case SYSCALL_SET_STDOUT:
        /* BX = file handle to redirect stdout to, or 0xFFFF to clear */
        g_redirect = bx;
        break;

    case SYSCALL_SET_STDIN:
        /* BX = file handle to redirect stdin to, or 0xFFFF to clear */
        g_stdin = bx;
        break;

    case SYSCALL_ISATTY:
        /* Returns 1 if stdin is the keyboard (no redirect), 0 if piped. */
        ret = (g_stdin == 0xFFFF) ? 1 : 0;
        break;

    case SYSCALL_READ_STDIN:
        {
            uint16_t buf_off = si;
            uint16_t maxlen = di;
            uint16_t count = 0;

            /* Stdin redirect: read raw bytes from file (pipe support).
             * Returns actual byte count (0 = EOF); no echo or editing. */
            if (g_stdin < MAX_OPEN_FILES && open_files[g_stdin].used) {
                static uint8_t tmp_in[512];
                uint16_t chunk = (maxlen > sizeof(tmp_in)) ? sizeof(tmp_in) : maxlen;
                uint16_t n = fat16_read(&open_files[g_stdin].file, tmp_in, chunk);
                uint16_t i;
                for (i = 0; i < n; i++)
                    write_far_b(caller_ds, buf_off + i, tmp_in[i]);
                ret = n;
                break;
            }

            /* Normal interactive stdin: keyboard line editor */
            for (;;) {
                char c = (char)(uint8_t)keyboard_getkey();
                if (c == '\r' || c == '\n') {
                    serial_puts("\r\n");
                    video_tty_putchar('\r');
                    video_tty_putchar('\n');
                    break;
                }
                if (c == '\b' || c == 0x7F) {
                    if (count > 0) {
                        count--;
                        serial_puts("\b \b");
                        video_tty_putchar('\b');
                        video_tty_putchar(' ');
                        video_tty_putchar('\b');
                    }
                    continue;
                }
                /* Ctrl-D (0x04) = EOF on interactive stdin */
                if ((uint8_t)c == 0x04)
                    break;
                if (count < maxlen - 1) {
                    write_far_b(caller_ds, buf_off + count, (uint8_t)c);
                    count++;
                    serial_putchar(c);
                    video_tty_putchar(c);
                }
            }
            write_far_b(caller_ds, buf_off + count, '\0');
            ret = count;
        }
        break;

    case SYSCALL_READ_CHAR:
        /* Returns uint16_t: high byte = scan code (0 for plain ASCII),
         * low byte = ASCII char (0 for special/Alt keys). */
        ret = keyboard_getkey();
        break;

    case SYSCALL_GET_SHIFT:
        /* Returns modifier byte: bits 0-1=shift, bit 2=ctrl, bit 3=alt */
        ret = keyboard_shift_state();
        break;

    case SYSCALL_KEY_AVAILABLE:
        /* Nonzero if a keyboard event is buffered (non-blocking; does not
         * touch the serial port, safe for apps that own the UART). */
        ret = keyboard_available();
        break;

    case SYSCALL_CLEAR_SCREEN:
        video_clear();
        break;

    case SYSCALL_SET_CURSOR:
        /* BX high byte = row, low byte = col (matches MEDIT vid_cursor packing) */
        video_set_cursor((uint8_t)(bx >> 8), (uint8_t)bx);
        break;

    case SYSCALL_WRITE_VGA:
        /* SI = string offset in caller's DS; write to VGA screen only (not serial). */
        if (si != 0) {
            uint16_t off = si;
            uint8_t c;
            do {
                c = read_far_b(caller_ds, off);
                if (c == 0) break;
                video_tty_putchar((char)c);
                off++;
            } while (1);
        }
        break;

    case SYSCALL_GET_CURSOR:
        {
            uint16_t pos;
            port_out_b(0x3D4, 0x0F);
            pos = port_in_b(0x3D5);
            port_out_b(0x3D4, 0x0E);
            pos |= ((uint16_t)port_in_b(0x3D5)) << 8;
            /* Return row in high byte, col in low byte */
            ret = (uint16_t)(((pos / 80) << 8) | (pos % 80));
        }
        break;

    /* File I/O (0x10-0x1F) */
    case SYSCALL_OPEN:
        {
            /* DS:BX = path (C string), AL = flags (O_RDONLY etc.).
             * Path may be relative (resolved against CWD) or absolute
             * with subdirectories ("/DOCS/MANUAL.TXT").
             * Device paths ("CON", "/dev/tty", "/dev/com1") create a
             * serial/console device fd (OF_KIND_SERIAL). */
            /* Static: see SYSCALL_READ — same SS!=DS requirement. */
            static uint8_t name_83[12];
            static uint16_t dir_cluster;
            static fat16_fs_t *fs;
            uint16_t i;

            /* Find a free slot in the open file table */
            for (i = 0; i < MAX_OPEN_FILES; i++) {
                if (!open_files[i].used)
                    break;
            }
            if (i >= MAX_OPEN_FILES) {
                ret = 0xFFFF;   /* callers treat >= 16 as failure */
                break;
            }

            /* Check for device path first */
            if (is_device_path(caller_ds, bx)) {
                open_files[i].used = 1;
                open_files[i].kind = OF_KIND_SERIAL;
                ret = i;
                break;
            }

            if (resolve_user_path(caller_ds, bx, &fs, &dir_cluster,
                                  name_83) != 0) {
                ret = 0xFFFF;
                break;
            }

            if (fat16_open_in_dir(fs, dir_cluster, name_83,
                                  &open_files[i].file) != 0) {
                ret = 0xFFFF;
                break;
            }
            open_files[i].used = 1;
            open_files[i].kind = OF_KIND_FILE;
            ret = i;   /* return file handle */
        }
        break;

    case SYSCALL_CLOSE:
        {
            uint16_t handle = bx;

            if (handle >= MAX_OPEN_FILES || !open_files[handle].used) {
                ret = ERR_INVALID_HANDLE;
                break;
            }
            open_files[handle].used = 0;
        }
        break;

    case SYSCALL_READ:
        {
            uint16_t handle = bx;
            uint16_t user_buf = cx;
            uint16_t count = dx;
            /* Static: fat16_read writes via near ptr (DS=KERNEL_SEGMENT) but
             * auto tmp would be read via SS=COM_SEGMENT — same SS!=DS bug.
             * 512 bytes = one sector per fat16_read call (16x fewer
             * read-modify-write cycles than the old 32-byte chunks). */
            static uint8_t tmp[512];
            uint16_t total = 0;
            uint16_t chunk;
            uint16_t n;
            uint16_t i;

            if (handle >= MAX_OPEN_FILES || !open_files[handle].used) {
                ret = ERR_INVALID_HANDLE;
                break;
            }
            /* Device fd: read from serial port (blocking, one char at a time) */
            if (open_files[handle].kind == OF_KIND_SERIAL) {
                for (i = 0; i < count; i++) {
                    uint8_t cb = (uint8_t)serial_getchar();
                    write_far_b(caller_ds, user_buf + i, cb);
                }
                ret = count;
                break;
            }
            while (count > 0) {
                chunk = (count > sizeof(tmp)) ? sizeof(tmp) : count;
                n = fat16_read(&open_files[handle].file, tmp, chunk);
                if (n == 0) break;
                for (i = 0; i < n; i++)
                    write_far_b(caller_ds, user_buf + total + i, tmp[i]);
                total += n;
                count -= n;
            }
            ret = total;
        }
        break;

    case SYSCALL_WRITE:
        {
            uint16_t handle = bx;
            uint16_t user_buf = cx;
            uint16_t count = dx;
            /* Static: see SYSCALL_READ — same SS!=DS requirement.
             * 512 bytes so sector-aligned writes skip the per-32-byte
             * read-modify-write in fat16_write. */
            static uint8_t tmp[512];
            uint16_t total = 0;
            uint16_t chunk;
            uint16_t n;
            uint16_t i;

            if (handle >= MAX_OPEN_FILES || !open_files[handle].used) {
                ret = ERR_INVALID_HANDLE;
                break;
            }
            /* Device fd: write to serial port */
            if (open_files[handle].kind == OF_KIND_SERIAL) {
                for (i = 0; i < count; i++)
                    serial_putchar((char)read_far_b(caller_ds, user_buf + i));
                ret = count;
                break;
            }
            while (count > 0) {
                chunk = (count > sizeof(tmp)) ? sizeof(tmp) : count;
                for (i = 0; i < chunk; i++)
                    tmp[i] = read_far_b(caller_ds, user_buf + total + i);
                n = fat16_write(&open_files[handle].file, tmp, chunk);
                if (n == 0) break;
                total += n;
                count -= n;
            }
            ret = total;
        }
        break;

    case SYSCALL_CREATE:
        {
            /* Static: see SYSCALL_READ — same SS!=DS requirement. */
            static uint8_t name_83[12];
            static uint16_t dir_cluster;
            static fat16_fs_t *fs;
            uint16_t i;

            if (resolve_user_path(caller_ds, bx, &fs, &dir_cluster,
                                  name_83) != 0) {
                ret = 0xFFFF;   /* bad path / missing directory */
                break;
            }

            /* Find a free slot in the open file table */
            for (i = 0; i < MAX_OPEN_FILES; i++) {
                if (!open_files[i].used)
                    break;
            }
            if (i >= MAX_OPEN_FILES) {
                ret = 0xFFFF;   /* too many open files; 0xFFFF = invalid handle */
                break;
            }

            if (fat16_create_in_dir(fs, dir_cluster, name_83,
                                    &open_files[i].file) != 0) {
                ret = 0xFFFF;   /* disk full or file already exists */
                break;
            }
            open_files[i].used = 1;
            ret = i;   /* return file handle */
        }
        break;

    case SYSCALL_DELETE:
        {
            /* Static: see SYSCALL_READ — same SS!=DS requirement. */
            static uint8_t name_83[12];
            static uint16_t dir_cluster;
            static fat16_fs_t *fs;

            if (resolve_user_path(caller_ds, bx, &fs, &dir_cluster,
                                  name_83) != 0) {
                ret = ERR_NOT_FOUND;
                break;
            }

            if (fat16_delete_in_dir(fs, dir_cluster, name_83) != 0)
                ret = ERR_NOT_FOUND;
        }
        break;

    case SYSCALL_SEEK:
        {
            /* AL = whence (SEEK_SET=0, SEEK_CUR=1, SEEK_END=2).
             * BX = handle, CX = pos_low16, DX = pos_high16 (32-bit offset). */
            uint16_t handle = bx;
            uint32_t offset = (uint32_t)dx << 16 | cx;
            uint32_t pos;

            if (handle >= MAX_OPEN_FILES || !open_files[handle].used) {
                ret = ERR_INVALID_HANDLE;
                break;
            }
            /* Device fds: seek unsupported */
            if (open_files[handle].kind == OF_KIND_SERIAL) {
                ret = ERR_INVALID_PARAM;
                break;
            }
            switch (al) {
            case 1:   /* SEEK_CUR */
                pos = open_files[handle].file.position + offset;
                break;
            case 2:   /* SEEK_END */
                pos = (offset <= open_files[handle].file.file_size)
                      ? open_files[handle].file.file_size - offset
                      : 0;
                break;
            default:  /* SEEK_SET (0) */
                pos = offset;
                break;
            }
            if (fat16_seek(&open_files[handle].file, pos) != 0)
                ret = ERR_INVALID_PARAM;
        }
        break;

    case SYSCALL_TELL:
        {
            /* BX = handle, CX = ptr to 4-byte position buffer in caller DS */
            uint16_t handle = bx;
            uint16_t out_off = cx;
            uint32_t position;

            if (handle >= MAX_OPEN_FILES || !open_files[handle].used) {
                ret = ERR_INVALID_HANDLE;
                break;
            }
            if (open_files[handle].kind == OF_KIND_SERIAL) {
                position = 0;
            } else {
                position = open_files[handle].file.position;
            }
            write_far_b(caller_ds, out_off + 0, (uint8_t)(position));
            write_far_b(caller_ds, out_off + 1, (uint8_t)(position >> 8));
            write_far_b(caller_ds, out_off + 2, (uint8_t)(position >> 16));
            write_far_b(caller_ds, out_off + 3, (uint8_t)(position >> 24));
        }
        break;

    case SYSCALL_RENAME:
        {
            /* SI = old path offset, DI = new path offset (both in caller's DS) */
            /* Static: see SYSCALL_READ — same SS!=DS requirement. */
            static uint8_t old_83[12];
            static uint8_t new_83[12];
            static uint16_t old_dir, new_dir;
            static fat16_fs_t *old_fs, *new_fs;

            if (resolve_user_path(caller_ds, si, &old_fs, &old_dir,
                                  old_83) != 0
                || resolve_user_path(caller_ds, di, &new_fs, &new_dir,
                                     new_83) != 0) {
                ret = ERR_NOT_FOUND;
                break;
            }

            /* Rename cannot move between directories or mounts */
            if (old_fs != new_fs || old_dir != new_dir) {
                ret = ERR_ACCESS_DENIED;
                break;
            }

            ret = fat16_rename_in_dir(old_fs, old_dir, old_83, new_83);
        }
        break;

    case SYSCALL_UTIME:
        {
            /* SI = path offset (caller DS), CX = FAT16 date, DX = FAT16 time */
            static uint8_t ut_83[12];
            static uint16_t ut_dir;
            static fat16_fs_t *ut_fs;

            if (resolve_user_path(caller_ds, si, &ut_fs, &ut_dir, ut_83) != 0) {
                ret = ERR_NOT_FOUND;
                break;
            }
            ret = fat16_set_datetime_in_dir(ut_fs, ut_dir, ut_83,
                                            (uint16_t)cx, (uint16_t)dx);
        }
        break;

    case SYSCALL_STAT:
        {
            /* DS:BX = path, CX = pointer to fat16_dirent_t */
            /* Static: see SYSCALL_READ — same SS!=DS requirement. */
            static uint8_t name_83[12];
            static fat16_dirent_t dirent;
            static uint16_t dir_cluster;
            static fat16_fs_t *fs;
            uint16_t out_off = cx;
            uint16_t i;
            uint8_t *dp;

            if (resolve_user_path(caller_ds, bx, &fs, &dir_cluster,
                                  name_83) != 0) {
                ret = ERR_NOT_FOUND;
                break;
            }

            if (fat16_find_in_dir(fs, dir_cluster, name_83, &dirent,
                                  NULL, NULL) != 0) {
                ret = ERR_NOT_FOUND;
                break;
            }

            /* Copy dirent to user space */
            dp = (uint8_t *)&dirent;
            for (i = 0; i < sizeof(fat16_dirent_t); i++)
                write_far_b(caller_ds, out_off + i, dp[i]);
        }
        break;

    /* Directory (0x20-0x2F) */
    case SYSCALL_OPENDIR:
        {
            /* CX = pointer to fat16_dir_t in user space */
            /* Static: see SYSCALL_READ — same SS!=DS requirement. */
            static fat16_dir_t dir;
            static uint16_t dir_cluster;
            uint16_t out_off = cx;
            uint16_t i;
            uint8_t *dp;

            /* Resolve CWD (any depth) to root or a subdirectory cluster */
            if (cwd_dir_cluster(&dir_cluster)) {
                if (fat16_opendir_cluster(&root_fs, dir_cluster, &dir) != 0) {
                    ret = ERR_DISK_ERROR;
                    break;
                }
            } else {
                if (fat16_opendir(&root_fs, &dir) != 0) {
                    ret = ERR_DISK_ERROR;
                    break;
                }
            }

            /* Copy dir to user space */
            dp = (uint8_t *)&dir;
            for (i = 0; i < sizeof(fat16_dir_t); i++)
                write_far_b(caller_ds, out_off + i, dp[i]);
        }
        break;

    case SYSCALL_READDIR:
        {
            /* CX = pointer to fat16_dir_t in user space,
             * DX = pointer to fat16_dirent_t in user space */
            /* Static: see SYSCALL_READ — same SS!=DS requirement. */
            static fat16_dir_t dir;
            static fat16_dirent_t dirent;
            uint16_t dir_off = cx;
            uint16_t dirent_off = dx;
            uint16_t i;
            uint8_t *sp;

            /* Read dir struct from user space */
            sp = (uint8_t *)&dir;
            for (i = 0; i < sizeof(fat16_dir_t); i++)
                ((uint8_t *)&dir)[i] = read_far_b(caller_ds, dir_off + i);

            ret = fat16_readdir(&dir, &dirent);

            /* Write dirent to user space */
            for (i = 0; i < sizeof(fat16_dirent_t); i++)
                write_far_b(caller_ds, dirent_off + i, ((uint8_t *)&dirent)[i]);

            /* Write updated dir back to user space */
            for (i = 0; i < sizeof(fat16_dir_t); i++)
                write_far_b(caller_ds, dir_off + i, ((uint8_t *)&dir)[i]);
        }
        break;

    case SYSCALL_CLOSEDIR:
        {
            /* CX = pointer to fat16_dir_t in user space */
            /* Static: see SYSCALL_READ — same SS!=DS requirement. */
            static fat16_dir_t dir;
            uint16_t dir_off = cx;
            uint16_t i;

            /* Read dir from user space */
            for (i = 0; i < sizeof(fat16_dir_t); i++)
                ((uint8_t *)&dir)[i] = read_far_b(caller_ds, dir_off + i);

            fat16_closedir(&dir);
        }
        break;

    /* Directory create/remove */
    case SYSCALL_MKDIR:
        {
            /* Static: see SYSCALL_READ — same SS!=DS requirement. */
            static uint8_t name_83[12];
            static uint16_t dir_cluster;
            static fat16_fs_t *fs;

            if (resolve_user_path(caller_ds, bx, &fs, &dir_cluster,
                                  name_83) != 0) {
                ret = ERR_NOT_FOUND;
                break;
            }
            ret = fat16_mkdir_in_dir(fs, dir_cluster, name_83);
        }
        break;

    case SYSCALL_RMDIR:
        {
            /* Static: see SYSCALL_READ — same SS!=DS requirement. */
            static uint8_t name_83[12];
            static uint16_t dir_cluster;
            static fat16_fs_t *fs;

            if (resolve_user_path(caller_ds, bx, &fs, &dir_cluster,
                                  name_83) != 0) {
                ret = ERR_NOT_FOUND;
                break;
            }
            ret = fat16_rmdir_in_dir(fs, dir_cluster, name_83);
        }
        break;

    case SYSCALL_CHDIR:
        {
            /* BX = path string offset in caller's DS */
            uint16_t path_off = bx;
            /* Static: see SYSCALL_READ — same SS!=DS requirement. */
            static char path[VFS_MAX_CWD];
            uint16_t i;

            for (i = 0; i < sizeof(path) - 1; i++) {
                uint8_t c = read_far_b(caller_ds, path_off + i);
                path[i] = (char)c;
                if (c == 0) break;
            }
            path[sizeof(path) - 1] = '\0';
            ret = vfs_chdir(path);
        }
        break;

    case SYSCALL_GETCWD:
        {
            /* CX = pointer to buffer, DX = buffer length */
            uint16_t out_off = cx;
            uint16_t buflen = dx;
            /* Static: see SYSCALL_READ — same SS!=DS requirement. */
            static char cwd_buf[VFS_MAX_CWD];
            uint16_t i;

            vfs_getcwd(cwd_buf, VFS_MAX_CWD);
            for (i = 0; i < buflen - 1 && cwd_buf[i]; i++)
                write_far_b(caller_ds, out_off + i, (uint8_t)cwd_buf[i]);
            write_far_b(caller_ds, out_off + i, 0);
        }
        break;

    /* Filesystem (0x30-0x3F) */
    case SYSCALL_MOUNT:
        {
            /* BX = mount point string offset (caller's DS)
             * CX = drive number (e.g. 0x80)
             * DX = partition LBA (low 16 bits)
             * SI = partition LBA (high 16 bits) */
            /* Static: see SYSCALL_READ — same SS!=DS requirement. */
            static char mount_point[32];
            uint8_t drive;
            uint32_t partition_lba;
            uint16_t i, off;

            off = bx;
            for (i = 0; i < sizeof(mount_point) - 1; i++) {
                uint8_t c = read_far_b(caller_ds, off + i);
                mount_point[i] = (char)c;
                if (c == 0) break;
            }
            mount_point[sizeof(mount_point) - 1] = '\0';

            drive = (uint8_t)cx;
            partition_lba = dx | ((uint32_t)si << 16);

            ret = mount_add(mount_point, drive, partition_lba);
        }
        break;

    case SYSCALL_UNMOUNT:
        {
            /* BX = mount point string offset (caller's DS) */
            /* Static: see SYSCALL_READ — same SS!=DS requirement. */
            static char mount_point[32];
            uint16_t i, off;

            off = bx;
            for (i = 0; i < sizeof(mount_point) - 1; i++) {
                uint8_t c = read_far_b(caller_ds, off + i);
                mount_point[i] = (char)c;
                if (c == 0) break;
            }
            mount_point[sizeof(mount_point) - 1] = '\0';

            ret = mount_remove(mount_point);
        }
        break;

    case SYSCALL_LIST_MOUNTS:
        {
            /* Print all mounted filesystems */
            uint16_t i;
            static const char hex[] = "0123456789ABCDEF";

            for (i = 0; i < MAX_MOUNTS; i++) {
                mount_entry_t *m = mount_get_entry(i);
                if (m) {
                    uint8_t drive = m->drive;
                    uint32_t lba = m->partition_lba;
                    char num[16];
                    uint16_t k = 0;
                    uint32_t n;

                    serial_puts("  ");
                    serial_puts(m->mount_point);
                    serial_puts("  drive 0x");
                    serial_putchar(hex[(drive >> 4) & 0xF]);
                    serial_putchar(hex[drive & 0xF]);
                    serial_puts(" LBA ");

                    n = lba;
                    if (n == 0) {
                        num[k++] = '0';
                    } else {
                        while (n > 0) {
                            num[k++] = '0' + (n % 10);
                            n /= 10;
                        }
                    }
                    while (k > 0)
                        serial_putchar(num[--k]);

                    serial_puts("\n");
                }
            }
        }
        break;

    case SYSCALL_GET_VERSION:
        {
            /* BX = pointer to 4-byte version buffer */
            write_far_b(caller_ds, bx, 0);     /* major = 0 */
            write_far_b(caller_ds, bx + 1, 2); /* minor = 2 */
            write_far_b(caller_ds, bx + 2, 0); /* patch = 0 */
            write_far_b(caller_ds, bx + 3, 0); /* reserved */
        }
        break;

    /* Process mgmt (0x40-0x4F) */
    case SYSCALL_EXEC:
        {
            /* BX = program name/path offset (caller's DS).
             * Name with '/' → resolve that exact path.
             * Bare name → search CWD, then root, then BIN/.
             * Both forms also try with .COM auto-appended. */
            static uint8_t name_83[12];
            static uint8_t with_ext[12];
            uint16_t reentry_off;
            static fat16_file_t file;
            static fat16_dirent_t bin_dirent;
            static uint16_t dir_cluster;
            static fat16_fs_t *exec_fs;
            uint16_t load_off;
            uint16_t has_slash;
            uint16_t i;

            has_slash = 0;
            for (i = 0; ; i++) {
                uint8_t c = read_far_b(caller_ds, bx + i);
                if (c == '\0')
                    break;
                if (c == '/') {
                    has_slash = 1;
                    break;
                }
            }

            if (has_slash) {
                /* Explicit path: resolve directory, try exact then .COM */
                if (resolve_user_path(caller_ds, bx, &exec_fs, &dir_cluster,
                                      name_83) != 0) {
                    ret = ERR_NOT_FOUND;
                    break;
                }
            } else {
                name_to_83(caller_ds, bx, name_83);
                exec_fs = &root_fs;
            }

            for (i = 0; i < 8; i++)
                with_ext[i] = name_83[i];
            with_ext[8] = 'C';
            with_ext[9] = 'O';
            with_ext[10] = 'M';
            with_ext[11] = '\0';

            if (has_slash) {
                if (fat16_open_in_dir(exec_fs, dir_cluster, name_83,
                                      &file) == 0)
                    goto load_and_exec;
                if (fat16_open_in_dir(exec_fs, dir_cluster, with_ext,
                                      &file) == 0)
                    goto load_and_exec;
                ret = ERR_NOT_FOUND;
                break;
            }

            /* 1. Current working directory */
            if (cwd_dir_cluster(&dir_cluster)) {
                if (fat16_open_in_dir(&root_fs, dir_cluster, name_83,
                                      &file) == 0)
                    goto load_and_exec;
                if (fat16_open_in_dir(&root_fs, dir_cluster, with_ext,
                                      &file) == 0)
                    goto load_and_exec;
            }

            /* 2. Root directory */
            if (fat16_open(&root_fs, name_83, &file) == 0)
                goto load_and_exec;
            if (fat16_open(&root_fs, with_ext, &file) == 0)
                goto load_and_exec;

            /* 3. BIN/ subdirectory */
            {
                static const uint8_t bin_name[] = "BIN        ";
                if (fat16_find(&root_fs, bin_name, &bin_dirent, NULL, NULL) == 0
                    && (bin_dirent.attrs & FAT16_ATTR_DIRECTORY)) {
                    if (fat16_open_in_dir(&root_fs, bin_dirent.first_cluster,
                                          name_83, &file) == 0)
                        goto load_and_exec;
                    if (fat16_open_in_dir(&root_fs, bin_dirent.first_cluster,
                                          with_ext, &file) == 0)
                        goto load_and_exec;
                }
            }

            ret = ERR_NOT_FOUND;
            break;

        load_and_exec:
            /* Allocate space for the child in the process address space.
             * g_next_seg tracks the next free paragraph (initialized by exec.c
             * after loading SHELL.COM, then bumped per child allocation here).
             * Each allocation = PSP (16 paras) + code + 2KB stack (128 paras).
             * On child exit, return_to_parent_ restores g_next_seg = child_seg. */
            {
            uint16_t child_seg = get_next_seg();
            uint16_t child_sp;
            uint16_t alloc_paras;

            /* Refuse to exec past physical RAM: loading the child above
             * the RAM top freezes on the first stack use.
             * BDA word 0040:0013 = base memory size in KB. */
            {
                uint16_t ram_paras = (uint16_t)(read_far_w(0x0040, 0x0013) << 6);
                if (ram_paras >= PROC_PARAS
                    && child_seg > (uint16_t)(ram_paras - PROC_PARAS)) {
                    ret = ERR_NO_MEMORY;
                    break;
                }
            }

            /* Zero the whole child slot first so the program's BSS
             * (uninitialized globals, which live above the raw .COM image
             * and are NOT part of the file) starts cleared. Otherwise a
             * child inherits stale RAM from a previous child — e.g. EDIT's
             * menu_wrap_flag comes up nonzero and its word-wrap layout
             * loops. Word-fill for speed: PROC_PARAS<<4 bytes = <<3 words. */
            for (i = 0; i < (uint16_t)(PROC_PARAS << 3); i++)
                write_far_w(child_seg, (uint16_t)(i << 1), 0);

            /* Load file to child_seg:0x0100 sector by sector.
             * tmp must be static (SS!=DS: fat16_read uses DS-relative near ptr). */
            {
            static uint8_t tmp[512];
            load_off = 0x0100;
            while (1) {
                uint16_t bytes = fat16_read(&file, tmp, 512);
                if (bytes == 0) break;
                for (i = 0; i < bytes; i++)
                    write_far_b(child_seg, load_off + i, tmp[i]);
                load_off += bytes;
                if (bytes < 512) break;
            }
            }

            /* Allocate PROC_PARAS (16 KB) for the child: code + BSS above
             * the raw binary + stack at the top. All current apps fit
             * (largest: EDIT.COM ~10 KB image + ~2.5 KB BSS). Kept small
             * so a 256 KB machine can run shell + child + ALLOC blocks. */
            alloc_paras = PROC_PARAS;
            set_next_seg(child_seg + alloc_paras);

            /* Initial SP: top of the child's allocation */
            child_sp = PROC_SP;

            /* Write argument string to child_seg:0x0082 (PSP argv area).
             * cx holds the caller's argument offset (0 if none).
             * PSP tail spans 0x82..0xFF (125 usable bytes); the child's
             * code starts at 0x100.  Cap at k < 125 so the NUL terminator
             * at 0x0082+k never reaches 0x0100 and overwrites the entry point. */
            {
                uint16_t arg_src = cx;
                uint16_t k = 0;
                uint8_t c2 = 0;
                if (arg_src != 0) {
                    do {
                        c2 = read_far_b(caller_ds, arg_src + k);
                        write_far_b(child_seg, 0x0082 + k, c2);
                        k++;
                        if (k >= 125) break;   /* hard stop: 0x82+125 = 0xFF */
                    } while (c2 != 0);
                }
                write_far_b(child_seg, 0x0082 + k, 0);
            }

            /* Push parent context onto pctx stack, then jump to child.
             * child_return_ is the retf trampoline; on child exit,
             * return_to_parent_ irets back to the shell's EXEC call site. */
            reentry_off = (uint16_t)(void (__near *)(void))&child_return;
            exec_save_parent_ctx(child_seg);
            exec_far_jump_sp(child_seg, 0x0100, KERNEL_SEGMENT, reentry_off, child_sp);
            /* Unreachable */
            }
            break;
        }
        break;

    case SYSCALL_EXIT:
        /* Return to parent (shell) via pctx, or restart shell if no parent. */
        return_to_parent();
        /* Unreachable */
        break;

    case SYSCALL_GET_PID:
        /* Return a process id proxy: (next_seg - PROC_PARAS) is the current
         * process's load segment. Not a true PID but unique per exec level. */
        {
            uint16_t ns = get_next_seg();
            ret = (ns > PROC_PARAS) ? (uint16_t)(ns - PROC_PARAS) : ns;
        }
        break;

    /* Memory (0x50-0x51) */
    case SYSCALL_ALLOC:
        {
            /* BX = paragraphs to allocate; returns base segment,
             * 0 if the block would extend past physical RAM. */
            uint16_t seg = get_next_seg();
            uint16_t ram_paras = (uint16_t)(read_far_w(0x0040, 0x0013) << 6);

            if (ram_paras != 0
                && (bx > ram_paras || seg > (uint16_t)(ram_paras - bx))) {
                ret = 0;
                break;
            }
            set_next_seg(seg + bx);
            ret = seg;
        }
        break;

    case SYSCALL_FREE:
        /* No-op: memory is reclaimed when child exits via return_to_parent_
         * which resets g_next_seg to child_seg (the value saved in pctx). */
        ret = 0;
        break;

    case SYSCALL_MEM_INFO:
        {
            /* CX = ptr to 4-byte buffer in caller DS.
             * [0-1] = total RAM in paragraphs (from BDA 0040:0013 in KB << 6).
             * [2-3] = used paragraphs (= get_next_seg()). */
            uint16_t out_off = cx;
            uint16_t total_paras = (uint16_t)(read_far_w(0x0040, 0x0013) << 6);
            uint16_t used_paras  = get_next_seg();
            write_far_b(caller_ds, out_off + 0, (uint8_t)(total_paras));
            write_far_b(caller_ds, out_off + 1, (uint8_t)(total_paras >> 8));
            write_far_b(caller_ds, out_off + 2, (uint8_t)(used_paras));
            write_far_b(caller_ds, out_off + 3, (uint8_t)(used_paras >> 8));
        }
        break;

    case SYSCALL_STATFS:
        {
            /* CX = ptr to 8-byte statfs_t in caller DS.
             * Layout: [0-1]=total_clusters, [2-3]=free_clusters,
             *         [4]=sectors_per_cluster, [5]=0 (pad),
             *         [6-7]=bytes_per_sector. */
            uint16_t out_off = cx;
            uint16_t total_cl = root_fs.total_clusters;
            uint16_t free_cl;

            /* Count free clusters sector-by-sector (32 reads, not ~8000). */
            free_cl = fat16_count_free_clusters(&root_fs);

            write_far_b(caller_ds, out_off + 0, (uint8_t)(total_cl));
            write_far_b(caller_ds, out_off + 1, (uint8_t)(total_cl >> 8));
            write_far_b(caller_ds, out_off + 2, (uint8_t)(free_cl));
            write_far_b(caller_ds, out_off + 3, (uint8_t)(free_cl >> 8));
            write_far_b(caller_ds, out_off + 4, root_fs.sectors_per_cluster);
            write_far_b(caller_ds, out_off + 5, 0);
            write_far_b(caller_ds, out_off + 6, (uint8_t)(root_fs.bytes_per_sector));
            write_far_b(caller_ds, out_off + 7, (uint8_t)(root_fs.bytes_per_sector >> 8));
        }
        break;

    default:
        ret = ERR_NOT_IMPLEMENTED;
        break;
    }

    return ret;
}