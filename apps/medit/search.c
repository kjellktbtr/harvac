/* Finn / Finn neste / Erstatt. Case-insensitive matching including
 * the cp865 Norwegian letter pairs. Linear scan over the gap buffer;
 * fine at 60 kB even on a 4.77 MHz 8088.
 */
#include "medit.h"
#include "gap.h"
#include "edit.h"
#include "menu.h"
#include "dlg.h"
#include "search.h"

#define NOTF 0xFFFFU

static char s_find[40];
static char s_repl[40];
static char s_msg[24];

static u8 up_no(u8 c)
{
    if (c >= 'a' && c <= 'z')
        return (u8)(c - 32);
    if (c == 0x86) return 0x8F;         /* aa */
    if (c == 0x91) return 0x92;         /* ae */
    if (c == 0x9B) return 0x9D;         /* oe */
    return c;
}

static u16 search_next(u16 from)
{
    u16 len = gb_len();
    u16 plen = (u16)strlen(s_find);
    u16 pos, i;

    if (plen == 0 || len < plen)
        return NOTF;
    for (pos = from; pos + plen <= len; pos++) {
        for (i = 0; i < plen; i++)
            if (up_no(gb_at(pos + i)) != up_no((u8)s_find[i]))
                break;
        if (i == plen)
            return pos;
    }
    return NOTF;
}

/* find from cursor, wrap once; select the hit */
static void find_and_select(void)
{
    u16 plen = (u16)strlen(s_find);
    u16 pos = search_next(e_cur);

    if (pos == NOTF && e_cur > 0) {
        pos = search_next(0);
        if (pos != NOTF)
            edit_set_msg("S" S_oe "kte fra toppen");
    }
    if (pos == NOTF) {
        edit_set_msg("Ikke funnet");
        return;
    }
    edit_select(pos, pos + plen);
}

void search_finn(void)
{
    if (!dlg_input("Finn:", s_find, 39))
        return;
    if (s_find[0] == '\0')
        return;
    find_and_select();
}

void search_finn_neste(void)
{
    if (s_find[0] == '\0') {
        search_finn();
        return;
    }
    find_and_select();
}

void search_erstatt(void)
{
    u16 plen, rlen, pos, count = 0;
    int all = 0, r, i, n;

    if (!dlg_input("Erstatt:", s_find, 39))
        return;
    if (s_find[0] == '\0')
        return;
    if (!dlg_input("Med:", s_repl, 39))
        return;
    plen = (u16)strlen(s_find);
    rlen = (u16)strlen(s_repl);

    for (;;) {
        pos = search_next(e_cur);
        if (pos == NOTF)
            break;
        edit_select(pos, pos + plen);
        if (!all) {
            edit_render();
            menu_draw_bar(-1);
            r = dlg_msgbox("Erstatte?", DB_JNAA);
            if (r == DR_AVBRYT)
                break;
            if (r == DR_NEI) {
                edit_select(pos + plen, pos + plen);
                continue;
            }
            if (r == DR_ALLE)
                all = 1;
        }
        edit_insert_str(s_repl, rlen);
        count++;
    }

    n = 0;
    if (count == 0) {
        strcpy(s_msg, "Ikke funnet");
    } else {
        char tmp[6];
        i = 0;
        pos = count;
        do {
            tmp[i++] = (char)('0' + pos % 10);
            pos /= 10;
        } while (pos);
        while (i)
            s_msg[n++] = tmp[--i];
        strcpy(s_msg + n, " erstattet");
    }
    edit_set_msg(s_msg);
}
