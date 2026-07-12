/* dlg.c -- File browser dialog for MEDIT.
 * dlg_msgbox and dlg_input are in lib/hdk/dlg.c (via medit.h → hdk_dialog.h).
 * Only the app-specific file-open/save-as browser (dlg_filebox) lives here. */

#include "medit.h"
#include "types.h"
#include "dirent.h"
#include "unistd.h"
#include "fcntl.h"
#include "dlg.h"

/* ─── File browser internals ─── */

#define FB_MAX   120
#define FK_FILE  0
#define FK_DIR   1

static char fb_name[FB_MAX][13];
static u8   fb_kind[FB_MAX];
static int  fb_n;

static void fb_add(const char *name, u8 kind)
{
    if (fb_n >= FB_MAX)
        return;
    strcpy(fb_name[fb_n], name);
    fb_kind[fb_n] = kind;
    fb_n++;
}

static int fb_before(int a, int b)
{
    if (fb_kind[a] != fb_kind[b])
        return fb_kind[a] == FK_DIR;
    return strcmp(fb_name[a], fb_name[b]) < 0;
}

static void fb_sort(void)
{
    int i, j;
    char tn[13];
    u8 tk;
    for (i = 1; i < fb_n; i++) {
        for (j = i; j > 0 && fb_before(j, j - 1); j--) {
            memcpy(tn, fb_name[j], 13);
            memcpy(fb_name[j], fb_name[j - 1], 13);
            memcpy(fb_name[j - 1], tn, 13);
            tk = fb_kind[j];
            fb_kind[j] = fb_kind[j - 1];
            fb_kind[j - 1] = tk;
        }
    }
}

static void fb_read_dir(void)
{
    DIR          *dp;
    struct dirent *de;
    char          cwd[64];

    fb_n = 0;

    getcwd(cwd, (u16)sizeof(cwd));
    if (cwd[0] != '/' || cwd[1] != '\0')
        fb_add("..", FK_DIR);

    dp = opendir(".");
    if (!dp) return;
    while ((de = readdir(dp)) != (struct dirent *)0) {
        if (de->d_name[0] == '.' && de->d_name[1] == '.' && de->d_name[2] == '\0')
            continue;
        fb_add(de->d_name, (de->d_attr & FAT_ATTR_DIRECTORY) ? FK_DIR : FK_FILE);
    }
    closedir(dp);

    fb_sort();
}

static void fb_entry_text(int i, char *out)   /* out: 14+ bytes */
{
    if (fb_kind[i] == FK_DIR) {
        out[0] = '[';
        strcpy(out + 1, fb_name[i]);
        strcat(out, "]");
    } else {
        strcpy(out, fb_name[i]);
    }
}

/* ─── Public: file open / save-as browser ─── */

int dlg_filebox(char *out, int save)
{
    static char namebuf[66];
    HdkField f;
    char cwd[64], txt[16];
    int top = 2, w = 44, left = (COLS - w) / 2;
    int lrows = 13, lrow0 = top + 4;
    int sel = 0, off = 0, focus_list = 1;
    int i, r, th;
    unsigned k;

    namebuf[0] = '\0';
    f.buf       = namebuf;
    f.max       = 65;
    f.uppercase = 1;
    f.len = f.pos = 0;

    fb_read_dir();

    for (;;) {
        if (sel >= fb_n) sel = fb_n ? fb_n - 1 : 0;
        if (sel < off) off = sel;
        if (sel >= off + lrows) off = sel - lrows + 1;

        dlg_draw_frame(top, left, 20, w);
        vid_puts(top, left + 2,
                 save ? " Lagre som " : " " S_AA "pne fil ", A_DLG);
        vid_puts(top + 1, left + 2, "Filnavn:", A_DLG);
        dlg_field_draw(top + 1, left + 11, w - 13, &f);
        getcwd(cwd, (u16)sizeof(cwd));
        cwd[w - 4] = '\0';
        vid_puts(top + 2, left + 2, cwd, A_DLG);

        for (r = 0; r < lrows; r++) {
            i = off + r;
            if (i >= fb_n)
                continue;
            fb_entry_text(i, txt);
            if (focus_list && i == sel) {
                vid_fill(lrow0 + r, left + 2, 16, ' ', A_DLG_HI);
                vid_puts(lrow0 + r, left + 2, txt, A_DLG_HI);
            } else {
                vid_puts(lrow0 + r, left + 2, txt, A_DLG);
            }
        }
        vid_puts(top + 18, left + 2,
                 "Enter=Velg  Tab=Bytt felt  Esc=Avbryt", A_DLG);
        vid_flush();
        if (focus_list)
            vid_cursor_hide();
        else
            vid_cursor(top + 1, left + 11 + f.pos);

        k = kbd_get();
        if (k == K_ESC)
            return 0;
        if (k == K_TAB) {
            focus_list = !focus_list;
            continue;
        }
        if (k == K_ENTER) {
            if (!focus_list || fb_n == 0) {
                if (f.len == 0)
                    continue;
                namebuf[f.len] = '\0';
            } else if (fb_kind[sel] == FK_DIR) {
                chdir(fb_name[sel]);
                fb_read_dir();
                sel = off = 0;
                continue;
            } else {
                strcpy(namebuf, fb_name[sel]);
            }
            if (save) {
                th = open(namebuf, O_RDONLY);
                if (th >= 0) {
                    close(th);
                    if (dlg_msgbox("Filen finnes - overskrive?", DB_JNA)
                            != DR_JA)
                        continue;
                }
            }
            strcpy(out, namebuf);
            return 1;
        }
        if (focus_list) {
            switch (k) {
            case K_UP:
                if (sel > 0) sel--;
                break;
            case K_DOWN:
                if (sel < fb_n - 1) sel++;
                break;
            case K_PGUP:
                sel -= lrows - 1;
                if (sel < 0) sel = 0;
                break;
            case K_PGDN:
                sel += lrows - 1;
                if (sel > fb_n - 1) sel = fb_n - 1;
                break;
            case K_HOME:
                sel = 0;
                break;
            case K_END:
                sel = fb_n ? fb_n - 1 : 0;
                break;
            default:
                if (k >= 32 && k < 0x100) {
                    focus_list = 0;
                    dlg_field_key(&f, k);
                }
                break;
            }
        } else {
            dlg_field_key(&f, k);
        }
    }
}
