/* viewer.c -- Fullscreen file viewer for NCD.
 * Opens a file, reads into far buffer, displays with scroll support.
 * F3 or Esc to close. */
/* Scrolling: Up/Down 1 line, PgUp/PgDn 22 lines, Home/End to top/bottom. */

#include "ncd.h"
#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "fs.h"
#include "viewer.h"

/* Number of visible lines in the viewer content area */
#define VIEWER_VISIBLE 22

static u16 viewer_seg;
static u16 viewer_buf_len;
static u16 viewer_scroll;
static char viewer_filename[NAME_MAX];

/* 512-byte bounce buffer for ncd_read; same size as io_buf in fs.c */
static u8 bounce[512];

/* Count total lines in the far buffer */
static u16 count_lines(void)
{
    u16 lines = 0;
    u16 i = 0;
    u8 __far *vbuf = (u8 __far *)MK_FP(viewer_seg, 0);

    while (i < viewer_buf_len) {
        lines++;
        while (i < viewer_buf_len && vbuf[i] != '\n' && vbuf[i] != '\r')
            i++;
        while (i < viewer_buf_len && (vbuf[i] == '\n' || vbuf[i] == '\r'))
            i++;
    }
    return lines;
}

static void viewer_render(void)
{
    u16 i = 0;
    u16 line;
    u16 line_start;
    u16 line_len;
    u16 j;
    u8 ch;
    u8 __far *vbuf;
    u16 cur_line = 0;
    u16 total_lines;
    char tmp[10];

    vbuf = (u8 __far *)MK_FP(viewer_seg, 0);
    total_lines = count_lines();

    /* Title bar */
    vid_fill(0, 0, COLS, ' ', A_VIEWER_HI);
    vid_puts(0, 1, viewer_filename, A_VIEWER_HI);

    /* Clear text area */
    for (line = 1; line < (u16)(ROWS - 1); line++)
        vid_fill(line, 0, COLS, ' ', A_VIEWER);

    /* Skip to scroll position */
    while (i < viewer_buf_len && cur_line < viewer_scroll) {
        while (i < viewer_buf_len && vbuf[i] != '\n' && vbuf[i] != '\r')
            i++;
        while (i < viewer_buf_len && (vbuf[i] == '\n' || vbuf[i] == '\r'))
            i++;
        cur_line++;
    }

    /* Render file content line by line */
    line = 1;
    while (i < viewer_buf_len && line < (u16)(VIEWER_VISIBLE + 1)) {
        line_start = i;
        /* Find end of line */
        while (i < viewer_buf_len && vbuf[i] != '\n' && vbuf[i] != '\r')
            i++;
        /* Write this line */
        line_len = i - line_start;
        if (line_len > COLS - 2) line_len = COLS - 2;
        vid_fill(line, 1, line_len, ' ', A_VIEWER);
        for (j = 0; j < line_len; j++) {
            ch = vbuf[line_start + j];
            if (ch < 32) ch = '.';  /* replace control chars */
            vid_putat(line, 1 + j, ch, A_VIEWER);
        }
        line++;
        /* Skip newline chars */
        while (i < viewer_buf_len && (vbuf[i] == '\n' || vbuf[i] == '\r'))
            i++;
    }

    /* Status line */
    vid_fill(ROWS - 1, 0, COLS, ' ', A_STATUS);
    vid_puts(ROWS - 1, 1, viewer_filename, A_STATUS);
    if (total_lines > 0) {
        u32toa((u32)(viewer_scroll + 1), tmp);
        vid_puts(ROWS - 1, 20, "Ln ", A_STATUS);
        vid_puts(ROWS - 1, 23, tmp, A_STATUS);
        vid_puts(ROWS - 1, 23 + (u16)strlen(tmp), "/", A_STATUS);
        u32toa((u32)total_lines, tmp);
        vid_puts(ROWS - 1, 24 + (u16)strlen(tmp), tmp, A_STATUS);
    }
    vid_puts(ROWS - 1, 60, "F3/Esc:Close", A_STATUS);

    vid_cursor_hide();
    vid_flush();
}

void viewer_open(const char *path)
{
    u16 fh;
    u16 total = 0;
    u16 nr;
    u16 chunk;
    u16 total_lines;

    /* Copy filename */
    strcpy(viewer_filename, path);

    /* Allocate far segment if not already done */
    if (!viewer_seg) {
        /* 4096 bytes = 256 paragraphs */
        viewer_seg = (u16)syscall_int40(SYSCALL_ALLOC, 0, 256, 0, 0, 0, 0);
        if (!viewer_seg) return;
    }

    /* Open file */
    fh = ncd_open(path);
    if (fh >= 16) {
        /* Could not open - show error */
        vid_fill(ROWS - 1, 0, COLS, ' ', A_STATUS);
        vid_puts(ROWS - 1, 1, "Cannot open file", A_STATUS);
        vid_flush();
        return;
    }

    /* Read file into far buffer via bounce buffer */
    while (total < VIEWER_BUF_SIZE - 1) {
        chunk = VIEWER_BUF_SIZE - 1 - total;
        if (chunk > sizeof(bounce)) chunk = sizeof(bounce);
        nr = ncd_read(fh, bounce, chunk);
        if (nr == 0) break;
        far_copy((u8 __far *)MK_FP(viewer_seg, total),
                 (const u8 __far *)bounce, nr);
        total += nr;
    }
    /* Null-terminate in far segment */
    {
        u8 __far *vbuf = (u8 __far *)MK_FP(viewer_seg, total);
        *vbuf = '\0';
    }
    viewer_buf_len = total;
    viewer_scroll = 0;

    ncd_close(fh);

    total_lines = count_lines();

    /* Render and wait for close */
    viewer_render();

    for (;;) {
        unsigned k = kbd_get();

        if (k == K_F3 || k == K_ESC)
            break;

        switch (k) {
        case K_UP:
            if (viewer_scroll > 0)
                viewer_scroll--;
            viewer_render();
            break;
        case K_DOWN:
            if (viewer_scroll + VIEWER_VISIBLE < total_lines)
                viewer_scroll++;
            viewer_render();
            break;
        case K_PGUP:
            if (viewer_scroll >= VIEWER_VISIBLE)
                viewer_scroll -= VIEWER_VISIBLE;
            else
                viewer_scroll = 0;
            viewer_render();
            break;
        case K_PGDN:
            if (viewer_scroll + VIEWER_VISIBLE + VIEWER_VISIBLE < total_lines)
                viewer_scroll += VIEWER_VISIBLE;
            else if (total_lines > VIEWER_VISIBLE)
                viewer_scroll = total_lines - VIEWER_VISIBLE;
            viewer_render();
            break;
        case K_HOME:
            viewer_scroll = 0;
            viewer_render();
            break;
        case K_END:
            if (total_lines > VIEWER_VISIBLE)
                viewer_scroll = total_lines - VIEWER_VISIBLE;
            viewer_render();
            break;
        default:
            break;
        }
    }
}