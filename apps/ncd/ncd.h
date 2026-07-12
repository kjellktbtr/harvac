/* ncd.h -- Norton Commander Clone for Harvac OS
 * Common types, screen layout, key codes, and string aliases.
 */
#ifndef NCD_H
#define NCD_H

typedef unsigned char  u8;
typedef unsigned int   u16;
typedef unsigned long  u32;

/* Screen layout (VGA 80x25 text mode) */
#define ROWS        25
#define COLS        80

/* Row assignments (0 = top border of panes) */
#define ROW_PATH        1
#define ROW_HEADER      2
#define ROW_FILE_FIRST  3
#define ROW_FILE_LAST   19     /* 17 rows of file entries */
#define ROW_BORDER      20     /* bottom border of panes */
#define ROW_STATUS      21
#define ROW_MSG         22     /* transient messages ("2 copied") */
#define ROW_SHELL       23     /* command line */
#define ROW_FKEYS       24     /* inverted F-key bar */

/* Pane column boundaries */
#define LEFT_PANE_LEFT  0
#define LEFT_PANE_RIGHT 39     /* 40 cols wide */
#define RIGHT_PANE_LEFT 40
#define RIGHT_PANE_RIGHT 79   /* 40 cols wide */

/* Pane internal margins (1 col border on each side) */
#define PANE_INNER_LEFT(p)    ((p) == 0 ? 1 : 41)
#define PANE_INNER_RIGHT(p)   ((p) == 0 ? 38 : 78)
#define PANE_WIDTH            38

/* Column layout within a pane: |NAME 12|SIZE 5|DATE 10|TIME 5| = 37 cols.
 * Offsets are relative to the pane's first interior column. */
#define COL_NAME_WIDTH  12
#define COL_SIZE_WIDTH  5
#define COL_DATE_WIDTH  10
#define COL_TIME_WIDTH  5
#define P_SEP0   0             /* '|' */
#define P_NAME   1
#define P_SEP1   13            /* '|' */
#define P_SIZE   14
#define P_SEP2   19            /* '|' */
#define P_DATE   20
#define P_SEP3   30            /* '|' */
#define P_TIME   31
#define P_SEP4   36            /* '|' */

/* Attributes */
#define A_NORMAL      0x07    /* white on black */
#define A_INVERSE     0x70    /* black on white */
#define A_HIGHLIGHT   0x0F    /* white on bright white */
#define A_MENU        0x07    /* menu bar: normal */
#define A_MENU_KEY    0x0D    /* F-key labels: red on black */
#define A_STATUS      0x02    /* status line: green on blue */
#define A_STATUS_HI   0x2F    /* status highlight: bright green on bright blue */
#define A_PATH        0x07    /* directory path header */
#define A_HEADER      0x0E    /* column headers: yellow on black */
#define A_DIR         0x09    /* directory entries: blue on black */
#define A_FILE        0x07    /* file entries: normal */
#define A_SELECTED    0x70    /* selected entry: inverse */
#define A_MARKER      0x4F    /* selection marker: bright white on red */
#define A_SHELL       0x07    /* shell prompt */
#define A_DLG         0x70    /* dialog body: inverse */
#define A_DLG_HI      0x07    /* dialog button highlight: normal */
#define A_SEPARATOR   0x07    /* separator line */
#define A_VIEWER      0x07    /* viewer text */
#define A_VIEWER_HI   0x70    /* viewer highlight */

/* cp437 box-drawing characters */
#define BOX_TL  0xDA    /* top-left corner */
#define BOX_TR  0xBF    /* top-right corner */
#define BOX_BL  0xC0    /* bottom-left corner */
#define BOX_BR  0xD9    /* bottom-right corner */
#define BOX_H   0xC4    /* horizontal line */
#define BOX_V   0xB3    /* vertical line */
#define BOX_TJ  0xBB    /* T junction (top) */
#define BOX_BJ  0xB9    /* T junction (bottom) */
#define BOX_LJ  0xBC    /* T junction (left) */
#define BOX_RJ  0xBD    /* T junction (right) */
#define BOX_CJ  0xB8    /* cross junction */

/* Key codes (normalized from kbd_get()) */
#define K_SPECIAL   0x100
#define K_UP        (K_SPECIAL|0x48)
#define K_DOWN      (K_SPECIAL|0x50)
#define K_LEFT      (K_SPECIAL|0x4B)
#define K_RIGHT     (K_SPECIAL|0x4D)
#define K_HOME      (K_SPECIAL|0x47)
#define K_END       (K_SPECIAL|0x4F)
#define K_PGUP      (K_SPECIAL|0x49)
#define K_PGDN      (K_SPECIAL|0x51)
#define K_INS       (K_SPECIAL|0x52)
#define K_DEL       (K_SPECIAL|0x53)
#define K_ESC       (K_SPECIAL|0x01)
#define K_F3        (K_SPECIAL|0x3D)
#define K_F4        (K_SPECIAL|0x3E)
#define K_F5        (K_SPECIAL|0x3F)
#define K_F6        (K_SPECIAL|0x40)
#define K_F7        (K_SPECIAL|0x41)
#define K_F8        (K_SPECIAL|0x42)
#define K_F10       (K_SPECIAL|0x44)
#define K_ENTER     0x0D
#define K_BS        0x08
#define K_TAB       0x09

/* Shift flags (from kbd_shift after kbd_get) */
#define SH_SHIFT    0x01
#define SH_CTRL     0x02
#define SH_ALT      0x04

/* Directory attributes (from FAT16 dirent) */
#define DIR_ATTR_READONLY   0x01
#define DIR_ATTR_HIDDEN     0x02
#define DIR_ATTR_SYSTEM     0x04
#define DIR_ATTR_VOLUME     0x08
#define DIR_ATTR_DIRECTORY  0x10
#define DIR_ATTR_ARCHIVE    0x20

/* Max file/directory name length (8.3 = 12 chars + null) */
#define NAME_MAX    13
/* Max path length */
#define PATH_MAX    64
/* Max entries per directory (FAT16 root = 512) */
#define MAX_ENTRIES 512
/* Viewer buffer size */
#define VIEWER_BUF_SIZE  4096
/* Shell command buffer */
#define SHELL_BUF_SIZE  128
/* Dialog input buffer */
#define DIALOG_BUF_SIZE 64

/* Panel IDs */
#define PANEL_LEFT    0
#define PANEL_RIGHT   1

/* App modes */
#define MODE_BROWSER  0
#define MODE_VIEWER   1
#define MODE_SHELL    2
#define MODE_DIALOG   3

/* Dialog result codes */
#define DR_CANCEL   0
#define DR_OK       1
#define DR_YES      1
#define DR_NO       2

/* String function aliases (map to m_ prefixed implementations) */
#include "str.h"
#define strlen  m_strlen
#define strcmp  m_strcmp
#define strcpy  m_strcpy
#define strcat  m_strcat
#define memcpy  m_memcpy
#define memmove m_memmove
#define memset  m_memset

#endif
