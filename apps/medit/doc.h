#ifndef DOC_H
#define DOC_H
#include "medit.h"

/* Line-boundary helpers over the gap buffer. Lines end with CRLF,
 * lone LF or lone CR; the terminator belongs to the line. No global
 * line index is kept (low-memory design) - callers scan locally from
 * known anchors (top-of-screen, cursor). */
u16 doc_line_home(u16 pos);     /* start of line containing pos */
u16 doc_line_end(u16 pos);      /* offset of terminator (or len) */
u16 doc_next_line(u16 endpos);  /* start of following line */
u16 doc_prev_line(u16 start);   /* start of preceding line */

#define DOC_OK       0
#define DOC_ERR_OPEN 1
#define DOC_ERR_BIG  2
#define DOC_ERR_IO   3

int doc_load(const char *path);
int doc_save(const char *path);

#endif
