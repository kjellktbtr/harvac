/* viewer.c -- Simple file viewer for DOS NCD */

#include "dos_ncd.h"
#include <stdio.h>
#include <string.h>

static FILE *viewer_fp = NULL;
static char viewer_buf[VIEWER_BUF_SIZE];
static u16 viewer_pos = 0;   /* File position in bytes */
static u16 viewer_line = 0;  /* Current line in view */
static char viewer_path[DOS_PATH_MAX];
u8 viewer_mode = 0;

void viewer_open(const char *path)
{
    size_t read;

    if (viewer_mode) viewer_close();

    viewer_fp = fopen(path, "rb");
    if (!viewer_fp) return;

    strncpy(viewer_path, path, DOS_PATH_MAX - 1);
    viewer_path[DOS_PATH_MAX - 1] = '\0';
    viewer_pos = 0;
    viewer_line = 0;
    viewer_mode = 1;

    /* Read first chunk */
    read = fread(viewer_buf, 1, VIEWER_BUF_SIZE, viewer_fp);
    viewer_buf[read] = '\0';
}

void viewer_close(void)
{
    if (viewer_fp) {
        fclose(viewer_fp);
        viewer_fp = NULL;
    }
    viewer_mode = 0;
}

void viewer_render(void)
{
    u8 r;
    u8 row;
    char *p;
    char *line_start;
    u16 line_count;
    u16 len;
    char save;
    char status[80];

    if (!viewer_mode) return;

    /* Clear viewer area */
    for (r = ROW_FILE_FIRST; r <= ROW_FILE_LAST; r++) {
        vid_fill(r, 1, PANE_WIDTH, ' ', A_VIEWER);
    }

    /* Draw viewer header */
    vid_fill(ROW_PATH, 1, PANE_WIDTH, ' ', A_PATH);
    vid_puts(ROW_PATH, 1, viewer_path, A_PATH);

    /* Draw content lines */
    row = ROW_FILE_FIRST;
    p = viewer_buf;
    line_start = p;
    line_count = 0;

    while (*p && row <= ROW_FILE_LAST) {
        if (*p == '\n' || *p == '\r') {
            /* End of line */
            len = p - line_start;
            if (len > PANE_WIDTH) len = PANE_WIDTH;

            save = line_start[len];
            line_start[len] = '\0';
            vid_puts(row, 1, line_start, A_VIEWER);
            line_start[len] = save;

            /* Skip \r\n */
            if (*p == '\r' && *(p + 1) == '\n') p += 2;
            else p++;

            line_start = p;
            row++;
            line_count++;
        } else {
            p++;
        }
    }

    /* Status line */
    snprintf(status, sizeof(status), " Viewer: %s | Line %u | F3/F10 to exit ", viewer_path, viewer_line);
    vid_fill(ROW_BORDER, 1, PANE_WIDTH, ' ', A_VIEWER_HI);
    vid_puts(ROW_BORDER, 1, status, A_VIEWER_HI);
}

int viewer_handle_key(u16 key)
{
    if (!viewer_mode) return 0;

    switch (key) {
        case K_ESC:
        case K_F3:
        case K_F10:
            viewer_close();
            return 1;

        case K_UP:
            if (viewer_line > 0) {
                viewer_line--;
                /* TODO: Scroll up */
            }
            return 1;

        case K_DOWN:
            viewer_line++;
            /* TODO: Scroll down */
            return 1;

        case K_PGUP:
            viewer_line = (viewer_line > 20) ? viewer_line - 20 : 0;
            return 1;

        case K_PGDN:
            viewer_line += 20;
            return 1;

        case K_HOME:
            viewer_line = 0;
            return 1;

        case K_END:
            viewer_line = 0xFFFF;
            return 1;
    }

    return 0;
}
