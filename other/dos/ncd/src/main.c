/* main.c -- DOS NCD (Norton Commander Clone)
 * Entry point and main event loop.
 * Build with: wcc -ms -0 -os -s -bt=com main.c video.c keys.c mem.c fs.c dialog.c panel.c viewer.c shell.c
 */

#include "dos_ncd.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* --- Forward declarations --- */
static void full_render(void);
static void render_cmdline(void);
static void render_status_line(void);
static void render_fkey_bar(void);
static void launch_child(const char *cmd, const char *args);
static void shell_prompt(const char *prefill);
static void handle_f3(void);
static void handle_f4(void);
static void handle_shift_f4(void);
static void handle_f5(void);
static void handle_f6(void);
static void handle_f7(void);
static void handle_f8(void);
static void copy_selected(int move);
static void delete_selected(void);
static void make_directory(void);
static panel_t *get_active_panel(void);
static panel_t *get_other_panel(void);
static void build_full_path(panel_t *p, const char *name, char *buf, size_t bufsize);

/* --- Global shell buffer --- */
static char shell_buf[SHELL_BUF_SIZE];

/* --- Main entry point --- */

int main(void)
{
    u16 k;

    /* Initialize video */
    vid_init();

    /* Initialize panels */
    panel_init(&panel_left);
    panel_init(&panel_right);
    active_panel = PANEL_LEFT;

    /* Start both panels at root */
    panel_left.cwd[0] = '\\';
    panel_left.cwd[1] = '\0';
    panel_right.cwd[0] = '\\';
    panel_right.cwd[1] = '\0';

    /* Refresh both panels */
    panel_refresh(&panel_left);
    panel_refresh(&panel_right);
    panel_sort(&panel_left);
    panel_sort(&panel_right);

    /* Main loop */
    for (;;) {
        full_render();

        k = kbd_get();

        /* F10: Quit */
        if (k == K_F10) {
            vid_done();
            return 0;
        }

        /* F3: View */
        if (k == K_F3) {
            handle_f3();
            continue;
        }

        /* F4: Edit (Shift+F4: New file) */
        if (k == K_F4) {
            if (kbd_shift & SH_SHIFT) {
                handle_shift_f4();
            } else {
                handle_f4();
            }
            continue;
        }

        /* F5: Copy, F6: Move, F7: Mkdir, F8: Delete */
        if (k == K_F5) { handle_f5(); continue; }
        if (k == K_F6) { handle_f6(); continue; }
        if (k == K_F7) { handle_f7(); continue; }
        if (k == K_F8) { handle_f8(); continue; }

        /* Tab: Switch panel */
        if (k == K_TAB) {
            active_panel = (active_panel == PANEL_LEFT) ? PANEL_RIGHT : PANEL_LEFT;
            continue;
        }

        /* Insert: Toggle selection */
        if (k == K_INS) {
            panel_t *p = get_active_panel();
            panel_toggle_select(p);
            continue;
        }

        /* Navigation */
        {
            panel_t *p = get_active_panel();

            switch (k) {
                case K_UP:
                    panel_up(p);
                    break;
                case K_DOWN:
                    panel_down(p);
                    break;
                case K_PGUP:
                    panel_page_up(p);
                    break;
                case K_PGDN:
                    panel_page_down(p);
                    break;
                case K_HOME:
                    panel_home(p);
                    break;
                case K_END:
                    panel_end(p);
                    break;
                case K_ENTER:
                    if (!panel_enter_dir(p)) {
                        /* Not a directory - try to execute */
                        const char *name = panel_get_name(p);
                        if (p->sel < p->count) {
                            panel_entry_t *e = &p->entries[p->sel];
                            if (strstr(e->name, ".BAT") || strstr(e->name, ".bat")) {
                                launch_child("COMMAND.COM", e->name);
                            } else if (strstr(e->name, ".COM") || strstr(e->name, ".EXE")) {
                                launch_child(e->name, "");
                            }
                        }
                    }
                    break;
                case K_BS:
                    panel_parent_dir(p);
                    break;
                case K_LEFT:
                    if (kbd_shift & SH_SHIFT) {
                        active_panel = (active_panel == PANEL_LEFT) ? PANEL_RIGHT : PANEL_LEFT;
                    } else {
                        panel_parent_dir(p);
                    }
                    break;
                case K_RIGHT:
                    if (kbd_shift & SH_SHIFT) {
                        active_panel = (active_panel == PANEL_LEFT) ? PANEL_RIGHT : PANEL_LEFT;
                    } else {
                        panel_enter_dir(p);
                    }
                    break;
                default:
                    /* Printable character: shell prompt */
                    if (k >= 32 && k < 127) {
                        char pb[2];
                        pb[0] = (char)k;
                        pb[1] = '\0';
                        shell_prompt(pb);
                    }
                    break;
            }
        }
    }

    return 0;
}

/* --- Rendering --- */

static void render_cmdline(void)
{
    panel_t *p = get_active_panel();
    vid_fill(ROW_SHELL, 0, COLS, ' ', A_SHELL);
    vid_puts(ROW_SHELL, 0, p->cwd, A_SHELL);
    vid_puts(ROW_SHELL, strlen(p->cwd), ">", A_SHELL);
}

static void render_status_line(void)
{
    panel_t *p = get_active_panel();
    char status[80];
    u16 sel_count = panel_count_selected(p);

    if (sel_count > 0) {
        snprintf(status, sizeof(status), " %u selected | %u files ", sel_count, p->count);
    } else {
        snprintf(status, sizeof(status), " %u files ", p->count);
    }

    vid_fill(ROW_STATUS, 0, COLS, ' ', A_STATUS);
    vid_puts(ROW_STATUS, 0, status, A_STATUS);
}

static void render_fkey_bar(void)
{
    const char *fkeys = " F3 View  F4 Edit  F5 Copy  F6 Move  F7 Mkdir  F8 Delete  F10 Quit ";
    vid_fill(ROW_FKEYS, 0, COLS, ' ', A_INVERSE);
    vid_puts(ROW_FKEYS, (COLS - strlen(fkeys)) / 2, fkeys, A_INVERSE);
}

static void full_render(void)
{
    u8 c, r;

    /* Draw pane borders */
    vid_putat(0, 0, A_SEPARATOR, BOX_TL);
    vid_putat(0, 39, A_SEPARATOR, BOX_TRIGHT);
    vid_putat(0, 40, A_SEPARATOR, BOX_TDOWN);
    vid_putat(0, 79, A_SEPARATOR, BOX_TR);

    for (c = 1; c < 39; c++) vid_putat(0, c, A_SEPARATOR, BOX_H);
    for (c = 41; c < 79; c++) vid_putat(0, c, A_SEPARATOR, BOX_H);

    for (r = 1; r <= ROW_BORDER; r++) {
        vid_putat(r, 0, A_SEPARATOR, BOX_V);
        vid_putat(r, 39, A_SEPARATOR, BOX_TLEFT);
        vid_putat(r, 40, A_SEPARATOR, BOX_TRIGHT);
        vid_putat(r, 79, A_SEPARATOR, BOX_V);
    }

    vid_putat(ROW_BORDER, 0, A_SEPARATOR, BOX_BL);
    vid_putat(ROW_BORDER, 39, A_SEPARATOR, BOX_TUP);
    vid_putat(ROW_BORDER, 40, A_SEPARATOR, BOX_TDOWN);
    vid_putat(ROW_BORDER, 79, A_SEPARATOR, BOX_BR);
    for (c = 1; c < 39; c++) vid_putat(ROW_BORDER, c, A_SEPARATOR, BOX_H);
    for (c = 41; c < 79; c++) vid_putat(ROW_BORDER, c, A_SEPARATOR, BOX_H);

    /* Render panels */
    panel_render(&panel_left, PANEL_LEFT);
    panel_render(&panel_right, PANEL_RIGHT);

    /* Status line */
    render_status_line();

    /* Message line (clear) */
    vid_fill(ROW_MSG, 0, COLS, ' ', A_NORMAL);

    /* Command line */
    render_cmdline();

    /* F-key bar */
    render_fkey_bar();

    /* Hide cursor */
    vid_cursor_hide();

    /* Flush to screen */
    vid_flush();
}

/* --- F-key handlers --- */

static void handle_f3(void)
{
    panel_t *p = get_active_panel();
    if (p->sel < p->count) {
        const char *name = panel_get_name(p);
        char path[DOS_PATH_MAX];
        build_full_path(p, name, path, sizeof(path));

        if (!p->entries[p->sel].is_dir) {
            viewer_open(path);
            while (viewer_mode) {
                viewer_render();
                {
                    u16 key = kbd_get();
                    viewer_handle_key(key);
                }
            }
        }
    }
}

static void handle_f4(void)
{
    panel_t *p = get_active_panel();
    if (p->sel < p->count && !p->entries[p->sel].is_dir) {
        const char *name = panel_get_name(p);
        launch_child("EDIT.COM", name);
    }
}

static void handle_shift_f4(void)
{
    shell_prompt("newfile.txt");
    if (shell_buf[0]) {
        char path[DOS_PATH_MAX];
        panel_t *p = get_active_panel();
        FILE *fp;
        snprintf(path, sizeof(path), "%s%s", p->cwd, shell_buf);
        fp = fopen(path, "w");
        if (fp) fclose(fp);
        launch_child("EDIT.COM", shell_buf);
    }
}

static void handle_f5(void)
{
    copy_selected(0);
}

static void handle_f6(void)
{
    copy_selected(1);
}

static void handle_f7(void)
{
    make_directory();
}

static void handle_f8(void)
{
    delete_selected();
}

/* --- File operations --- */

static void copy_selected(int move)
{
    panel_t *src = get_active_panel();
    panel_t *dst = get_other_panel();
    u16 count;
    u16 i;

    count = panel_count_selected(src);
    if (count == 0 && src->sel < src->count) {
        /* No selection, use current file */
        panel_entry_t *e = &src->entries[src->sel];
        char src_path[DOS_PATH_MAX], dst_path[DOS_PATH_MAX];
        build_full_path(src, e->name, src_path, sizeof(src_path));
        snprintf(dst_path, sizeof(dst_path), "%s%s", dst->cwd, e->name);

        if (move) {
            fs_move_file(src_path, dst_path);
        } else {
            fs_copy_file(src_path, dst_path);
        }
        panel_refresh(dst);
    } else {
        /* Multiple selection */
        for (i = 0; i < src->count; i++) {
            if (src->entries[i].selected) {
                char src_path[DOS_PATH_MAX], dst_path[DOS_PATH_MAX];
                build_full_path(src, src->entries[i].name, src_path, sizeof(src_path));
                snprintf(dst_path, sizeof(dst_path), "%s%s", dst->cwd, src->entries[i].name);

                if (move) {
                    fs_move_file(src_path, dst_path);
                } else {
                    fs_copy_file(src_path, dst_path);
                }
            }
        }
        panel_refresh(dst);
        /* Clear selections */
        for (i = 0; i < src->count; i++) src->entries[i].selected = 0;
    }
}

static void delete_selected(void)
{
    panel_t *p = get_active_panel();
    u16 count = panel_count_selected(p);

    if (count == 0 && p->sel < p->count) {
        /* Single file */
        char path[DOS_PATH_MAX];
        build_full_path(p, panel_get_name(p), path, sizeof(path));

        if (dlg_confirm("Delete", "Delete this file?")) {
            if (p->entries[p->sel].is_dir) {
                fs_remove_dir(path);
            } else {
                fs_delete_file(path);
            }
            panel_refresh(p);
        }
    } else if (count > 0) {
        /* Multiple files */
        if (dlg_confirm("Delete", "Delete selected files?")) {
            u16 i;
            for (i = 0; i < p->count; i++) {
                if (p->entries[i].selected) {
                    char path[DOS_PATH_MAX];
                    build_full_path(p, p->entries[i].name, path, sizeof(path));
                    if (p->entries[i].is_dir) {
                        fs_remove_dir(path);
                    } else {
                        fs_delete_file(path);
                    }
                }
            }
            panel_refresh(p);
        }
    }
}

static void make_directory(void)
{
    shell_prompt("newdir");
    if (shell_buf[0]) {
        panel_t *p = get_active_panel();
        char path[DOS_PATH_MAX];
        snprintf(path, sizeof(path), "%s%s", p->cwd, shell_buf);
        if (fs_make_dir(path) == 0) {
            panel_refresh(p);
        }
    }
}

/* --- Shell prompt --- */

static void shell_prompt(const char *prefill)
{
    u8 pos;

    vid_fill(ROW_MSG, 0, COLS, ' ', A_SHELL);
    vid_puts(ROW_MSG, 0, prefill, A_SHELL);
    vid_cursor_show();
    vid_flush();

    strncpy(shell_buf, prefill, SHELL_BUF_SIZE - 1);
    shell_buf[SHELL_BUF_SIZE - 1] = '\0';
    pos = strlen(shell_buf);

    while (1) {
        u16 key = kbd_get();

        if (key == K_ENTER) {
            shell_buf[pos] = '\0';
            break;
        } else if (key == K_ESC) {
            shell_buf[0] = '\0';
            break;
        } else if (key == K_BS && pos > 0) {
            pos--;
            shell_buf[pos] = '\0';
            vid_putat(ROW_MSG, pos, A_SHELL, ' ');
        } else if (key >= 32 && key < 127 && pos < SHELL_BUF_SIZE - 1) {
            shell_buf[pos++] = (char)key;
            shell_buf[pos] = '\0';
            vid_putat(ROW_MSG, pos - 1, A_SHELL, shell_buf[pos - 1]);
        }
        vid_cursor_show();
        vid_flush();
    }

    vid_cursor_hide();
    vid_fill(ROW_MSG, 0, COLS, ' ', A_NORMAL);
}

/* --- Launch child program --- */

static void launch_child(const char *cmd, const char *args)
{
    vid_done();
    shell_exec(cmd, args);
    vid_init();
    vid_dirty_all();

    {
        panel_t *p = get_active_panel();
        panel_refresh(p);
        p = get_other_panel();
        panel_refresh(p);
    }
}

/* --- Helpers --- */

static panel_t *get_active_panel(void)
{
    return (active_panel == PANEL_LEFT) ? &panel_left : &panel_right;
}

static panel_t *get_other_panel(void)
{
    return (active_panel == PANEL_LEFT) ? &panel_right : &panel_left;
}

static void build_full_path(panel_t *p, const char *name, char *buf, size_t bufsize)
{
    if (p->cwd[0] == '\0' || (p->cwd[0] == '\\' && p->cwd[1] == '\0')) {
        snprintf(buf, bufsize, "\\%s", name);
    } else {
        snprintf(buf, bufsize, "%s%s", p->cwd, name);
    }
}