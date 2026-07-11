#ifndef MENU_H
#define MENU_H
#include "medit.h"

void menu_draw_bar(int active);     /* active menu index or -1 */
int  menu_open(int m);              /* returns MI_* action or 0 */

/* menu indexes */
#define M_FIL     0
#define M_REDIGER 1
#define M_SOK     2

extern u8 menu_wrap_flag;           /* mirrored word-wrap state for checkmark */

#endif
