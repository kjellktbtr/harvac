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

/* Forward declaration */
static void panel_clamp_scroll(panel_t *p);

/* Re-enter the panel's own directory. The kernel CWD is a single global
 * shared by both panels (and child programs), so every operation that
 * resolves names must sync it to the panel's directory first. */
void panel_sync_cwd(panel_t *p)
{
    if (p->cwd[0] != '\0')
        ncd_chdir(p->cwd);
}

void panel_refresh(panel_t *p)
{
    ncd_dir_t dir;
    ncd_dirent_t ent;
    panel_entry_t entry;

    /* Re-enter this panel's directory, then read back the (possibly
     * corrected) canonical CWD */
    panel_sync_cwd(p);
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
        panel_clamp_scroll(p);
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
            panel_sync_cwd(p);
            ncd_chdir(e.name);
            ncd_getcwd(p->cwd, sizeof(p->cwd));
            p->sel = 0;
            p->scroll = 0;
            panel_refresh(p);
            return 1;
        }
    }
    return 0;
}

void panel_parent_dir(panel_t *p)
{
    char cwd[PATH_MAX];
    panel_sync_cwd(p);
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

void panel_render(panel_t *p, int pane_id)
{
    int left_col = pane_id == PANEL_LEFT ? 1 : 41;
    /* interior width: left pane has a spare column before the divider */
    int inner = pane_id == PANEL_LEFT ? 39 : 38;
    int is_active = ((u8)pane_id == active_panel);
    int file_start = ROW_FILE_FIRST;
    int file_end = ROW_FILE_LAST;
    int vis_rows = file_end - file_start + 1;

    u16 i, r;
    char tmp[16];
    u8 attr;
    u8 name_attr;
    panel_entry_t e;

    /* Directory path header (row 1) */
    vid_fill(ROW_PATH, left_col, inner, ' ', A_PATH);
    vid_puts(ROW_PATH, left_col, p->cwd, A_PATH);

    /* Column headers (row 2): |NAME        |SIZE |DATE      |TIME | */
    vid_fill(ROW_HEADER, left_col, inner, ' ', A_HEADER);
    vid_putat(ROW_HEADER, left_col + P_SEP0, '|', A_HEADER);
    vid_puts(ROW_HEADER, left_col + P_NAME, "NAME", A_HEADER);
    vid_putat(ROW_HEADER, left_col + P_SEP1, '|', A_HEADER);
    vid_puts(ROW_HEADER, left_col + P_SIZE, "SIZE", A_HEADER);
    vid_putat(ROW_HEADER, left_col + P_SEP2, '|', A_HEADER);
    vid_puts(ROW_HEADER, left_col + P_DATE, "DATE", A_HEADER);
    vid_putat(ROW_HEADER, left_col + P_SEP3, '|', A_HEADER);
    vid_puts(ROW_HEADER, left_col + P_TIME, "TIME", A_HEADER);
    vid_putat(ROW_HEADER, left_col + P_SEP4, '|', A_HEADER);

    /* File entries */
    for (i = 0; i < (u16)vis_rows; i++) {
        r = (u16)(file_start + i);

        if ((u16)(i + p->scroll) >= p->count) {
            vid_fill(r, left_col, inner, ' ', A_NORMAL);
            continue;
        }

        panel_entry_get(p, p->scroll + i, &e);

        /* Cursor bar only in the active panel (shows which pane has focus) */
        if (is_active && p->scroll + i == p->sel) {
            attr = A_SELECTED;
        } else if (e.is_dir) {
            attr = A_DIR;
        } else {
            attr = A_FILE;
        }
        /* Ins-selected entries keep their marker color in both panes */
        name_attr = e.selected ? A_MARKER : attr;

        /* Clear this row and draw column separators */
        vid_fill(r, left_col, inner, ' ', attr);
        vid_putat(r, left_col + P_SEP0, '|', attr);
        vid_putat(r, left_col + P_SEP1, '|', attr);
        vid_putat(r, left_col + P_SEP2, '|', attr);
        vid_putat(r, left_col + P_SEP3, '|', attr);
        vid_putat(r, left_col + P_SEP4, '|', attr);

        /* Name column: "NNNNNNNN EXT" */
        ncd_format_name12(e.name, tmp);
        vid_puts(r, left_col + P_NAME, tmp, name_attr);

        /* Size column (right-aligned 5) */
        if (e.is_dir) {
            vid_puts(r, left_col + P_SIZE, "<DIR>", attr);
        } else {
            m_format_size(e.size, tmp);
            vid_puts(r, left_col + P_SIZE, tmp, attr);
        }

        /* Date / time columns */
        ncd_format_date(e.date, tmp);
        vid_puts(r, left_col + P_DATE, tmp, attr);
        ncd_format_time(e.date, e.time, tmp);
        vid_puts(r, left_col + P_TIME, tmp, attr);
    }
}

/* --- UI Chrome --- */

/* Norton-style F-key bar on the bottom row: number in normal video,
 * label in inverse video. */
void render_fkey_bar(void)
{
    static const char *nums[]   = { "3", "4", "5", "6", "7", "8", "10" };
    static const char *labels[] = { "View  ", "Edit  ", "Copy  ", "RenMov",
                                    "MkDir ", "Delete", "Quit  " };
    u16 col = 1;
    u16 i;

    vid_fill(ROW_FKEYS, 0, COLS, ' ', A_NORMAL);
    for (i = 0; i < 7; i++) {
        vid_puts(ROW_FKEYS, col, nums[i], A_NORMAL);
        col += (u16)strlen(nums[i]);
        vid_puts(ROW_FKEYS, col, labels[i], A_INVERSE);
        col += (u16)strlen(labels[i]) + 2;
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
