#ifndef PANEL_H
#define PANEL_H

#include "ncd.h"
#include "fs.h"

/* Single file entry in a pane */
typedef struct {
    char    name[NAME_MAX];
    u8      is_dir;
    u32     size;
    u16     date;
    u16     time;
    u8      selected;
} panel_entry_t;

/* Entries stored in far segment (SYSCALL_ALLOC); 512 x 24 B = 768 paras */
#define PANEL_ENT_PARAS  768

/* File pane state */
typedef struct {
    char            cwd[PATH_MAX];
    u16             ent_seg;   /* far segment for entries[] */
    u16             count;
    u16             sel;       /* selected index */
    u16             scroll;   /* scroll offset */
} panel_t;

/* External panel instances */
extern panel_t panel_left;
extern panel_t panel_right;
extern u8      active_panel;  /* PANEL_LEFT or PANEL_RIGHT */

/* Far entry accessors */
void panel_entry_get(panel_t *p, u16 i, panel_entry_t *out);
void panel_entry_set(panel_t *p, u16 i, const panel_entry_t *in);

/* Panel operations */
void panel_init(panel_t *p);
void panel_refresh(panel_t *p);
void panel_render(panel_t *p, int pane_id);
void panel_up(panel_t *p);
void panel_down(panel_t *p);
void panel_page_up(panel_t *p);
void panel_page_down(panel_t *p);
void panel_home(panel_t *p);
void panel_end(panel_t *p);
void panel_toggle_select(panel_t *p);
int  panel_enter_dir(panel_t *p);
void panel_parent_dir(panel_t *p);
u16  panel_get_visible(panel_t *p);
u16  panel_count_selected(panel_t *p);

/* Get selected entry name */
const char *panel_get_name(panel_t *p);

/* Get current directory of a panel */
const char *panel_get_cwd(panel_t *p);

/* Panel shutdown: free far segments */
void panel_done(panel_t *p);

/* Render functions for UI chrome */
void render_menu_bar(void);
void render_pane_borders(void);
void render_separator(void);
void render_status_line(void);
void render_help_line(void);

#endif /* PANEL_H */
