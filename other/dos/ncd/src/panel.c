/* panel.c -- Panel management for DOS NCD
 * Handles directory browsing, sorting, and rendering.
 */

#include "dos_ncd.h"
#include <string.h>
#include <stdlib.h>

/* Global panels */
panel_t panel_left;
panel_t panel_right;
u8 active_panel = PANEL_LEFT;

/* Panel entry comparison for sorting */
static int panel_cmp(const panel_entry_t *a, const panel_entry_t *b)
{
    /* Directories first */
    if (a->is_dir && !b->is_dir) return -1;
    if (!a->is_dir && b->is_dir) return 1;
    /* Then by name */
    return strcmp(a->name, b->name);
}

void panel_init(panel_t *p)
{
    p->count = 0;
    p->sel = 0;
    p->scroll = 0;
    p->cwd[0] = '\0';
}

void panel_done(panel_t *p)
{
    (void)p;
}

void panel_refresh(panel_t *p)
{
    fs_read_dir(p->cwd, p);
    if (p->sel >= p->count && p->count > 0) p->sel = p->count - 1;
}

void panel_sort(panel_t *p)
{
    /* Simple bubble sort - fine for small arrays */
    u16 i;
    u16 j;
    panel_entry_t tmp;

    for (i = 0; i < p->count; i++) {
        for (j = 0; j < p->count - 1 - i; j++) {
            if (panel_cmp(&p->entries[j], &p->entries[j + 1]) > 0) {
                tmp = p->entries[j];
                p->entries[j] = p->entries[j + 1];
                p->entries[j + 1] = tmp;
            }
        }
    }
}

const char *panel_get_name(panel_t *p)
{
    if (p->sel < p->count) {
        return p->entries[p->sel].name;
    }
    return "";
}

const char *panel_get_cwd(panel_t *p)
{
    return p->cwd;
}

u16 panel_get_visible(panel_t *p)
{
    (void)p;
    return ROW_FILE_LAST - ROW_FILE_FIRST + 1;
}

u16 panel_count_selected(panel_t *p)
{
    u16 count = 0;
    u16 i;

    for (i = 0; i < p->count; i++) {
        if (p->entries[i].selected) count++;
    }
    return count;
}

void panel_up(panel_t *p)
{
    if (p->sel > 0) {
        p->sel--;
        if (p->sel < p->scroll) p->scroll = p->sel;
    }
}

void panel_down(panel_t *p)
{
    u16 vis;

    if (p->sel + 1 < p->count) {
        p->sel++;
        vis = panel_get_visible(p);
        if (p->sel >= p->scroll + vis) p->scroll = p->sel - vis + 1;
    }
}

void panel_page_up(panel_t *p)
{
    u16 vis;

    vis = panel_get_visible(p);
    if (p->sel >= vis) p->sel -= vis;
    else p->sel = 0;

    if (p->sel < p->scroll) p->scroll = p->sel;
}

void panel_page_down(panel_t *p)
{
    u16 vis;

    vis = panel_get_visible(p);
    if (p->sel + vis < p->count) p->sel += vis;
    else p->sel = p->count - 1;

    if (p->sel >= p->scroll + vis) p->scroll = p->sel - vis + 1;
}

void panel_home(panel_t *p)
{
    p->sel = 0;
    p->scroll = 0;
}

void panel_end(panel_t *p)
{
    u16 vis;

    if (p->count > 0) {
        p->sel = p->count - 1;
        vis = panel_get_visible(p);
        if (p->count > vis) p->scroll = p->count - vis;
    }
}

void panel_toggle_select(panel_t *p)
{
    if (p->sel < p->count) {
        p->entries[p->sel].selected ^= 1;
        panel_down(p);
    }
}

int panel_enter_dir(panel_t *p)
{
    const char *name;
    char new_path[DOS_PATH_MAX];

    if (p->sel >= p->count) return 0;
    if (!p->entries[p->sel].is_dir) return 0;

    name = p->entries[p->sel].name;
    if (strcmp(name, "..") == 0) {
        panel_parent_dir(p);
        return 1;
    }

    /* Build new path */
    snprintf(new_path, sizeof(new_path), "%s%s\\", p->cwd, name);

    strncpy(p->cwd, new_path, DOS_PATH_MAX - 1);
    p->cwd[DOS_PATH_MAX - 1] = '\0';

    panel_refresh(p);
    return 1;
}

void panel_parent_dir(panel_t *p)
{
    char *last_slash = strrchr(p->cwd, '\\');
    if (last_slash && last_slash > p->cwd) {
        *last_slash = '\0';
    } else {
        p->cwd[0] = '\0';
    }
    panel_refresh(p);
}

void panel_render(const panel_t *p, int pane)
{
    u8 left_col;
    u8 inner_left;
    u8 inner_right;
    u8 width;
    u8 attr_normal;
    u8 attr_sel;
    u8 vis;
    u8 i;
    u16 idx;
    u8 row;
    u8 attr;
    const panel_entry_t *e;

    left_col = (pane == PANEL_LEFT) ? LEFT_PANE_LEFT : RIGHT_PANE_LEFT;
    inner_left = (pane == PANEL_LEFT) ? 1 : 41;
    inner_right = (pane == PANEL_LEFT) ? 38 : 78;
    width = inner_right - inner_left + 1;
    attr_normal = (pane == (int)active_panel) ? A_NORMAL : A_NORMAL;
    attr_sel = (pane == (int)active_panel) ? A_SELECTED : A_INVERSE;

    vis = panel_get_visible(p);

    /* Draw path header */
    vid_fill(ROW_PATH, left_col, 40, ' ', A_PATH);
    vid_puts(ROW_PATH, left_col + 1, p->cwd[0] ? p->cwd : "\\", A_PATH);

    /* Draw column headers */
    vid_fill(ROW_HEADER, left_col, 40, ' ', A_HEADER);
    vid_puts(ROW_HEADER, inner_left, "Name", A_HEADER);
    vid_puts(ROW_HEADER, inner_left + 14, "Size", A_HEADER);
    vid_puts(ROW_HEADER, inner_left + 20, "Date", A_HEADER);
    vid_puts(ROW_HEADER, inner_left + 31, "Time", A_HEADER);

    /* Draw file entries */
    for (i = 0; i < vis; i++) {
        idx = p->scroll + i;
        row = ROW_FILE_FIRST + i;

        if (idx >= p->count) {
            vid_fill(row, inner_left, width, ' ', attr_normal);
            continue;
        }

        e = &p->entries[idx];

        if (idx == p->sel && pane == (int)active_panel) {
            attr = attr_sel;
        } else if (e->selected) {
            attr = A_MARKER;
        } else {
            attr = e->is_dir ? A_DIR : A_FILE;
        }

        vid_fill(row, inner_left, width, ' ', attr);

        /* Name */
        vid_puts(row, inner_left, e->name, attr);

        /* Size */
        if (!e->is_dir) {
            char size_str[16];
            if (e->size >= 1024) {
                snprintf(size_str, sizeof(size_str), "%uK", e->size / 1024);
            } else {
                snprintf(size_str, sizeof(size_str), "%u", e->size);
            }
            vid_puts(row, inner_left + 14, size_str, attr);
        }

        /* Date */
        if (e->date) {
            char date_str[16];
            u16 year = 1980 + ((e->date >> 9) & 0x7F);
            u16 month = (e->date >> 5) & 0x0F;
            u16 day = e->date & 0x1F;
            snprintf(date_str, sizeof(date_str), "%04u-%02u-%02u", year, month, day);
            vid_puts(row, inner_left + 20, date_str, attr);
        }

        /* Time */
        if (e->time) {
            char time_str[16];
            u16 hour = (e->time >> 11) & 0x1F;
            u16 min = (e->time >> 5) & 0x3F;
            snprintf(time_str, sizeof(time_str), "%02u:%02u", hour, min);
            vid_puts(row, inner_left + 31, time_str, attr);
        }
    }

    /* Draw bottom border */
    vid_fill(ROW_BORDER, left_col, 40, BOX_H, A_SEPARATOR);
    vid_putat(ROW_BORDER, left_col, A_SEPARATOR, BOX_TLEFT);
    vid_putat(ROW_BORDER, left_col + 39, A_SEPARATOR, BOX_TRIGHT);
}