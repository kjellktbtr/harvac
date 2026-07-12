/* hdk_box.h -- cp437 box-drawing characters and shared color/attribute macros
 * for HarvaC HDK dialogs and panel borders. */
#ifndef HDK_BOX_H
#define HDK_BOX_H

/* ─── cp437 single-line box characters ─── */
#define BOX_TL  0xDA    /* ┌ top-left corner    */
#define BOX_TR  0xBF    /* ┐ top-right corner   */
#define BOX_BL  0xC0    /* └ bottom-left corner */
#define BOX_BR  0xD9    /* ┘ bottom-right corner*/
#define BOX_H   0xC4    /* ─ horizontal line    */
#define BOX_V   0xB3    /* │ vertical line      */

/* ─── cp437 double-line box characters ─── */
#define BOX2_TL 0xC9    /* ╔ */
#define BOX2_TR 0xBB    /* ╗ */
#define BOX2_BL 0xC8    /* ╚ */
#define BOX2_BR 0xBC    /* ╝ */
#define BOX2_H  0xCD    /* ═ */
#define BOX2_V  0xBA    /* ║ */

/* ─── T-junction connectors ─── */
#define BOX_TJ  0xC2    /* ┬ top T    */
#define BOX_BJ  0xC1    /* ┴ bottom T */
#define BOX_LJ  0xC3    /* ├ left T   */
#define BOX_RJ  0xB4    /* ┤ right T  */
#define BOX_CJ  0xC5    /* ┼ cross    */

/* ─── Common video attributes ─── */
#define A_NORMAL    0x07    /* grey on black (default text)     */
#define A_BOLD      0x0F    /* bright white on black            */
#define A_INVERSE   0x70    /* black on grey (status/menu bars) */
#define A_DLG       0x70    /* dialog body: inverse             */
#define A_DLG_HI    0x07    /* selected widget: normal (double-inverse) */
#define A_DLG_TITLE 0x70    /* dialog title row: same as body   */

/* ─── Fkey bar attributes ─── */
#define A_FKEY_NUM  0x70    /* Fn number label: inverse         */
#define A_FKEY_LBL  0x07    /* Fn description: normal           */

#endif /* HDK_BOX_H */
