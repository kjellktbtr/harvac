#ifndef EDIT_H
#define EDIT_H
#include "medit.h"

extern u16  e_cur;          /* cursor byte offset */
extern u16  e_top;          /* offset of first visible line start */
extern u16  e_topline;      /* 1-based line number of e_top */
extern u8   e_modified;
extern u8   e_have_name;
extern char e_fname[68];

void edit_init(void);
void edit_cut(void);
void edit_copy(void);
void edit_paste(void);
void edit_select(u16 lo, u16 hi);   /* select range, cursor at hi */
void edit_insert_str(const char *s, u16 n);  /* replaces selection */
void edit_render(void);         /* text rows + status line into shadow */
void edit_place_cursor(void);   /* hardware cursor at the edit point */
int  edit_handle(unsigned k);   /* 1 if the key was consumed */
void edit_set_msg(const char *m);  /* one-shot status-line message */

#endif
