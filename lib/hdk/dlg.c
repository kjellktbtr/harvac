/* dlg.c -- Modal dialogs for HarvaC HDK.
 * Unified from apps/medit/dlg.c (Norwegian, with uppercase Field and filebox)
 * and apps/ncd/dlg.c (English, simpler subset).
 *
 * Localization: set hdk_lang = HDK_LANG_EN (default) or HDK_LANG_NO before
 * calling dialog functions. Result codes are HDK_* (language-independent). */

#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "string.h"
#include "hdk_video.h"
#include "hdk_keys.h"
#include "hdk_box.h"
#include "hdk_dialog.h"

/* Active language; default English */
int hdk_lang = HDK_LANG_EN;

/* ─── Button label tables ─── */

/* English */
static const char *lbl_ok_en[1]    = { "OK" };
static const char *lbl_yn_en[2]    = { "Yes", "No" };
static const char *lbl_ynca_en[3]  = { "Yes", "No", "Cancel" };
static const char *lbl_ynall_en[4] = { "Yes", "No", "All", "Cancel" };

/* Norwegian */
static const char *lbl_ok_no[1]    = { "OK" };
static const char *lbl_yn_no[2]    = { "Ja", "Nei" };
static const char *lbl_ynca_no[3]  = { "Ja", "Nei", "Avbryt" };
static const char *lbl_ynall_no[4] = { "Ja", "Nei", "Alle", "Avbryt" };

static const int res_ok[1]   = { HDK_OK };
static const int res_yn[2]   = { HDK_YES, HDK_NO };
static const int res_ynca[3] = { HDK_YES, HDK_NO, HDK_CANCEL };
static const int res_ynall[4]= { HDK_YES, HDK_NO, HDK_ALL, HDK_CANCEL };

/* ─── Low-level drawing ─── */

void dlg_draw_frame(int top, int left, int h, int w)
{
    int r, c;
    vid_putat(top, left,         BOX_TL, A_DLG);
    vid_putat(top, left + w - 1, BOX_TR, A_DLG);
    vid_putat(top + h - 1, left,         BOX_BL, A_DLG);
    vid_putat(top + h - 1, left + w - 1, BOX_BR, A_DLG);
    for (c = 1; c < w - 1; c++) {
        vid_putat(top,         left + c, BOX_H, A_DLG);
        vid_putat(top + h - 1, left + c, BOX_H, A_DLG);
    }
    for (r = 1; r < h - 1; r++) {
        vid_putat(top + r, left,         BOX_V, A_DLG);
        vid_putat(top + r, left + w - 1, BOX_V, A_DLG);
        vid_fill(top + r, left + 1, w - 2, ' ', A_DLG);
    }
}

void dlg_draw_button(int row, int col, const char *label, int selected)
{
    uint8_t a = selected ? A_DLG_HI : A_DLG;
    vid_putat(row, col,                              '<', a);
    vid_puts( row, col + 2,                          label, a);
    vid_putat(row, col + 2 + (int)strlen(label) + 1, '>', a);
}

/* ─── Text input field ─── */

void dlg_field_draw(int row, int col, int w, HdkField *f)
{
    int i;
    for (i = 0; i < w; i++)
        vid_putat(row, col + i,
                  (i < f->len) ? (uint8_t)f->buf[i] : ' ',
                  A_DLG_HI);
}

int dlg_field_key(HdkField *f, unsigned k)
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
            memmove(f->buf + f->pos - 1, f->buf + f->pos,
                    (uint16_t)(f->len - f->pos));
            f->pos--;
            f->len--;
        }
        return 1;
    case K_DEL:
        if (f->pos < f->len) {
            memmove(f->buf + f->pos, f->buf + f->pos + 1,
                    (uint16_t)(f->len - f->pos - 1));
            f->len--;
        }
        return 1;
    default:
        if (k >= 32 && k < 0x100 && k != 127 && f->len < f->max) {
            memmove(f->buf + f->pos + 1, f->buf + f->pos,
                    (uint16_t)(f->len - f->pos));
            f->buf[f->pos++] = (char)(
                f->uppercase && k >= 'a' && k <= 'z' ? k - 32 : k);
            f->len++;
            return 1;
        }
        return 0;
    }
}

/* ─── High-level dialogs ─── */

int dlg_msgbox(const char *text, int buttons)
{
    const char **lbl;
    const int  *res;
    int nbtn, sel = 0, i, col, brow;
    int w, top, left, tlen, bw;
    unsigned k;
    int use_no = (hdk_lang == HDK_LANG_NO);

    switch (buttons) {
    case DB_YESNO:
        lbl = use_no ? lbl_yn_no   : lbl_yn_en;
        res = res_yn;  nbtn = 2;
        break;
    case DB_YESNOCA:
        lbl = use_no ? lbl_ynca_no : lbl_ynca_en;
        res = res_ynca; nbtn = 3;
        break;
    case DB_YESNOALL:
        lbl = use_no ? lbl_ynall_no : lbl_ynall_en;
        res = res_ynall; nbtn = 4;
        break;
    default: /* DB_OK */
        lbl = use_no ? lbl_ok_no : lbl_ok_en;
        res = res_ok; nbtn = 1;
        break;
    }

    bw = 0;
    for (i = 0; i < nbtn; i++)
        bw += (int)strlen(lbl[i]) + 6;

    tlen = (int)strlen(text);
    w = tlen + 6;
    if (w < bw + 4) w = bw + 4;
    if (w > 76)     w = 76;
    top  = 8;
    left = (COLS - w) / 2;

    vid_cursor_hide();
    for (;;) {
        dlg_draw_frame(top, left, 6, w);
        vid_puts(top + 1, left + (w - tlen) / 2, text, A_DLG);
        brow = top + 3;
        col  = left + (w - bw) / 2;
        for (i = 0; i < nbtn; i++) {
            dlg_draw_button(brow, col, lbl[i], sel == i);
            col += (int)strlen(lbl[i]) + 6;
        }
        vid_flush();

        k = kbd_get();
        switch (k) {
        case K_ESC:
            return HDK_CANCEL;
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
                /* First-letter shortcuts — language-appropriate */
                if (use_no) {
                    if (k == 'j' || k == 'J') return HDK_YES;
                    if (k == 'n' || k == 'N') return HDK_NO;
                    if (nbtn >= 3 && (k == 'a' || k == 'A'))
                        return (nbtn == 4) ? HDK_ALL : HDK_CANCEL;
                } else {
                    if (k == 'y' || k == 'Y') return HDK_YES;
                    if (k == 'n' || k == 'N') return HDK_NO;
                    if (nbtn >= 4 && (k == 'a' || k == 'A')) return HDK_ALL;
                    if (nbtn >= 3 && (k == 'c' || k == 'C')) return HDK_CANCEL;
                }
            }
            break;
        }
    }
}

int dlg_input(const char *title, char *buf, int max)
{
    HdkField f;
    int w = 44, top = 9, left = (COLS - w) / 2;
    unsigned k;

    f.buf      = buf;
    f.max      = max;
    f.uppercase = 0;
    if (f.max > w - 4) f.max = w - 4;
    f.len = (int)strlen(buf);
    f.pos = f.len;

    vid_cursor_hide();
    for (;;) {
        dlg_draw_frame(top, left, 5, w);
        vid_puts(top, left + 2, title, A_DLG);
        dlg_field_draw(top + 2, left + 2, w - 4, &f);
        vid_flush();
        vid_cursor(top + 2, left + 2 + f.pos);

        k = kbd_get();
        if (k == K_ESC)  return HDK_CANCEL;
        if (k == K_ENTER) {
            buf[f.len] = '\0';
            return HDK_OK;
        }
        dlg_field_key(&f, k);
    }
}
