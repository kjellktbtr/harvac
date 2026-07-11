/* dlg.c -- Modal dialogs for Harvac port of MEDIT.
 * DOS _dos_findfirst/findnext replaced with SYSCALL_OPENDIR/READDIR/CLOSEDIR.
 * Drive enumeration removed (Harvac has no drive letters).
 * getcwd/chdir replaced with SYSCALL_GETCWD/CHDIR. */

#include "medit.h"
#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "vid.h"
#include "kbd.h"
#include "dlg.h"

/* cp437/cp865 single-line box characters (same codes in both) */
#define BOX_TL 0xDA
#define BOX_TR 0xBF
#define BOX_BL 0xC0
#define BOX_BR 0xD9
#define BOX_H  0xC4
#define BOX_V  0xB3

#define A_DLG   0x70    /* dialog body: inverse */
#define A_DLGHI 0x07    /* selected button: normal (double inverse) */

static void draw_frame(int top, int left, int h, int w)
{
    int r, c;
    vid_putat(top, left, BOX_TL, A_DLG);
    vid_putat(top, left + w - 1, BOX_TR, A_DLG);
    vid_putat(top + h - 1, left, BOX_BL, A_DLG);
    vid_putat(top + h - 1, left + w - 1, BOX_BR, A_DLG);
    for (c = 1; c < w - 1; c++) {
        vid_putat(top, left + c, BOX_H, A_DLG);
        vid_putat(top + h - 1, left + c, BOX_H, A_DLG);
    }
    for (r = 1; r < h - 1; r++) {
        vid_putat(top + r, left, BOX_V, A_DLG);
        vid_putat(top + r, left + w - 1, BOX_V, A_DLG);
        vid_fill(top + r, left + 1, w - 2, ' ', A_DLG);
    }
}

static void draw_button(int row, int col, const char *label, int selected)
{
    u8 a = selected ? A_DLGHI : A_DLG;
    vid_putat(row, col, '<', a);
    vid_puts(row, col + 2, label, a);
    vid_putat(row, col + 3 + (int)strlen(label), '>', a);
}

int dlg_msgbox(const char *text, int buttons)
{
    static const char *lbl_ok[1]   = { "OK" };
    static const char *lbl_jna[3]  = { "Ja", "Nei", "Avbryt" };
    static const char *lbl_jnaa[4] = { "Ja", "Nei", "Alle", "Avbryt" };
    static const int   res_ok[1]   = { DR_JA };
    static const int   res_jna[3]  = { DR_JA, DR_NEI, DR_AVBRYT };
    static const int   res_jnaa[4] = { DR_JA, DR_NEI, DR_ALLE, DR_AVBRYT };
    const char **lbl;
    const int *res;
    int w, top, left, tlen, bw;
    int nbtn, sel = 0, i, col, brow;
    unsigned k;

    switch (buttons) {
    case DB_JNA:
        lbl = lbl_jna;  res = res_jna;  nbtn = 3;
        break;
    case DB_JNAA:
        lbl = lbl_jnaa; res = res_jnaa; nbtn = 4;
        break;
    default:
        lbl = lbl_ok;   res = res_ok;   nbtn = 1;
        break;
    }
    bw = 0;
    for (i = 0; i < nbtn; i++)
        bw += (int)strlen(lbl[i]) + 6;

    tlen = (int)strlen(text);
    w = tlen + 6;
    if (w < bw + 4)
        w = bw + 4;
    if (w > 76)
        w = 76;
    top = 8;
    left = (COLS - w) / 2;

    vid_cursor_hide();
    for (;;) {
        draw_frame(top, left, 6, w);
        vid_puts(top + 1, left + (w - tlen) / 2, text, A_DLG);
        brow = top + 3;
        col = left + (w - bw) / 2;
        for (i = 0; i < nbtn; i++) {
            draw_button(brow, col, lbl[i], sel == i);
            col += (int)strlen(lbl[i]) + 6;
        }
        vid_flush();

        k = kbd_get();
        switch (k) {
        case K_ESC:
            return DR_AVBRYT;
        case K_ENTER:
            return res[sel];
        case K_LEFT:
            sel = (sel + nbtn - 1) % nbtn;
            break;
        case K_RIGHT:
        case K_TAB:
            sel = (sel + 1) % nbtn;
            break;
        default:
            if (nbtn >= 3) {
                if (k == 'j' || k == 'J') return DR_JA;
                if (k == 'n' || k == 'N') return DR_NEI;
                if (nbtn == 4 && (k == 'a' || k == 'A')) return DR_ALLE;
                if (nbtn == 3 && (k == 'a' || k == 'A')) return DR_AVBRYT;
            }
            break;
        }
    }
}

/* ---------------- text input field ---------------- */

typedef struct {
    char *buf;
    int max, len, pos;
    int uppercase;
} Field;

static void field_draw(int row, int col, int w, Field *f)
{
    int i;
    for (i = 0; i < w; i++)
        vid_putat(row, col + i, i < f->len ? (u8)f->buf[i] : ' ', A_DLGHI);
}

/* returns 1 if the key was consumed by the field */
static int field_key(Field *f, unsigned k)
{
    switch (k) {
    case K_LEFT:
        if (f->pos > 0) f->pos--;
        return 1;
    case K_RIGHT:
        if (f->pos < f->len) f->pos++;
        return 1;
    case K_HOME:
        f->pos = 0;
        return 1;
    case K_END:
        f->pos = f->len;
        return 1;
    case K_BS:
        if (f->pos > 0) {
            memmove(f->buf + f->pos - 1, f->buf + f->pos, f->len - f->pos);
            f->pos--;
            f->len--;
        }
        return 1;
    case K_DEL:
        if (f->pos < f->len) {
            memmove(f->buf + f->pos, f->buf + f->pos + 1,
                    f->len - f->pos - 1);
            f->len--;
        }
        return 1;
    default:
        if (k >= 32 && k < 0x100 && k != 127 && f->len < f->max) {
            memmove(f->buf + f->pos + 1, f->buf + f->pos, f->len - f->pos);
            f->buf[f->pos++] = (char)(f->uppercase && k >= 'a' && k <= 'z'
                                       ? k - 32 : k);
            f->len++;
            return 1;
        }
        return 0;
    }
}

int dlg_input(const char *title, char *buf, int max)
{
    Field f;
    int w = 44, top = 9, left = (COLS - w) / 2;
    unsigned k;

    f.buf = buf;
    f.max = max;
    f.uppercase = 0;
    if (f.max > w - 4)
        f.max = w - 4;
    f.len = (int)strlen(buf);
    f.pos = f.len;

    for (;;) {
        draw_frame(top, left, 5, w);
        vid_puts(top, left + 2, title, A_DLG);
        field_draw(top + 2, left + 2, w - 4, &f);
        vid_flush();
        vid_cursor(top + 2, left + 2 + f.pos);

        k = kbd_get();
        if (k == K_ESC)
            return 0;
        if (k == K_ENTER) {
            buf[f.len] = '\0';
            return 1;
        }
        field_key(&f, k);
    }
}

/* ---------------- file browser ---------------- */

/* Opaque dir handle: same size as kernel's fat16_dir_t (7 uint16_t fields). */
typedef struct { u16 raw[7]; } harvac_dir_t;

/* Dir entry: same layout as fat16_dirent_t. */
typedef struct {
    u8  name[8];
    u8  ext[3];
    u8  attrs;
    u8  reserved[10];
    u16 time;
    u16 date;
    u16 first_cluster;
    u32 file_size;
} harvac_dirent_t;

#define HATTR_VOLUME    0x08
#define HATTR_DIRECTORY 0x10

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

/* Build null-terminated 8.3 name from FAT16 raw fields (no dot if no ext). */
static void dirent_name(const harvac_dirent_t *d, char *out)
{
    int i, j = 0;
    for (i = 0; i < 8 && d->name[i] != ' '; i++)
        out[j++] = (char)d->name[i];
    if (d->ext[0] != ' ') {
        out[j++] = '.';
        for (i = 0; i < 3 && d->ext[i] != ' '; i++)
            out[j++] = (char)d->ext[i];
    }
    out[j] = '\0';
}

static void fb_read_dir(void)
{
    harvac_dir_t    dir;
    harvac_dirent_t de;
    char            cwd[64];
    char            nm[13];
    int             ret;

    fb_n = 0;

    syscall_int40(SYSCALL_GETCWD, 0, 0, (u16)cwd, (u16)sizeof(cwd), 0, 0);
    if (cwd[0] != '/' || cwd[1] != '\0')
        fb_add("..", FK_DIR);

    syscall_int40(SYSCALL_OPENDIR, 0, 0, (u16)&dir, 0, 0, 0);
    for (;;) {
        ret = (int)syscall_int40(SYSCALL_READDIR, 0, 0,
                                 (u16)&dir, (u16)&de, 0, 0);
        if (ret != 0)
            break;
        if (de.name[0] == 0 || de.name[0] == (u8)0xE5)
            continue;
        if (de.attrs & HATTR_VOLUME)
            continue;
        dirent_name(&de, nm);
        if (nm[0] == '.' && nm[1] == '\0')
            continue;
        if (nm[0] == '.' && nm[1] == '.' && nm[2] == '\0')
            continue;
        if (fb_n < FB_MAX)
            fb_add(nm, (de.attrs & HATTR_DIRECTORY) ? FK_DIR : FK_FILE);
    }
    syscall_int40(SYSCALL_CLOSEDIR, 0, 0, (u16)&dir, 0, 0, 0);

    fb_sort();
}

static void fb_entry_text(int i, char *out)     /* out: 14+ bytes */
{
    if (fb_kind[i] == FK_DIR) {
        out[0] = '[';
        strcpy(out + 1, fb_name[i]);
        strcat(out, "]");
    } else {
        strcpy(out, fb_name[i]);
    }
}

int dlg_filebox(char *out, int save)
{
    static char namebuf[66];
    Field f;
    char cwd[64], txt[16];
    int top = 2, w = 44, left = (COLS - w) / 2;
    int lrows = 13, lrow0 = top + 4;
    int sel = 0, off = 0, focus_list = 1;
    int i, r;
    unsigned k;
    u16 th;

    namebuf[0] = '\0';
    f.buf = namebuf;
    f.max = 65;
    f.uppercase = 1;
    f.len = f.pos = 0;

    fb_read_dir();

    for (;;) {
        if (sel >= fb_n) sel = fb_n ? fb_n - 1 : 0;
        if (sel < off) off = sel;
        if (sel >= off + lrows) off = sel - lrows + 1;

        draw_frame(top, left, 20, w);
        vid_puts(top, left + 2,
                 save ? " Lagre som " : " " S_AA "pne fil ", A_DLG);
        vid_puts(top + 1, left + 2, "Filnavn:", A_DLG);
        field_draw(top + 1, left + 11, w - 13, &f);
        syscall_int40(SYSCALL_GETCWD, 0, 0, (u16)cwd, (u16)sizeof(cwd), 0, 0);
        cwd[w - 4] = '\0';
        vid_puts(top + 2, left + 2, cwd, A_DLG);

        for (r = 0; r < lrows; r++) {
            i = off + r;
            if (i >= fb_n)
                continue;
            fb_entry_text(i, txt);
            if (focus_list && i == sel) {
                vid_fill(lrow0 + r, left + 2, 16, ' ', A_DLGHI);
                vid_puts(lrow0 + r, left + 2, txt, A_DLGHI);
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
                syscall_int40(SYSCALL_CHDIR, 0, (u16)fb_name[sel], 0, 0, 0, 0);
                fb_read_dir();
                sel = off = 0;
                continue;
            } else {
                strcpy(namebuf, fb_name[sel]);
            }
            /* overwrite check for save: try to open the file */
            if (save) {
                th = (u16)syscall_int40(SYSCALL_OPEN, 0,
                                        (u16)namebuf, 0, 0, 0, 0);
                if (th != 0xFFFFu) {
                    syscall_int40(SYSCALL_CLOSE, 0, th, 0, 0, 0, 0);
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
                    field_key(&f, k);
                }
                break;
            }
        } else {
            field_key(&f, k);
        }
    }
}
