#ifndef DLG_H
#define DLG_H
/* dlg_msgbox, dlg_input, DB_*, DR_* are in hdk_dialog.h (via medit.h). */

/* File open / save-as browser: navigate directories, pick or type a filename.
 * Returns 1 with the chosen name in out (68 bytes), 0 = cancelled.
 * save=1 adds an overwrite confirmation. */
int dlg_filebox(char *out, int save);

#endif /* DLG_H */
