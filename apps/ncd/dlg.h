#ifndef DLG_H
#define DLG_H

#include "ncd.h"

/* Modal message box. buttons = DB_OK or DB_JNA.
 * Returns DR_OK (also DR_YES), DR_NO, DR_CANCEL (also Esc). */
#define DB_OK   1
#define DB_JNA  2

int dlg_msgbox(const char *text, int buttons);

/* Single-line input box. Returns DR_OK (buf filled), DR_CANCEL (cancelled).
 * buf must hold max+1 bytes and may be pre-filled. */
int dlg_input(const char *title, char *buf, int max);

#endif /* DLG_H */
