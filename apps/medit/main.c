/* main.c -- MEDIT entry point for Harvac.
 * _main MUST be the first function so it lands at CS:0x100 (COM entry).
 * Args read from PSP 0x0082 (written by SYSCALL_EXEC). */

#include "medit.h"
#include "types.h"
#include "stdio.h"
#include "gap.h"
#include "doc.h"
#include "edit.h"
#include "menu.h"
#include "dlg.h"
#include "search.h"

/* Forward declarations for helpers defined below _main */
static void say(const char *s);
static void set_name(const char *path);
static int  do_save_as(void);
static int  do_save(void);
static void do_open(void);
static int  confirm_discard(void);
static void do_new(void);
static int  do_action(int act);

/* Entry point: must be FIRST function in this file. */
void __far _main(void)
{
    const char *tail = (const char *)0x0082;
    const char *file = 0;
    unsigned k;

    /* Parse command tail: skip spaces; treat first token as filename.
     * /F and -F are silently accepted (no CGA snow on Harvac). */
    while (*tail == ' ') tail++;
    if (*tail != '\0') {
        if ((*tail == '/' || *tail == '-')
                && (tail[1] == 'F' || tail[1] == 'f')) {
            tail += 2;
            while (*tail == ' ') tail++;
        }
        if (*tail != '\0')
            file = tail;
    }

    if (gb_init() != 0) {
        say("MEDIT: ikke nok minne.\r\n");
        return;
    }

    edit_init();
    if (file) {
        switch (doc_load(file)) {
        case DOC_OK:
            set_name(file);
            break;
        case DOC_ERR_OPEN:
            set_name(file);
            edit_set_msg("Ny fil");
            break;
        case DOC_ERR_BIG:
            say("MEDIT: filen er for stor (maks 47000 tegn).\r\n");
            return;
        default:
            say("MEDIT: lesefeil.\r\n");
            return;
        }
    }

    vid_init();

    for (;;) {
        edit_render();
        menu_draw_bar(-1);
        vid_flush();
        edit_place_cursor();

        k = kbd_get();
        if (k == K_ALT_F || k == K_ALT_R || k == K_ALT_S) {
            int m = (k == K_ALT_F) ? M_FIL
                  : (k == K_ALT_R) ? M_REDIGER : M_SOK;
            if (do_action(menu_open(m)))
                break;
            continue;
        }
        if (k == K_F3) {
            search_finn_neste();
            continue;
        }
        if (k == 0x13) {    /* Ctrl+S */
            do_save();
            continue;
        }
        if (k == 0x11) {    /* Ctrl+Q */
            if (confirm_discard())
                break;
            continue;
        }
        edit_handle(k);
    }

    vid_done();
}

static void say(const char *s)
{
    fputs(s);
}

static void set_name(const char *path)
{
    unsigned i;
    for (i = 0; path[i] && i < sizeof(e_fname) - 1; i++)
        e_fname[i] = (char)(path[i] >= 'a' && path[i] <= 'z'
                            ? path[i] - 32 : path[i]);
    e_fname[i] = '\0';
    e_have_name = 1;
}

static int do_save_as(void)
{
    char path[68];
    if (!dlg_filebox(path, 1))
        return 0;
    if (doc_save(path) != DOC_OK) {
        dlg_msgbox("Feil ved lagring!", DB_OK);
        return 0;
    }
    set_name(path);
    e_modified = 0;
    edit_set_msg("Lagret");
    return 1;
}

static int do_save(void)
{
    if (!e_have_name)
        return do_save_as();
    if (doc_save(e_fname) != DOC_OK) {
        dlg_msgbox("Feil ved lagring!", DB_OK);
        return 0;
    }
    e_modified = 0;
    edit_set_msg("Lagret");
    return 1;
}

static void do_open(void)
{
    char path[68];
    if (!confirm_discard())
        return;
    if (!dlg_filebox(path, 0))
        return;
    switch (doc_load(path)) {
    case DOC_OK:
        edit_init();
        set_name(path);
        break;
    case DOC_ERR_OPEN:
        gb_reset();
        edit_init();
        set_name(path);
        edit_set_msg("Ny fil");
        break;
    case DOC_ERR_BIG:
        gb_reset();
        edit_init();
        dlg_msgbox("Filen er for stor (maks 47000 tegn)", DB_OK);
        break;
    default:
        gb_reset();
        edit_init();
        dlg_msgbox("Lesefeil", DB_OK);
        break;
    }
}

static int confirm_discard(void)
{
    switch (e_modified ? dlg_msgbox("Lagre endringer?", DB_JNA) : DR_NEI) {
    case DR_JA:
        return do_save();
    case DR_NEI:
        return 1;
    default:
        return 0;
    }
}

static void do_new(void)
{
    if (!confirm_discard())
        return;
    gb_reset();
    edit_init();
}

static int do_action(int act)
{
    edit_render();
    menu_draw_bar(-1);
    switch (act) {
    case MI_NY:       do_new();             break;
    case MI_APNE:     do_open();            break;
    case MI_LAGRE:    do_save();            break;
    case MI_LAGRESOM: do_save_as();         break;
    case MI_AVSLUTT:
        if (confirm_discard()) return 1;
        break;
    case MI_KLIPP:    edit_cut();           break;
    case MI_KOPIER:   edit_copy();          break;
    case MI_LIM:      edit_paste();         break;
    case MI_BRYT:     menu_wrap_flag ^= 1;  break;
    case MI_FINN:     search_finn();        break;
    case MI_FINNNESTE:search_finn_neste();  break;
    case MI_ERSTATT:  search_erstatt();     break;
    default:                                break;
    }
    return 0;
}
