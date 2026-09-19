/* fs.c -- File system operations for DOS NCD
 * Uses DOS INT 21h for file operations.
 */

#include "dos_ncd.h"
#include <dos.h>
#include <string.h>
#include <stdio.h>

/* DOS findfirst/findnext structures */
typedef struct {
    u8  reserved[21];
    u8  attr;
    u16 time;
    u16 date;
    u32 size;
    char name[13];
} dos_find_t;

static dos_find_t find_buf;

/* Read directory into panel */
int fs_read_dir(const char *path, panel_t *panel)
{
    union REGS regs;
    struct SREGS sregs;
    char search_path[DOS_PATH_MAX];
    int count = 0;

    panel->count = 0;

    /* Set DTA to our find buffer */
    regs.h.ah = 0x1A;
    regs.x.dx = (u16)&find_buf;
    segread(&sregs);
    sregs.ds = sregs.ds;
    intdosx(&regs, &regs, &sregs);

    /* Build search pattern: e.g. "C:\*.*" or "\*.*" */
    snprintf(search_path, sizeof(search_path), "%s*.*", path);

    /* Find first file (AH=4Eh) */
    regs.h.ah = 0x4E;
    regs.x.cx = 0x1F;  /* All attributes except volume label */
    regs.x.dx = (u16)search_path;
    segread(&sregs);
    sregs.ds = sregs.ds;
    intdosx(&regs, &regs, &sregs);

    if (regs.x.cflag) {
        /* No files found or error */
        return 0;
    }

    /* Process first file (result is now in find_buf via DTA) */
    for (;;) {
        panel_entry_t *e;

        /* Skip "." and ".." */
        if (strcmp(find_buf.name, ".") != 0 && strcmp(find_buf.name, "..") != 0) {
            e = &panel->entries[count];
            strncpy(e->name, find_buf.name, COL_NAME_WIDTH - 1);
            e->name[COL_NAME_WIDTH - 1] = '\0';
            e->attr = find_buf.attr;
            e->is_dir = (find_buf.attr & DIR_ATTR_DIRECTORY) ? 1 : 0;
            e->size = (u16)find_buf.size;
            e->date = find_buf.date;
            e->time = find_buf.time;
            e->selected = 0;
            count++;
            if (count >= MAX_ENTRIES) break;
        }

        /* Find next (AH=4Fh) */
        regs.h.ah = 0x4F;
        intdosx(&regs, &regs, &sregs);
        if (regs.x.cflag) break;  /* No more files */
    }

    panel->count = count;
    panel_sort(panel);

    /* Copy path to panel */
    strncpy(panel->cwd, path, DOS_PATH_MAX - 1);
    panel->cwd[DOS_PATH_MAX - 1] = '\0';

    return count;
}

int fs_file_exists(const char *path)
{
    union REGS regs;
    struct SREGS sregs;

    regs.h.ah = 0x43;  /* Get file attributes */
    regs.h.al = 0x00;
    regs.x.dx = (u16)path;
    segread(&sregs);
    sregs.ds = sregs.ds;
    intdosx(&regs, &regs, &sregs);

    return !regs.x.cflag;
}

int fs_is_dir(const char *path)
{
    union REGS regs;
    struct SREGS sregs;

    regs.h.ah = 0x43;
    regs.h.al = 0x00;
    regs.x.dx = (u16)path;
    segread(&sregs);
    sregs.ds = sregs.ds;
    intdosx(&regs, &regs, &sregs);

    if (regs.x.cflag) return 0;

    return (regs.x.cx & DIR_ATTR_DIRECTORY) != 0;
}

u32 fs_file_size(const char *path)
{
    union REGS regs;
    struct SREGS sregs;
    u16 handle;
    u32 size;

    regs.h.ah = 0x3D;  /* Open file */
    regs.h.al = 0x00;  /* Read only */
    regs.x.dx = (u16)path;
    segread(&sregs);
    sregs.ds = sregs.ds;
    intdosx(&regs, &regs, &sregs);

    if (regs.x.cflag) return 0;

    handle = regs.x.ax;

    /* Seek to end (AH=42h, AL=02h) */
    regs.h.ah = 0x42;
    regs.h.al = 0x02;
    regs.x.bx = handle;
    regs.x.cx = 0;
    regs.x.dx = 0;
    intdosx(&regs, &regs, &sregs);

    size = ((u32)regs.x.dx << 16) | regs.x.ax;

    /* Close file (AH=3Eh) */
    regs.h.ah = 0x3E;
    regs.x.bx = handle;
    intdosx(&regs, &regs, &sregs);

    return size;
}

/* Copy file using DOS read/write */
int fs_copy_file(const char *src, const char *dst)
{
    union REGS regs;
    struct SREGS sregs;
    char buf[4096];
    u16 src_handle, dst_handle;
    u16 bytes_read;

    /* Open source */
    regs.h.ah = 0x3D;
    regs.h.al = 0x00;
    regs.x.dx = (u16)src;
    segread(&sregs);
    sregs.ds = sregs.ds;
    intdosx(&regs, &regs, &sregs);
    if (regs.x.cflag) return -1;
    src_handle = regs.x.ax;

    /* Create destination */
    regs.h.ah = 0x3C;
    regs.x.cx = 0x20;  /* Archive attribute */
    regs.x.dx = (u16)dst;
    intdosx(&regs, &regs, &sregs);
    if (regs.x.cflag) {
        regs.h.ah = 0x3E;
        regs.x.bx = src_handle;
        intdosx(&regs, &regs, &sregs);
        return -1;
    }
    dst_handle = regs.x.ax;

    /* Copy data */
    while (1) {
        /* Read from source */
        regs.h.ah = 0x3F;
        regs.x.bx = src_handle;
        regs.x.cx = sizeof(buf);
        regs.x.dx = (u16)buf;
        intdosx(&regs, &regs, &sregs);
        if (regs.x.cflag || regs.x.ax == 0) break;
        bytes_read = regs.x.ax;

        /* Write to destination */
        regs.h.ah = 0x40;
        regs.x.bx = dst_handle;
        regs.x.cx = bytes_read;
        regs.x.dx = (u16)buf;
        intdosx(&regs, &regs, &sregs);
        if (regs.x.cflag || regs.x.ax != bytes_read) break;
    }

    /* Close files */
    regs.h.ah = 0x3E;
    regs.x.bx = src_handle;
    intdosx(&regs, &regs, &sregs);
    regs.x.bx = dst_handle;
    intdosx(&regs, &regs, &sregs);

    return 0;
}

int fs_move_file(const char *src, const char *dst)
{
    union REGS regs;
    struct SREGS sregs;

    /* Try rename first (AH=56h) */
    regs.h.ah = 0x56;
    regs.x.dx = (u16)src;
    regs.x.di = (u16)dst;
    segread(&sregs);
    sregs.ds = sregs.ds;
    sregs.es = sregs.es;
    intdosx(&regs, &regs, &sregs);

    if (!regs.x.cflag) return 0;

    /* Fallback: copy + delete */
    if (fs_copy_file(src, dst) == 0) {
        return fs_delete_file(src);
    }
    return -1;
}

int fs_delete_file(const char *path)
{
    union REGS regs;
    struct SREGS sregs;

    regs.h.ah = 0x41;  /* Delete file */
    regs.x.dx = (u16)path;
    segread(&sregs);
    sregs.ds = sregs.ds;
    intdosx(&regs, &regs, &sregs);

    return regs.x.cflag ? -1 : 0;
}

int fs_make_dir(const char *path)
{
    union REGS regs;
    struct SREGS sregs;

    regs.h.ah = 0x39;  /* Create directory */
    regs.x.dx = (u16)path;
    segread(&sregs);
    sregs.ds = sregs.ds;
    intdosx(&regs, &regs, &sregs);

    return regs.x.cflag ? -1 : 0;
}

int fs_remove_dir(const char *path)
{
    union REGS regs;
    struct SREGS sregs;

    regs.h.ah = 0x3A;  /* Remove directory */
    regs.x.dx = (u16)path;
    segread(&sregs);
    sregs.ds = sregs.ds;
    intdosx(&regs, &regs, &sregs);

    return regs.x.cflag ? -1 : 0;
}

int fs_rename(const char *src, const char *dst)
{
    union REGS regs;
    struct SREGS sregs;

    regs.h.ah = 0x56;  /* Rename */
    regs.x.dx = (u16)src;
    regs.x.di = (u16)dst;
    segread(&sregs);
    sregs.ds = sregs.ds;
    sregs.es = sregs.es;
    intdosx(&regs, &regs, &sregs);

    return regs.x.cflag ? -1 : 0;
}
