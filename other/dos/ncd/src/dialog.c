/* dialog.c -- Simple dialog boxes for DOS NCD
 * Uses video.c primitives for rendering.
 */

#include "dos_ncd.h"
#include <string.h>

/* Dialog result codes */
#define DR_OK       1
#define DR_CANCEL   0
#define DR_YES      2
#define DR_NO       3

/* Draw a dialog frame */
static void dlg_draw_frame(u8 top, u8 left, u8 height, u8 width, const char *title, u8 attr)
{
    u8 bottom;
    u8 right;
    u8 title_len;
    u8 title_pos;

    bottom = top + height - 1;
    right = left + width - 1;

    /* Draw box */
    vid_draw_box(top, left, height, width, attr);

    /* Draw title if provided */
    if (title) {
        title_len = strlen(title);
        title_pos = left + (width - title_len) / 2;
        vid_putat(top, title_pos - 1, attr, ' ');
        vid_puts(top, title_pos, title, attr);
        vid_putat(top, title_pos + title_len, attr, ' ');
    }
}

/* Message box types */
#define MB_OK       0
#define MB_OKCANCEL 1
#define MB_YESNO    2
#define MB_YESNOCAN 3

int dlg_msgbox(const char *title, const char *msg, int type)
{
    u8 msg_len;
    u8 width;
    u8 height;
    u8 left;
    u8 top;
    u8 row;
    u8 col;
    const char *p;
    u8 len;
    u8 btn_row;
    u8 btn_col;
    int result;
    u16 key;

    msg_len = strlen(msg);
    width = msg_len + 4;
    if (width < 20) width = 20;
    if (width > 60) width = 60;

    height = 7;
    left = (COLS - width) / 2;
    top = (ROWS - height) / 2;

    /* Save screen area (simplified - just redraw after) */
    vid_dirty_all();

    /* Draw dialog */
    dlg_draw_frame(top, left, height, width, title, A_DLG);

    /* Draw message (word-wrapped) */
    row = top + 2;
    col = left + 2;
    p = msg;
    while (*p && row < top + height - 2) {
        len = 0;
        while (p[len] && p[len] != '\n' && len < width - 4) len++;
        vid_puts(row, col, p, A_DLG);
        p += len;
        if (*p == '\n') p++;
        row++;
    }

    /* Draw buttons */
    btn_row = top + height - 2;
    btn_col = left + (width - 10) / 2;
    result = DR_CANCEL;

    if (type == MB_OK) {
        vid_puts(btn_row, btn_col, " [  OK  ] ", A_DLG_HI);
    } else if (type == MB_OKCANCEL) {
        vid_puts(btn_row, btn_col, " [  OK  ] ", A_DLG_HI);
        vid_puts(btn_row, btn_col + 12, " [Cancel] ", A_DLG);
    } else if (type == MB_YESNO) {
        vid_puts(btn_row, btn_col, " [  Yes  ] ", A_DLG_HI);
        vid_puts(btn_row, btn_col + 12, " [  No   ] ", A_DLG);
    } else if (type == MB_YESNOCAN) {
        vid_puts(btn_row, btn_col, " [  Yes  ] ", A_DLG_HI);
        vid_puts(btn_row, btn_col + 12, " [  No   ] ", A_DLG);
        vid_puts(btn_row, btn_col + 24, " [Cancel] ", A_DLG);
    }

    vid_flush();

    /* Handle key input */
    while (1) {
        key = kbd_get();
        if (type == MB_OK) {
            if (key == K_ENTER || key == K_ESC || key == ' ' || key == 'y' || key == 'Y') {
                result = DR_OK;
                break;
            }
        } else if (type == MB_OKCANCEL) {
            if (key == K_ENTER || key == 'y' || key == 'Y') {
                result = DR_OK;
                break;
            } else if (key == K_ESC || key == 'n' || key == 'N' || key == 'c' || key == 'C') {
                result = DR_CANCEL;
                break;
            }
        } else if (type == MB_YESNO) {
            if (key == 'y' || key == 'Y') {
                result = DR_YES;
                break;
            } else if (key == 'n' || key == 'N' || key == K_ESC) {
                result = DR_NO;
                break;
            }
        } else if (type == MB_YESNOCAN) {
            if (key == 'y' || key == 'Y') {
                result = DR_YES;
                break;
            } else if (key == 'n' || key == 'N') {
                result = DR_NO;
                break;
            } else if (key == 'c' || key == 'C' || key == K_ESC) {
                result = DR_CANCEL;
                break;
            }
        }
    }

    return result;
}

int dlg_input(const char *title, const char *prompt, char *buf, int maxlen)
{
    u8 width;
    u8 height;
    u8 left;
    u8 top;
    int pos;
    int cursor_visible;
    u16 key;

    width = 50;
    height = 7;
    left = (COLS - width) / 2;
    top = (ROWS - height) / 2;

    vid_dirty_all();
    dlg_draw_frame(top, left, height, width, title, A_DLG);

    vid_puts(top + 2, left + 2, prompt, A_DLG);

    /* Input field */
    vid_fill(top + 3, left + 2, width - 4, ' ', A_NORMAL);
    vid_puts(top + 3, left + 2, buf, A_NORMAL);

    vid_puts(top + 5, left + 2, "[  OK  ]  [Cancel]", A_DLG_HI);
    vid_flush();

    pos = strlen(buf);
    cursor_visible = 1;

    while (1) {
        vid_cursor(top + 3, left + 2 + pos);
        vid_flush();

        key = kbd_get();
        if (key == 0) continue;

        if (key == K_ENTER) {
            return DR_OK;
        } else if (key == K_ESC) {
            return DR_CANCEL;
        } else if (key == K_BS) {
            if (pos > 0) {
                pos--;
                buf[pos] = '\0';
                vid_putat(top + 3, left + 2 + pos, A_NORMAL, ' ');
            }
        } else if (key == K_LEFT) {
            if (pos > 0) pos--;
        } else if (key == K_RIGHT) {
            if (pos < (int)strlen(buf)) pos++;
        } else if (key >= 32 && key < 127 && pos < maxlen - 1) {
            buf[pos++] = (char)key;
            buf[pos] = '\0';
            vid_putat(top + 3, left + 2 + pos - 1, A_NORMAL, (char)key);
        }
    }
}

int dlg_confirm(const char *title, const char *msg)
{
    return dlg_msgbox(title, msg, MB_YESNO) == DR_YES;
}