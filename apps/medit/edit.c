/* Editor core: cursor movement, insert/delete, scrolling, rendering.
 * No global line index (low memory): everything scans locally from
 * two anchors, e_top (top-of-screen line start) and e_cur.
 */
#include "medit.h"
#include "vid.h"
#include "kbd.h"
#include "gap.h"
#include "doc.h"
#include "clip.h"
#include "edit.h"
#include "menu.h"

#define WRAP_COL 78

u16  e_cur;
u16  e_top;
u16  e_topline = 1;
u8   e_modified;
u8   e_have_name;
char e_fname[68];

static int  e_prefcol;      /* desired display column for up/down */
static int  e_hscroll;      /* horizontal scroll in display columns */
static int  e_currow;       /* cursor row within text area (0-based) */
static int  e_curcol;       /* cursor display column (0-based) */
static const char *e_msg;

static u8  e_selon;         /* selection active */
static u16 e_selanchor;     /* fixed end of the selection */
static u16 e_sello, e_selhi;    /* normalized range, set per render */

void edit_init(void)
{
    e_cur = e_top = 0;
    e_topline = 1;
    e_prefcol = e_hscroll = 0;
    e_modified = 0;
    e_have_name = 0;
    e_fname[0] = '\0';
    e_msg = 0;
    e_selon = 0;
    e_selanchor = 0;
}

static void sel_normalize(void)
{
    if (e_selon && e_selanchor != e_cur) {
        if (e_selanchor < e_cur) {
            e_sello = e_selanchor;
            e_selhi = e_cur;
        } else {
            e_sello = e_cur;
            e_selhi = e_selanchor;
        }
    } else {
        e_sello = e_selhi = 0;
    }
}

/* 1-based line number of pos (full scan - used only when a deletion
 * crossed the top of the screen) */
static u16 count_line(u16 pos)
{
    u16 i, l = 1;
    for (i = 0; i < pos; i++)
        if (gb_at(i) == '\n')
            l++;
    return l;
}

/* delete [lo,hi), fix cursor/top anchors */
static void del_range(u16 lo, u16 hi)
{
    gb_del(lo, hi - lo);
    e_cur = lo;
    if (lo < e_top) {
        e_top = doc_line_home(lo);
        e_topline = count_line(e_top);
    }
    e_modified = 1;
    e_selon = 0;
}

/* delete the active selection; returns 1 if there was one */
static int del_selection(void)
{
    sel_normalize();
    if (!e_selon || e_sello == e_selhi) {
        e_selon = 0;
        return 0;
    }
    del_range(e_sello, e_selhi);
    return 1;
}

void edit_set_msg(const char *m)
{
    e_msg = m;
}

/* display column of pos, expanding tabs, counting from line home */
static int disp_col(u16 home, u16 pos)
{
    int col = 0;
    u16 p;
    for (p = home; p < pos; p++) {
        if (gb_at(p) == '\t')
            col = (col / TABSTOP + 1) * TABSTOP;
        else
            col++;
    }
    return col;
}

/* offset within the line starting at home closest to display column want */
static u16 pos_at_col(u16 home, int want)
{
    int col = 0;
    u16 p = home, end = doc_line_end(home);
    while (p < end && col < want) {
        if (gb_at(p) == '\t')
            col = (col / TABSTOP + 1) * TABSTOP;
        else
            col++;
        p++;
    }
    return p;
}

static void set_prefcol(void)
{
    e_prefcol = disp_col(doc_line_home(e_cur), e_cur);
}

/* visual row helpers (word wrap) ----------------------------------------- */

/* end of visual row starting at pos, within logical line [pos, lend).
 * wraps at last space before WRAP_COL; hard-breaks at WRAP_COL.
 * *next = start of next visual row. */
static u16 vis_row_end(u16 pos, u16 lend, u16 *next)
{
    int col = 0, sp_col = -1;
    u16 p, sp_pos = pos;
    for (p = pos; p < lend; p++) {
        u8 c = gb_at(p);
        int nc = (c == '\t') ? (col / TABSTOP + 1) * TABSTOP : col + 1;
        if (nc >= WRAP_COL) {
            if (sp_col >= 0) { *next = sp_pos + 1; return sp_pos; }
            *next = p;
            return p;
        }
        if (c == ' ') { sp_pos = p; sp_col = col; }
        col = nc;
    }
    *next = lend;
    return lend;
}

/* number of visual rows a logical line [home, lend) occupies */
static int vis_rows_of(u16 home, u16 lend)
{
    u16 p = home, np;
    int rows = 0;
    do { vis_row_end(p, lend, &np); rows++; p = np; } while (p < lend);
    return rows;
}

/* ------------------------------------------------------------------------ */

static void ensure_visible(void)
{
    u16 chome = doc_line_home(e_cur);
    u16 lend, p, np;
    int rows;

    e_top = doc_line_home(e_top);
    while (chome < e_top) {
        e_top = doc_prev_line(e_top);
        if (e_topline > 1)
            e_topline--;
    }

    rows = 0;
    p = e_top;
    while (p < chome) {
        lend = doc_line_end(p);
        rows += menu_wrap_flag ? vis_rows_of(p, lend) : 1;
        p = doc_next_line(lend);
    }

    if (menu_wrap_flag) {
        u16 vp = chome;
        lend = doc_line_end(chome);
        while (vp < lend) {
            vis_row_end(vp, lend, &np);
            if (e_cur < np || np >= lend) {
                e_curcol = disp_col(vp, e_cur);
                break;
            }
            rows++;
            vp = np;
        }
        if (vp >= lend)
            e_curcol = 0;
        e_hscroll = 0;
    } else {
        e_curcol = disp_col(chome, e_cur);
        if (e_curcol < e_hscroll)
            e_hscroll = e_curcol;
        if (e_curcol >= e_hscroll + COLS)
            e_hscroll = e_curcol - COLS + 1;
    }

    while (rows >= TEXT_ROWS) {
        lend = doc_line_end(e_top);
        rows -= menu_wrap_flag ? vis_rows_of(e_top, lend) : 1;
        e_top = doc_next_line(lend);
        e_topline++;
        if (rows < 0) { rows = 0; break; }
    }
    e_currow = rows;
}

static void render_line(int row, u16 home, u16 end)
{
    int col = 0, sc;
    u16 p;
    u8 c, a;
    for (p = home; p < end; p++) {
        c = gb_at(p);
        a = (e_selon && p >= e_sello && p < e_selhi) ? A_SEL : A_TEXT;
        if (c == '\t') {
            int next = (col / TABSTOP + 1) * TABSTOP;
            while (col < next) {
                sc = col - e_hscroll;
                if (sc >= 0 && sc < COLS)
                    vid_putat(row, sc, ' ', a);
                col++;
            }
        } else {
            sc = col - e_hscroll;
            if (sc >= 0 && sc < COLS)
                vid_putat(row, sc, c, a);
            col++;
        }
        if (col - e_hscroll >= COLS)
            break;
    }
}

/* very small unsigned-to-decimal; returns chars written, no NUL */
static int fmt_u16(char *out, u16 v)
{
    char tmp[5];
    int i = 0, n = 0;
    do {
        tmp[i++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (i)
        out[n++] = tmp[--i];
    return n;
}

static void draw_status(void)
{
    char line[COLS];
    const char *name;
    char *q;
    int i;

    memset(line, ' ', COLS);
    if (e_msg) {
        for (i = 0; e_msg[i] && i < 50; i++)
            line[1 + i] = e_msg[i];
    } else {
        name = e_have_name ? e_fname : "Uten navn";
        for (i = 0; name[i] && i < 40; i++)
            line[1 + i] = name[i];
        if (e_modified)
            memcpy(line + 44, "Endret", 6);
    }
    q = line + 58;
    memcpy(q, "Lin ", 4);
    q += 4;
    q += fmt_u16(q, (u16)(e_topline + e_currow));
    q += 2;
    memcpy(q, "Kol ", 4);
    q += 4;
    q += fmt_u16(q, (u16)(e_curcol + 1));
    for (i = 0; i < COLS; i++)
        vid_putat(STATUS_ROW, i, (u8)line[i], A_BAR);
}

void edit_render(void)
{
    u16 n, p, lend, np;
    int r;
    u8 done = 0;

    ensure_visible();
    sel_normalize();
    n = gb_len();
    p = e_top;
    for (r = 0; r < TEXT_ROWS; r++) {
        vid_fill(TEXT_TOP + r, 0, COLS, ' ', A_TEXT);
        if (!done) {
            lend = doc_line_end(p);
            if (menu_wrap_flag) {
                u16 vp = p;
                while (r < TEXT_ROWS) {
                    u16 ve = vis_row_end(vp, lend, &np);
                    render_line(TEXT_TOP + r, vp, ve);
                    if (np >= lend)
                        break;
                    r++;
                    if (r >= TEXT_ROWS)
                        break;
                    vid_fill(TEXT_TOP + r, 0, COLS, ' ', A_TEXT);
                    vp = np;
                }
            } else {
                render_line(TEXT_TOP + r, p, lend);
            }
            if (lend >= n)
                done = 1;
            else
                p = doc_next_line(lend);
        }
    }
    draw_status();
}

void edit_place_cursor(void)
{
    vid_cursor(TEXT_TOP + e_currow, e_curcol - e_hscroll);
}

static void ins_char(u8 c)
{
    del_selection();
    if (gb_insert(e_cur, c) != 0) {
        edit_set_msg("Minnet er fullt");
        return;
    }
    e_cur++;
    e_modified = 1;
    set_prefcol();
}

static void cmd_enter(void)
{
    del_selection();
    if (gb_cap() - gb_len() < 2) {
        edit_set_msg("Minnet er fullt");
        return;
    }
    gb_insert(e_cur, '\r');
    e_cur++;
    gb_insert(e_cur, '\n');
    e_cur++;
    e_modified = 1;
    e_prefcol = 0;
}

static void cmd_backspace(void)
{
    u16 prev;
    if (del_selection()) {
        set_prefcol();
        return;
    }
    if (e_cur == 0)
        return;
    prev = e_cur - 1;
    if (gb_at(prev) == '\n' && prev > 0 && gb_at(prev - 1) == '\r')
        prev--;
    if (e_cur == e_top && e_topline > 1)
        e_topline--;                    /* joining into the line above */
    gb_del(prev, e_cur - prev);
    e_cur = prev;
    e_modified = 1;
    set_prefcol();
}

static void cmd_del(void)
{
    u8 c;
    if (del_selection()) {
        set_prefcol();
        return;
    }
    if (e_cur >= gb_len())
        return;
    c = gb_at(e_cur);
    if (c == '\r' || c == '\n')
        gb_del(e_cur, doc_next_line(e_cur) - e_cur);
    else
        gb_del(e_cur, 1);
    e_modified = 1;
}

static void cmd_left(void)
{
    if (e_cur == 0)
        return;
    e_cur--;
    if (gb_at(e_cur) == '\n' && e_cur > 0 && gb_at(e_cur - 1) == '\r')
        e_cur--;
    set_prefcol();
}

static void cmd_right(void)
{
    u8 c;
    if (e_cur >= gb_len())
        return;
    c = gb_at(e_cur);
    if (c == '\r' || c == '\n')
        e_cur = doc_next_line(e_cur);
    else
        e_cur++;
    set_prefcol();
}

static void cmd_up(void)
{
    u16 chome = doc_line_home(e_cur);
    if (chome == 0)
        return;
    e_cur = pos_at_col(doc_prev_line(chome), e_prefcol);
}

static void cmd_down(void)
{
    u16 end = doc_line_end(e_cur);
    if (end >= gb_len())
        return;
    e_cur = pos_at_col(doc_next_line(end), e_prefcol);
}

static void cmd_home(void)
{
    e_cur = doc_line_home(e_cur);
    e_prefcol = 0;
}

static void cmd_end(void)
{
    e_cur = doc_line_end(e_cur);
    set_prefcol();
}

void edit_copy(void)
{
    sel_normalize();
    if (!e_selon || e_sello == e_selhi)
        return;
    if (clip_set(e_sello, e_selhi - e_sello) != 0)
        edit_set_msg("Ikke plass til utklipp");
}

void edit_cut(void)
{
    sel_normalize();
    if (!e_selon || e_sello == e_selhi)
        return;
    if (clip_set(e_sello, e_selhi - e_sello) != 0) {
        edit_set_msg("Ikke plass til utklipp");
        return;
    }
    del_range(e_sello, e_selhi);
    set_prefcol();
}

void edit_paste(void)
{
    u16 n = clip_len();
    if (n == 0)
        return;
    del_selection();
    if (gb_insert_block(e_cur, clip_ptr(), n) != 0) {
        edit_set_msg("Minnet er fullt");
        return;
    }
    e_cur += n;
    e_modified = 1;
    set_prefcol();
}

void edit_select(u16 lo, u16 hi)
{
    e_selanchor = lo;
    e_cur = hi;
    e_selon = (lo != hi);
    set_prefcol();
}

void edit_insert_str(const char *s, u16 n)
{
    del_selection();
    if (gb_insert_block(e_cur, (const u8 __far *)s, n) != 0) {
        edit_set_msg("Minnet er fullt");
        return;
    }
    e_cur += n;
    e_modified = 1;
    set_prefcol();
}

static int is_nav_key(unsigned k)
{
    switch (k) {
    case K_UP: case K_DOWN: case K_LEFT: case K_RIGHT:
    case K_HOME: case K_END: case K_PGUP: case K_PGDN:
    case K_CHOME: case K_CEND:
        return 1;
    default:
        return 0;
    }
}

int edit_handle(unsigned k)
{
    int i;

    e_msg = 0;                          /* messages are one-shot */

    if (is_nav_key(k)) {
        if (kbd_shift & SH_SHIFT) {
            if (!e_selon) {
                e_selon = 1;
                e_selanchor = e_cur;
            }
        } else {
            e_selon = 0;
        }
    } else if (k == K_DEL && (kbd_shift & SH_SHIFT)) {
        edit_cut();                     /* Shift+Del */
        return 1;
    } else if (k == K_INS && (kbd_shift & SH_SHIFT)) {
        edit_paste();                   /* Shift+Ins */
        return 1;
    } else if (k == K_INS && (kbd_shift & SH_CTRL)) {
        edit_copy();                    /* Ctrl+Ins */
        return 1;
    }

    switch (k) {
    case K_UP:      cmd_up();    break;
    case K_DOWN:    cmd_down();  break;
    case K_LEFT:    cmd_left();  break;
    case K_RIGHT:   cmd_right(); break;
    case K_HOME:    cmd_home();  break;
    case K_END:     cmd_end();   break;
    case K_PGUP:
        for (i = 0; i < TEXT_ROWS - 1; i++)
            cmd_up();
        break;
    case K_PGDN:
        for (i = 0; i < TEXT_ROWS - 1; i++)
            cmd_down();
        break;
    case K_CHOME:
        e_cur = 0;
        e_top = 0;
        e_topline = 1;
        e_prefcol = 0;
        break;
    case K_CEND:
        e_cur = gb_len();
        set_prefcol();
        break;
    case K_DEL:     cmd_del();       break;
    case K_BS:      cmd_backspace(); break;
    case K_ENTER:   cmd_enter();     break;
    case K_TAB:     ins_char('\t');  break;
    default:
        if (k >= 32 && k < 0x100 && k != 127) {
            ins_char((u8)k);
            break;
        }
        return 0;
    }
    return 1;
}
