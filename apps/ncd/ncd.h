/* ncd.h -- Norton Commander Clone for Harvac OS.
 * Common types, screen layout constants, and shared library includes.
 * Key codes, box chars, string helpers, and dialogs come from lib/include/.
 */
#ifndef NCD_H
#define NCD_H

/* Local type aliases (u8/u16/u32) -- compatible with types.h uint*_t */
typedef unsigned char  u8;
typedef unsigned int   u16;
typedef unsigned long  u32;

/* ─── Shared library headers ─── */
#include "types.h"
#include "string.h"
#include "harva.h"
#include "hdk_video.h"    /* ROWS, COLS, vid_* */
#include "hdk_keys.h"     /* K_*, SH_*, kbd_get, kbd_shift */
#include "hdk_box.h"      /* BOX_*, A_NORMAL, A_INVERSE, A_DLG, A_DLG_HI */
#include "hdk_dialog.h"   /* dlg_msgbox, dlg_input, DB_*, DR_*, HDK_* */

/* ─── Screen layout (ROWS=25 / COLS=80 from hdk_video.h) ─── */

/* Row assignments (row 0 = top border of panes) */
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
#define LEFT_PANE_LEFT    0
#define LEFT_PANE_RIGHT   39   /* 40 cols wide */
#define RIGHT_PANE_LEFT   40
#define RIGHT_PANE_RIGHT  79   /* 40 cols wide */

/* Pane internal margins (1-col border on each side) */
#define PANE_INNER_LEFT(p)   ((p) == 0 ? 1 : 41)
#define PANE_INNER_RIGHT(p)  ((p) == 0 ? 38 : 78)
#define PANE_WIDTH            38

/* Column layout within a pane: |NAME 12|SIZE 5|DATE 10|TIME 5| = 37 cols.
 * Offsets are relative to the pane's first interior column. */
#define COL_NAME_WIDTH  12
#define COL_SIZE_WIDTH   5
#define COL_DATE_WIDTH  10
#define COL_TIME_WIDTH   5
#define P_SEP0   0             /* '|' */
#define P_NAME   1
#define P_SEP1  13             /* '|' */
#define P_SIZE  14
#define P_SEP2  19             /* '|' */
#define P_DATE  20
#define P_SEP3  30             /* '|' */
#define P_TIME  31
#define P_SEP4  36             /* '|' */

/* ─── App-specific video attributes ─── */
/* A_NORMAL=0x07, A_INVERSE=0x70, A_DLG=0x70, A_DLG_HI=0x07 from hdk_box.h */
#define A_HIGHLIGHT   0x0F    /* bright white on black */
#define A_MENU_KEY    0x0D    /* F-key number labels: bright red on black */
#define A_STATUS      0x02    /* status bar: green on black */
#define A_STATUS_HI   0x2F    /* status highlight: bright white on green */
#define A_PATH        0x07    /* directory path header: normal */
#define A_HEADER      0x0E    /* column headers: yellow on black */
#define A_DIR         0x09    /* directory name: bright blue on black */
#define A_FILE        0x07    /* regular file: normal */
#define A_SELECTED    0x70    /* cursor-selected row: inverse */
#define A_MARKER      0x4F    /* Ins-selected marker: bright white on red */
#define A_SHELL       0x07    /* shell prompt line: normal */
#define A_SEPARATOR   0x07    /* separator: normal */
#define A_VIEWER      0x07    /* viewer content: normal */
#define A_VIEWER_HI   0x70    /* viewer status bar: inverse */

/* ─── FAT16 directory attribute bits ─── */
/* Also available as FAT_ATTR_* in harva.h */
#define DIR_ATTR_READONLY   0x01
#define DIR_ATTR_HIDDEN     0x02
#define DIR_ATTR_SYSTEM     0x04
#define DIR_ATTR_VOLUME     0x08
#define DIR_ATTR_DIRECTORY  0x10
#define DIR_ATTR_ARCHIVE    0x20

/* ─── Buffer / limit constants ─── */
#define NAME_MAX          13   /* 8.3 name + null */
#define PATH_MAX          64   /* max path string */
#define MAX_ENTRIES      512   /* max entries per panel (FAT16 root = 512) */
#define VIEWER_BUF_SIZE  4096  /* viewer far-buffer size */
#define SHELL_BUF_SIZE   128   /* shell command line buffer */
#define DIALOG_BUF_SIZE   64   /* generic dialog input buffer */

/* ─── Panel and mode IDs ─── */
#define PANEL_LEFT    0
#define PANEL_RIGHT   1

#define MODE_BROWSER  0
#define MODE_VIEWER   1
#define MODE_SHELL    2
#define MODE_DIALOG   3

/* Dialog result codes DR_CANCEL/DR_OK/DR_YES/DR_NO are in hdk_dialog.h */

#endif /* NCD_H */
