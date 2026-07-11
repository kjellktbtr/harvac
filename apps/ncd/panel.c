/* panel.c -- File pane data model and rendering for NCD.
 * Reads directories via syscalls, sorts entries, renders 3-column layout.
 * Entries live in a far segment (SYSCALL_ALLOC) to fit within 16 KB slot. */

#include "ncd.h"
#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "far.h"
#include "vid.h"
#include "fs.h"
#include "panel.h"

panel_t panel_left;
panel_t panel_right;
u8      active_panel;

/* --- Far entry accessors --- */

void panel_entry_get(panel_t *p, u16 i, panel_entry_t *out)
{
    far_copy((u8 __far *)out,
             (const u8 __far *)MK_FP(p->ent_seg, i * sizeof(panel_entry_t)),
             sizeof(panel_entry_t));
}

void panel_entry_set(panel_t *p, u16 i, const panel_entry_t *in)
{
    far_copy((u8 __far *)MK_FP(p->ent_seg, i * sizeof(panel_entry_t)),
             (const u8 __far *)in,
             sizeof(panel_entry_t));
}

/* --- Sorting helper (simple insertion sort) --- */

static void panel_sort(panel_t *p)
{
    u16 i, j;
    u16 swap;
    panel_entry_t a, b, tmp;

    for (i = 1; i < p->count; i++) {
        j = i;
        while (j > 0) {
            panel_entry_get(p, j - 1, &a);
            panel_entry_get(p, j, &b);
            swap = 0;

            if (a.is_dir && !b.is_dir) {
                swap = 0;
            } else if (!a.is_dir && b.is_dir) {
                swap = 1;
            } else {
                if (m_strcmp(a.name, b.name) > 0)
                    swap = 1;
            }

            if (!swap) break;

            tmp = b;
            panel_entry_set(p, j, &a);
            panel_entry_set(p, j - 1, &tmp);
            j--;
        }
    }
}

/* --- Panel init --- */

void panel_init(panel_t *p)
{
    p->count = 0;
    p->sel = 0;
    p->scroll = 0;
    p->cwd[0] = '\0';
    p->ent_seg = (u16)syscall_int40(SYSCALL_ALLOC, 0, PANEL_ENT_PARAS, 0, 0, 0, 0);
}

void panel_done(panel_t *p)
{
    /* Free not strictly needed (child exit reclaims), but good form */
    if (p->ent_seg) {
        syscall_int40(SYSCALL_FREE, 0, p->ent_seg, 0, 0, 0, 0);
        p->ent_seg = 0;
    }
}

/* --- Panel refresh (read directory) --- */

void panel_refresh(panel_t *p)
{
    ncd_dir_t dir;
    ncd_dirent_t ent;
    panel_entry_t entry;
    u16 vis;

    /* Get current directory */
    ncd_getcwd(p->cwd, sizeof(p->cwd));

    p->count = 0;

    if (ncd_opendir(&dir) != 0)
        return;

    while (ncd_readdir(&dir, &ent) == 0) {
        if (ent.name[0] == 0 || ent.name[0] == 0xE5)
            continue;
        if (ncd_is_dot_entry(&ent))
            continue;
        if (p->count >= MAX_ENTRIES)
            break;

        ncd_format_name(&ent, entry.name, NAME_MAX);
        entry.is_dir = (ent.attrs & DIR_ATTR_DIRECTORY) ? 1 : 0;
        entry.size = ent.file_size;
        entry.date = ent.date;
        entry.time = ent.time;
        entry.selected = 0;
        panel_entry_set(p, p->count, &entry);
        p->count++;
    }

    ncd_closedir(&dir);
    panel_sort(p);

    /* Clamp selection and scroll */
    if (p->count == 0) {
        p->sel = 0;
        p->scroll = 0;
    } else {
        if (p->sel >= p->count) p->sel = p->count - 1;
        vis = panel_get_visible(p);
        if (p->sel < p->scroll) p->scroll = p->sel;
        if (p->scroll + vis > p->count) p->scroll = p->count - vis;
    }
}

u16 panel_get_visible(panel_t *p)
{
    (void)p;
    return (u16)(ROW_FILE_LAST - ROW_FILE_FIRST + 1);
}

u16 panel_count_selected(panel_t *p)
{
    u16 i, count = 0;
    panel_entry_t e;
    for (i = 0; i < p->count; i++) {
        panel_entry_get(p, i, &e);
        if (e.selected) count++;
    }
    return count;
}

const char *panel_get_name(panel_t *p)
{
    static panel_entry_t e;
    if (p->sel < p->count) {
        panel_entry_get(p, p->sel, &e);
        return e.name;
    }
    return "";
}

const char *panel_get_cwd(panel_t *p)
{
    return p->cwd;
}

/* --- Navigation --- */

static void panel_clamp_scroll(panel_t *p)
{
    u16 vis = panel_get_visible(p);
    if (p->count <= vis) {
        p->scroll = 0;
    } else {
        if (p->sel < p->scroll) p->scroll = p->sel;
        if (p->scroll + vis > p->count)
            p->scroll = p->count - vis;
    }
}

void panel_up(panel_t *p)
{
    if (p->sel > 0) {
        p->sel--;
        panel_clamp_scroll(p);
    }
}

void panel_down(panel_t *p)
{
    if (p->sel < p->count - 1) {
        p->sel++;
        panel_clamp_scroll(p);
    }
}

void panel_page_up(panel_t *p)
{
    u16 vis = panel_get_visible(p);
    if (p->sel >= vis) {
        p->sel -= vis;
    } else {
        p->sel = 0;
    }
    panel_clamp_scroll(p);
}

void panel_page_down(panel_t *p)
{
    u16 vis = panel_get_visible(p);
    if (p->sel + vis < p->count) {
        p->sel += vis;
    } else {
        p->sel = p->count - 1;
    }
    panel_clamp_scroll(p);
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
        if (p->count > vis)
            p->scroll = p->count - vis;
        else
            p->scroll = 0;
    }
}

void panel_toggle_select(panel_t *p)
{
    panel_entry_t e;
    if (p->sel < p->count) {
        panel_entry_get(p, p->sel, &e);
        e.selected = !e.selected;
        panel_entry_set(p, p->sel, &e);
    }
}

int panel_enter_dir(panel_t *p)
{
    panel_entry_t e;
    if (p->sel < p->count) {
        panel_entry_get(p, p->sel, &e);
        if (e.is_dir) {
            ncd_chdir(e.name);
            panel_refresh(p);
            return 1;
        }
    }
    return 0;
}

void panel_parent_dir(panel_t *p)
{
    char cwd[PATH_MAX];
    ncd_getcwd(cwd, sizeof(cwd));

    /* If not at root, go up one level */
    if (cwd[0] == '/' && cwd[1] != '\0') {
        /* Find last '/' */
        u16 i = (u16)strlen(cwd) - 1;
        while (i > 0 && cwd[i] != '/') i--;
        if (i > 0) {
            cwd[i] = '\0';
            ncd_chdir(cwd);
        } else {
            ncd_chdir("/");
        }
    }
    panel_refresh(p);
}

/* --- Rendering --- */

/* Draw outer border around both panes (rows 0..20, cols 0..79) with divider at col 40 */
void render_pane_borders(void)
{
    int c, r;

    /* Top border (row 0) */
    vid_putat(0, 0, BOX_TL, A_NORMAL);
    vid_putat(0, 79, BOX_TR, A_NORMAL);
    for (c = 1; c < 79; c++)
        vid_putat(0, c, BOX_H, A_NORMAL);

    /* Bottom border (row 20) */
    vid_putat(20, 0, BOX_BL, A_NORMAL);
    vid_putat(20, 79, BOX_BR, A_NORMAL);
    for (c = 1; c < 79; c++)
        vid_putat(20, c, BOX_H, A_NORMAL);

    /* Side borders and divider */
    for (r = 1; r < 20; r++) {
        vid_putat(r, 0, BOX_V, A_NORMAL);
        vid_putat(r, 79, BOX_V, A_NORMAL);
        vid_putat(r, 40, BOX_V, A_NORMAL);
    }

    /* T-junctions on top/bottom at divider */
    vid_putat(0, 40, BOX_TJ, A_NORMAL);
    vid_putat(20, 40, BOX_BJ, A_NORMAL);
}

#if 0
static void pad_right(char *buf, u16 target)
{
    u16 len = (u16)strlen(buf);
    while (len < target) {
        buf[len++] = ' ';
    }
    buf[len] = '\0';
}
#endif

void panel_render(panel_t *p, int pane_id)
{
    int left_col = pane_id == PANEL_LEFT ? 1 : 41;
    int right_col = pane_id == PANEL_LEFT ? 39 : 79;
    int path_row = ROW_PATH;
    int header_row = ROW_HEADER;
    int file_start = ROW_FILE_FIRST;
    int file_end = ROW_FILE_LAST;
    int vis_rows = file_end - file_start + 1;

    u16 i, r;
    char tmp[32];
    char dt_buf[16];
    u8 attr;
    panel_entry_t e;

    /* Clear pane area */
    for (r = file_start; r <= (u16)file_end; r++)
        vid_fill(r, left_col, right_col - left_col + 1, ' ', A_NORMAL);

    /* Directory path header (row 1) */
    vid_fill(path_row, left_col, right_col - left_col + 1, ' ', A_PATH);
    vid_puts(path_row, left_col, p->cwd, A_PATH);

    /* Column headers (row 2) */
    vid_fill(header_row, left_col, right_col - left_col + 1, ' ', A_HEADER);
    vid_puts(header_row, left_col + 1, "NAME", A_HEADER);
    vid_puts(header_row, left_col + 1 + COL_NAME_WIDTH, "SIZE", A_HEADER);
    vid_puts(header_row, left_col + 1 + COL_NAME_WIDTH + COL_SIZE_WIDTH,
             "DATE/TIME", A_HEADER);

    /* File entries */
    for (i = 0; i < (u16)vis_rows && (i + p->scroll) < p->count; i++) {
        r = (u16)(file_start + i);
        panel_entry_get(p, p->scroll + i, &e);

        /* Determine attribute */
        if (p->scroll + i == p->sel) {
            attr = A_SELECTED;
        } else if (e.is_dir) {
            attr = A_DIR;
        } else {
            attr = A_FILE;
        }

        /* Clear this row */
        vid_fill(r, left_col, right_col - left_col + 1, ' ', attr);

        /* Selection marker */
        if (e.selected) {
            vid_putat(r, left_col, '*', A_MARKER);
        } else {
            vid_putat(r, left_col, ' ', attr);
        }

        /* Name column */
        vid_puts(r, left_col + 2, e.name, attr);

        /* Size column */
        if (e.is_dir) {
            vid_puts(r, left_col + 2 + COL_NAME_WIDTH, "<DIR>", attr);
        } else {
            m_u32toa(e.size, tmp);
            vid_puts(r, left_col + 2 + COL_NAME_WIDTH, tmp, attr);
        }

        /* Date/time column */
        ncd_format_datetime(e.date, e.time, dt_buf, sizeof(dt_buf));
        vid_puts(r, left_col + 2 + COL_NAME_WIDTH + COL_SIZE_WIDTH, dt_buf, attr);
    }
}

/* --- UI Chrome --- */

void render_menu_bar(void)
{
    vid_fill(ROW_MENU, 0, COLS, ' ', A_MENU);
    vid_puts(ROW_MENU, 1, " F3:View  F4:Edit  F5:Copy  F6:Move  F7:Mkdir  F8:Del  F10:Quit",
             A_MENU_KEY);
}

void render_separator(void)
{
    int c;
    for (c = 0; c < COLS; c++) {
        if (c == 0)
            vid_putat(ROW_SEP, c, BOX_TL, A_SEPARATOR);
        else if (c == COLS - 1)
            vid_putat(ROW_SEP, c, BOX_TR, A_SEPARATOR);
        else if (c == 40)
            vid_putat(ROW_SEP, c, BOX_LJ, A_SEPARATOR);
        else
            vid_putat(ROW_SEP, c, BOX_H, A_SEPARATOR);
    }
}

void render_status_line(void)
{
    char tmp[80];
    char sc_buf[8];
    u16 sel_count;

    vid_fill(ROW_STATUS, 0, COLS, ' ', A_STATUS);

    /* Active panel indicator */
    tmp[0] = '\0';
    if (active_panel == PANEL_LEFT) {
        strcpy(tmp, "[Left]");
    } else {
        strcpy(tmp, "[Right]");
    }

    /* Selection count */
    sel_count = panel_count_selected(&panel_left) + panel_count_selected(&panel_right);
    m_u32toa((u32)sel_count, sc_buf);
    strcat(tmp, " Sel:");
    strcat(tmp, sc_buf);

    vid_puts(ROW_STATUS, 1, tmp, A_STATUS_HI);
}

void render_help_line(void)
{
    vid_fill(ROW_HELP, 0, COLS, ' ', A_NORMAL);
    vid_puts(ROW_HELP, 1,
             "Arrows:Navigate  Enter:OpenDir  Bs:Parent  Tab:Switch  Ins:Select  F10:Quit",
             A_NORMAL);
}
