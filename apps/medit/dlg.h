#ifndef DLG_H
#define DLG_H
#include "medit.h"

/* Modal message box. buttons = DB_OK, DB_JNA or DB_JNAA.
 * Returns: DR_JA (also OK), DR_NEI, DR_ALLE, DR_AVBRYT (also Esc). */
#define DB_OK   1
#define DB_JNA  2
#define DB_JNAA 3

#define DR_AVBRYT 0
#define DR_JA     1
#define DR_NEI    2
#define DR_ALLE   3

int dlg_msgbox(const char *text, int buttons);

/* Single-line input box. Returns 1 = OK (buf filled), 0 = cancelled.
 * buf must hold max+1 bytes and may be pre-filled. */
int dlg_input(const char *title, char *buf, int max);

/* File open / save-as browser: navigate directories and drives,
 * pick or type a filename. Returns 1 with the name in out (68 bytes),
 * 0 = cancelled. save=1 adds an overwrite confirmation. */
int dlg_filebox(char *out, int save);

#endif
