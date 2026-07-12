/* Menu bar and pull-down menus, MS-DOS EDIT style.
 * Alt-F / Alt-R / Alt-S open a menu; arrows navigate (Left/Right
 * switch menus), Enter selects, Esc closes. Returns an MI_* id.
 */
#include "medit.h"
#include "edit.h"
#include "menu.h"

u8 menu_wrap_flag;

typedef struct {
    const char *label;      /* "-" = separator */
    int id;
} MItem;

static const MItem fil_items[] = {
    { "Ny",                    MI_NY },
    { S_AA "pne...",           MI_APNE },
    { "Lagre",                 MI_LAGRE },
    { "Lagre som...",          MI_LAGRESOM },
    { "-",                     0 },
    { "Avslutt",               MI_AVSLUTT },
};

static const MItem red_items[] = {
    { "Klipp ut    Shift+Del", MI_KLIPP },
    { "Kopier      Ctrl+Ins",  MI_KOPIER },
    { "Lim inn     Shift+Ins", MI_LIM },
    { "-",                     0 },
    { "Tekstbryting",          MI_BRYT },
};

static const MItem sok_items[] = {
    { "Finn...",               MI_FINN },
    { "Finn neste  F3",        MI_FINNNESTE },
    { "Erstatt...",            MI_ERSTATT },
};

static const MItem *menus[3]  = { fil_items, red_items, sok_items };
static const int menu_n[3]    = { 6, 5, 3 };
static const char *titles[3]  = { "Fil", "Rediger", "S" S_oe "k" };
static const int title_col[3] = { 2, 8, 18 };

void menu_draw_bar(int active)
{
    int m;
    u8 a;
    vid_fill(0, 0, COLS, ' ', A_BAR);
    for (m = 0; m < 3; m++) {
        a = (m == active) ? A_TEXT : A_BAR;
        vid_putat(0, title_col[m] - 1, ' ', a);
        vid_puts(0, title_col[m], titles[m], a);
        vid_putat(0, title_col[m] + (int)strlen(titles[m]), ' ', a);
    }
}

static void draw_dropdown(int m, int sel)
{
    const MItem *it = menus[m];
    int n = menu_n[m];
    int w = 0, i, len, left, r;
    u8 a;

    for (i = 0; i < n; i++) {
        len = (int)strlen(it[i].label);
        if (len > w)
            w = len;
    }
    w += 4;                             /* borders + padding */
    left = title_col[m] - 1;
    if (left + w > COLS)
        left = COLS - w;

    vid_putat(1, left, 0xDA, A_BAR);
    vid_putat(1, left + w - 1, 0xBF, A_BAR);
    for (i = 1; i < w - 1; i++)
        vid_putat(1, left + i, 0xC4, A_BAR);
    for (r = 0; r < n; r++) {
        if (it[r].label[0] == '-' && it[r].label[1] == '\0') {
            vid_putat(2 + r, left, 0xC3, A_BAR);
            vid_putat(2 + r, left + w - 1, 0xB4, A_BAR);
            for (i = 1; i < w - 1; i++)
                vid_putat(2 + r, left + i, 0xC4, A_BAR);
            continue;
        }
        a = (r == sel) ? A_TEXT : A_BAR;
        vid_putat(2 + r, left, 0xB3, A_BAR);
        vid_putat(2 + r, left + w - 1, 0xB3, A_BAR);
        vid_fill(2 + r, left + 1, w - 2, ' ', a);
        vid_puts(2 + r, left + 2, it[r].label, a);
        if (it[r].id == MI_BRYT && menu_wrap_flag)
            vid_putat(2 + r, left + 1, 0xFB, a);    /* check mark */
    }
    vid_putat(2 + n, left, 0xC0, A_BAR);
    vid_putat(2 + n, left + w - 1, 0xD9, A_BAR);
    for (i = 1; i < w - 1; i++)
        vid_putat(2 + n, left + i, 0xC4, A_BAR);
}

static int is_sep(int m, int sel)
{
    const char *l = menus[m][sel].label;
    return l[0] == '-' && l[1] == '\0';
}

int menu_open(int m)
{
    int sel = 0;
    unsigned k;

    vid_cursor_hide();
    for (;;) {
        edit_render();                  /* repaint under old dropdown */
        menu_draw_bar(m);
        draw_dropdown(m, sel);
        vid_flush();

        k = kbd_get();
        switch (k) {
        case K_ESC:
            return 0;
        case K_LEFT:
            m = (m + 2) % 3;
            sel = 0;
            break;
        case K_RIGHT:
            m = (m + 1) % 3;
            sel = 0;
            break;
        case K_UP:
            do
                sel = (sel + menu_n[m] - 1) % menu_n[m];
            while (is_sep(m, sel));
            break;
        case K_DOWN:
            do
                sel = (sel + 1) % menu_n[m];
            while (is_sep(m, sel));
            break;
        case K_ENTER:
            return menus[m][sel].id;
        default:
            break;
        }
    }
}
