/* hdk_dialog.h -- Modal dialogs for HarvaC HDK.
 * Box-drawing, button rendering, text-input field, and the dlg_msgbox /
 * dlg_input high-level dialogs.
 *
 * Localization: set hdk_lang before calling dialogs.
 *   HDK_LANG_EN (0) — English buttons: "OK", "Yes", "No", "Cancel", "All"
 *   HDK_LANG_NO (1) — Norwegian buttons: "OK", "Ja", "Nei", "Avbryt", "Alle"
 * Default is English. */
#ifndef HDK_DIALOG_H
#define HDK_DIALOG_H

#include "types.h"

/* Language selection for button labels */
#define HDK_LANG_EN  0   /* English (default) */
#define HDK_LANG_NO  1   /* Norwegian          */

/* Set to HDK_LANG_EN or HDK_LANG_NO at application startup */
extern int hdk_lang;

/* ─── Button set codes (passed to dlg_msgbox) ─── */
#define DB_OK       0   /* single OK button  */
#define DB_YESNO    1   /* Yes + No          */
#define DB_YESNOCA  2   /* Yes + No + Cancel */
#define DB_YESNOALL 3   /* Yes + No + All + Cancel (for "replace all") */

/* Aliases for medit's Norwegian names (backward compat) */
#define DB_JNA   DB_YESNOCA
#define DB_JNAA  DB_YESNOALL

/* ─── Dialog result codes ─── */
#define HDK_CANCEL  0
#define HDK_OK      1
#define HDK_YES     1
#define HDK_NO      2
#define HDK_ALL     3

/* Aliases for medit's Norwegian result names (backward compat) */
#define DR_AVBRYT  HDK_CANCEL
#define DR_JA      HDK_YES
#define DR_NEI     HDK_NO
#define DR_ALLE    HDK_ALL

/* Aliases for ncd's English result names (backward compat) */
#define DR_CANCEL  HDK_CANCEL
#define DR_OK      HDK_OK
#define DR_YES     HDK_YES
#define DR_NO      HDK_NO

/* ─── Low-level drawing primitives ─── */
/* Draw a single-line box frame at (top, left) of size h x w. */
void dlg_draw_frame(int top, int left, int h, int w);

/* Draw a <Label> button at (row, col); selected=1 highlights it. */
void dlg_draw_button(int row, int col, const char *label, int selected);

/* ─── Text input field ─── */
typedef struct {
    char *buf;      /* edit buffer (caller-allocated)             */
    int   max;      /* max characters (buf must hold max+1 bytes) */
    int   len;      /* current length (strlen(buf) at init)       */
    int   pos;      /* cursor position (0..len)                   */
    int   uppercase; /* 1 = force uppercase on character insert   */
} HdkField;

/* Render field contents at (row, col) over width w. */
void dlg_field_draw(int row, int col, int w, HdkField *f);

/* Feed a key to the field. Returns 1 if consumed, 0 if the caller should
 * handle it (e.g. K_ENTER / K_ESC / K_TAB). */
int  dlg_field_key(HdkField *f, unsigned k);

/* ─── High-level dialogs ─── */

/* Show a message box with the given button set.
 * Returns HDK_OK / HDK_YES / HDK_NO / HDK_ALL / HDK_CANCEL. */
int dlg_msgbox(const char *text, int buttons);

/* Show a single-line text input dialog.
 * buf should be pre-filled (may be ""); max = maximum chars to accept.
 * Returns HDK_OK or HDK_CANCEL. On HDK_OK, buf is NUL-terminated. */
int dlg_input(const char *title, char *buf, int max);

#endif /* HDK_DIALOG_H */
