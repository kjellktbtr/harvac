/* dlg.c -- Modal dialogs for NCD.
 * Box-drawing using cp437 characters, inverse attribute for dialog body.
 * Pattern copied from MEDIT's dlg.c. */

#include "ncd.h"
#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "vid.h"
#include "kbd.h"
#include "dlg.h"

static void draw_frame(int top, int left, int h, int w)
{
    int r, c;
    vid_putat(top, left, BOX_TL, A_DLG);
    vid_putat(top, left + w - 1, BOX_TR, A_DLG);
    vid_putat(top + h - 1, left, BOX_BL, A_DLG);
    vid_putat(top + h - 1, left + w - 1, BOX_BR, A_DLG);
    for (c = 1; c < w - 1; c++) {
        vid_putat(top, left + c, BOX_H, A_DLG);
        vid_putat(top + h - 1, left + c, BOX_H, A_DLG);
    }
    for (r = 1; r < h - 1; r++) {
        vid_putat(top + r, left, BOX_V, A_DLG);
        vid_putat(top + r, left + w - 1, BOX_V, A_DLG);
        vid_fill(top + r, left + 1, w - 2, ' ', A_DLG);
    }
}

static void draw_button(int row, int col, const char *label, int selected)
{
    u8 a = selected ? A_DLG_HI : A_DLG;
    vid_putat(row, col, '<', a);
    vid_puts(row, col + 2, label, a);
    vid_putat(row, col + 3 + (int)strlen(label), '>', a);
}

int dlg_msgbox(const char *text, int buttons)
{
    static const char *lbl_ok[1]   = { "OK" };
    static const char *lbl_jna[2]  = { "Yes", "No" };
    static const int   res_ok[1]   = { DR_OK };
    static const int   res_jna[2]  = { DR_YES, DR_NO };
    const char **lbl;
    const int *res;
    int w, top, left, tlen, bw;
    int nbtn, sel = 0, i, col, brow;
    unsigned k;

    switch (buttons) {
    case DB_JNA:
        lbl = lbl_jna; res = res_jna; nbtn = 2;
        break;
    default:
        lbl = lbl_ok;  res = res_ok;  nbtn = 1;
        break;
    }
    bw = 0;
    for (i = 0; i < nbtn; i++)
        bw += (int)strlen(lbl[i]) + 6;

    tlen = (int)strlen(text);
    w = tlen + 6;
    if (w < bw + 4)
        w = bw + 4;
    if (w > 76)
        w = 76;
    top = 8;
    left = (COLS - w) / 2;

    vid_cursor_hide();
    for (;;) {
        draw_frame(top, left, 6, w);
        vid_puts(top + 1, left + (w - tlen) / 2, text, A_DLG);
        brow = top + 3;
        col = left + (w - bw) / 2;
        for (i = 0; i < nbtn; i++) {
            draw_button(brow, col, lbl[i], sel == i);
            col += (int)strlen(lbl[i]) + 6;
        }
        vid_flush();

        k = kbd_get();
        switch (k) {
        case K_ESC:
            return DR_CANCEL;
        case K_ENTER:
            return res[sel];
        case K_LEFT:
            sel = (sel + nbtn - 1) % nbtn;
            break;
        case K_RIGHT:
        case K_TAB:
            sel = (sel + 1) % nbtn;
            break;
        default:
            if (nbtn >= 2) {
                if (k == 'y' || k == 'Y') return DR_YES;
                if (k == 'n' || k == 'N') return DR_NO;
            }
            break;
        }
    }
}

/* --- text input field --- */

typedef struct {
    char *buf;
    int max, len, pos;
} Field;

static void field_draw(int row, int col, int w, Field *f)
{
    int i;
    for (i = 0; i < w; i++)
        vid_putat(row, col + i, i < f->len ? (u8)f->buf[i] : ' ', A_DLG_HI);
}

static int field_key(Field *f, unsigned k)
{
    switch (k) {
    case K_LEFT:
        if (f->pos > 0) f->pos--;
        return 1;
    case K_RIGHT:
        if (f->pos < f->len) f->pos++;
        return 1;
    case K_HOME:
        f->pos = 0;
        return 1;
    case K_END:
        f->pos = f->len;
        return 1;
    case K_BS:
        if (f->pos > 0) {
            memmove(f->buf + f->pos - 1, f->buf + f->pos, f->len - f->pos);
            f->pos--;
            f->len--;
        }
        return 1;
    case K_DEL:
        if (f->pos < f->len) {
            memmove(f->buf + f->pos, f->buf + f->pos + 1,
                    f->len - f->pos - 1);
            f->len--;
        }
        return 1;
    default:
        if (k >= 32 && k < 0x100 && k != 127 && f->len < f->max) {
            memmove(f->buf + f->pos + 1, f->buf + f->pos, f->len - f->pos);
            f->buf[f->pos++] = (char)k;
            f->len++;
            return 1;
        }
        return 0;
    }
}

int dlg_input(const char *title, char *buf, int max)
{
    Field f;
    int w = 44, top = 9, left = (COLS - w) / 2;
    unsigned k;

    f.buf = buf;
    f.max = max;
    if (f.max > w - 4)
        f.max = w - 4;
    f.len = (int)strlen(buf);
    f.pos = f.len;

    vid_cursor_hide();
    for (;;) {
        draw_frame(top, left, 5, w);
        vid_puts(top, left + 2, title, A_DLG);
        field_draw(top + 2, left + 2, w - 4, &f);
        vid_flush();

        k = kbd_get();
        if (k == K_ESC)
            return DR_CANCEL;
        if (k == K_ENTER)
            return DR_OK;
        if (!field_key(&f, k))
            continue;
    }
}