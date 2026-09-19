/* dos_ncd.h -- Common types and constants for DOS NCD port */

#ifndef DOS_NCD_H
#define DOS_NCD_H

#include <stdint.h>
#include <stddef.h>

/* Type aliases matching Harvac's u8/u16/u32 */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;

/* Screen dimensions */
#define COLS 80
#define ROWS 25

/* Key codes (matching Harvac's hdk_keys.h) */
#define K_UP      0x0100
#define K_DOWN    0x0101
#define K_LEFT    0x0102
#define K_RIGHT   0x0103
#define K_PGUP    0x0104
#define K_PGDN    0x0105
#define K_HOME    0x0106
#define K_END     0x0107
#define K_INS     0x0108
#define K_DEL     0x0109
#define K_F1      0x0110
#define K_F2      0x0111
#define K_F3      0x0112
#define K_F4      0x0113
#define K_F5      0x0114
#define K_F6      0x0115
#define K_F7      0x0116
#define K_F8      0x0117
#define K_F9      0x0118
#define K_F10     0x0119
#define K_BS      0x0008
#define K_TAB     0x0009
#define K_ENTER   0x000D
#define K_ESC     0x001B
#define K_SPACE   0x0020

/* Shift state flags */
#define SH_SHIFT  0x01
#define SH_CTRL   0x02
#define SH_ALT    0x04

extern u8 kbd_shift;

/* Video attributes (VGA text mode: foreground | (background << 4)) */
#define A_NORMAL     0x07  /* White on black */
#define A_INVERSE    0x70  /* Black on white */
#define A_BOLD       0x0F  /* Bright white on black */
#define A_DLG        0x70  /* Dialog: inverse */
#define A_DLG_HI     0x07  /* Dialog highlight: normal */
#define A_HIGHLIGHT  0x0F  /* Bright white on black */
#define A_MENU_KEY   0x0C  /* Red on black */
#define A_STATUS     0x02  /* Green on black */
#define A_STATUS_HI  0x2F  /* White on green */
#define A_PATH       0x07  /* Normal */
#define A_HEADER     0x0E  /* Yellow on black */
#define A_DIR        0x09  /* Bright blue on black */
#define A_FILE       0x07  /* Normal */
#define A_SELECTED   0x70  /* Inverse */
#define A_MARKER     0x4F  /* White on red */
#define A_SHELL      0x07  /* Normal */
#define A_SEPARATOR  0x07  /* Normal */
#define A_VIEWER     0x07  /* Normal */
#define A_VIEWER_HI  0x70  /* Inverse */

/* CP437 box drawing characters */
#define BOX_TL      0xDA  /* ┌ */
#define BOX_TR      0xBF  /* ┐ */
#define BOX_BL      0xC0  /* └ */
#define BOX_BR      0xD9  /* ┘ */
#define BOX_H       0xC4  /* ─ */
#define BOX_V       0xB3  /* │ */
#define BOX_TDOWN   0xC2  /* ┬ */
#define BOX_TUP     0xC1  /* ┴ */
#define BOX_TRIGHT  0xC3  /* ├ */
#define BOX_TLEFT   0xB4  /* ┤ */
#define BOX_CROSS   0xC5  /* ┼ */

/* DOS file attributes */
#define DIR_ATTR_READONLY   0x01
#define DIR_ATTR_HIDDEN     0x02
#define DIR_ATTR_SYSTEM     0x04
#define DIR_ATTR_VOLUME     0x08
#define DIR_ATTR_DIRECTORY  0x10
#define DIR_ATTR_ARCHIVE    0x20

/* Screen layout (matching NCD) */
#define ROW_PATH        1
#define ROW_HEADER      2
#define ROW_FILE_FIRST  3
#define ROW_FILE_LAST   19
#define ROW_BORDER      20
#define ROW_STATUS      21
#define ROW_MSG         22
#define ROW_SHELL       23
#define ROW_FKEYS       24

#define LEFT_PANE_LEFT    0
#define LEFT_PANE_RIGHT   39
#define RIGHT_PANE_LEFT   40
#define RIGHT_PANE_RIGHT  79
#define PANE_WIDTH        38

/* Panel entry columns */
#define COL_NAME_WIDTH   12
#define COL_SIZE_WIDTH    5
#define COL_DATE_WIDTH   10
#define COL_TIME_WIDTH    5

/* Panel entry structure */
typedef struct {
    char name[13];     /* 8.3 + null */
    u16 size;
    u16 date;
    u16 time;
    u8  attr;
    u8  is_dir;
    u8  selected;
} panel_entry_t;

/* Panel structure */
#define MAX_ENTRIES 512
#define DOS_PATH_MAX    64
#define SHELL_BUF_SIZE  128   /* shell command line buffer */
#define VIEWER_BUF_SIZE 4096  /* viewer buffer size */

typedef struct {
    panel_entry_t entries[MAX_ENTRIES];
    u16 count;
    u16 sel;
    u16 scroll;
    char cwd[DOS_PATH_MAX];
} panel_t;

/* Global panel state */
extern panel_t panel_left;
extern panel_t panel_right;
extern u8 active_panel;

/* Viewer state */
extern u8 viewer_mode;

#define PANEL_LEFT  0
#define PANEL_RIGHT 1

/* Function declarations */
u16 kbd_get(void);
void kbd_flush(void);

void vid_init(void);
void vid_done(void);
void vid_putat(u8 row, u8 col, char ch, u8 attr);
void vid_puts(u8 row, u8 col, const char *s, u8 attr);
void vid_fill(u8 row, u8 col, u8 len, char ch, u8 attr);
void vid_flush(void);
void vid_cursor_hide(void);
void vid_cursor_show(void);
void vid_dirty_all(void);

void *mem_alloc(size_t size);
void mem_free(void *ptr);

int fs_read_dir(const char *path, panel_t *panel);
int fs_file_exists(const char *path);
int fs_is_dir(const char *path);
u32 fs_file_size(const char *path);
int fs_copy_file(const char *src, const char *dst);
int fs_move_file(const char *src, const char *dst);
int fs_delete_file(const char *path);
int fs_make_dir(const char *path);
int fs_remove_dir(const char *path);
int fs_rename(const char *src, const char *dst);

int dlg_msgbox(const char *title, const char *msg, int type);
int dlg_input(const char *title, const char *prompt, char *buf, int maxlen);
int dlg_confirm(const char *title, const char *msg);

void panel_init(panel_t *p);
void panel_done(panel_t *p);
void panel_refresh(panel_t *p);
void panel_render(const panel_t *p, int pane);
void panel_sort(panel_t *p);
const char *panel_get_name(panel_t *p);
const char *panel_get_cwd(panel_t *p);
u16 panel_get_visible(panel_t *p);
u16 panel_count_selected(panel_t *p);
void panel_up(panel_t *p);
void panel_down(panel_t *p);
void panel_page_up(panel_t *p);
void panel_page_down(panel_t *p);
void panel_home(panel_t *p);
void panel_end(panel_t *p);
void panel_toggle_select(panel_t *p);
int panel_enter_dir(panel_t *p);
void panel_parent_dir(panel_t *p);

void viewer_open(const char *path);
void viewer_close(void);
void viewer_render(void);
int viewer_handle_key(u16 key);

int shell_exec(const char *cmd, const char *args);

#endif /* DOS_NCD_H */

