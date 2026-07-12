/* main.c -- NCD (Norton Commander Clone) for Harvac OS.
 * Entry point _main(). Dual-pane file browser with F-key operations.
 *
 * NOTE: _main MUST be the first function defined (wlink places functions
 * in source order; file offset 0 = entry point at memory 0x0100).
 */

#include "ncd.h"
#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "vid.h"
#include "kbd.h"
#include "fs.h"
#include "panel.h"

#include "dlg.h"
#include "viewer.h"

/* Forward declarations */
static void full_render(void);
static void launch_child(const char *cmd, const char *args, int pause);
static void shell_prompt(const char *prefill);
static void handle_f3(void);
static void handle_f4(void);
static void handle_shift_f4(void);
static void handle_f5(void);
static void handle_f6(void);
static void handle_f7(void);
static void handle_f8(void);

/* --- Main entry point --- */

void __far _main(void)
{
    unsigned k;

    /* Initialize video */
    vid_init();

    /* Initialize panels */
    panel_init(&panel_left);
    panel_init(&panel_right);
    active_panel = PANEL_LEFT;

    /* Refresh both panels (both start at root) */
    panel_refresh(&panel_left);
    panel_refresh(&panel_right);

    /* Main loop */
    for (;;) {
        full_render();

        k = kbd_get();

        /* F10: Quit */
        if (k == K_F10) {
            vid_done();
            ncd_exit();
        }

        /* F3: View, F4: Edit (Shift-F4: new file), F5-F8: File ops */
        if (k == K_F3) { handle_f3(); continue; }
        if (k == K_F4) {
            if (kbd_shift & SH_SHIFT)
                handle_shift_f4();
            else
                handle_f4();
            continue;
        }
        if (k == K_F5) { handle_f5(); continue; }
        if (k == K_F6) { handle_f6(); continue; }
        if (k == K_F7) { handle_f7(); continue; }
        if (k == K_F8) { handle_f8(); continue; }

        /* Tab: switch active panel */
        if (k == K_TAB) {
            active_panel = active_panel == PANEL_LEFT ? PANEL_RIGHT : PANEL_LEFT;
            continue;
        }

        /* Insert: toggle selection */
        if (k == K_INS) {
            panel_t *p = active_panel == PANEL_LEFT ? &panel_left : &panel_right;
            panel_toggle_select(p);
            continue;
        }

        /* Navigation keys */
        {
            panel_t *p = active_panel == PANEL_LEFT ? &panel_left : &panel_right;

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
                    /* Not a directory - try launching .BAT or .COM */
                    panel_entry_t e;
                    panel_entry_get(p, p->sel, &e);
                    if (p->sel < p->count) {
                        if (m_ends_with(e.name, ".bat")) {
                            /* Run the batch via SHELL.COM one-shot mode */
                            launch_child("SHELL.COM", e.name, 1);
                        } else if (m_ends_with(e.name, ".com")) {
                            launch_child(e.name, "", 1);
                        }
                    }
                }
                break;
            case K_BS:
                panel_parent_dir(p);
                break;
            case K_LEFT:
                /* Switch to other panel or go to parent */
                if (kbd_shift & SH_SHIFT) {
                    active_panel = active_panel == PANEL_LEFT ? PANEL_RIGHT : PANEL_LEFT;
                } else {
                    panel_parent_dir(p);
                }
                break;
            case K_RIGHT:
                /* Switch to other panel or enter dir */
                if (kbd_shift & SH_SHIFT) {
                    active_panel = active_panel == PANEL_LEFT ? PANEL_RIGHT : PANEL_LEFT;
                } else {
                    panel_enter_dir(p);
                }
                break;
            default:
                /* Printable character: open shell prompt */
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
}

/* --- Full screen render --- */

static void render_cmdline(void)
{
    panel_t *p = active_panel == PANEL_LEFT ? &panel_left : &panel_right;
    vid_fill(ROW_SHELL, 0, COLS, ' ', A_SHELL);
    vid_puts(ROW_SHELL, 0, p->cwd, A_SHELL);
    vid_puts(ROW_SHELL, (u16)strlen(p->cwd), ">", A_SHELL);
}

static void full_render(void)
{
    render_pane_borders();
    panel_render(&panel_left, PANEL_LEFT);
    panel_render(&panel_right, PANEL_RIGHT);
    render_status_line();
    vid_fill(ROW_MSG, 0, COLS, ' ', A_NORMAL);
    render_cmdline();
    render_fkey_bar();
    vid_cursor_hide();
    vid_flush();
}

/* --- Child program launcher --- */

/* Clear the screen, run the child, optionally wait for a key so its
 * output can be read, then restore the browser view. */
static void launch_child(const char *cmd, const char *args, int pause)
{
    static const char pak_msg[] = "\r\n[Press any key]";

    syscall_int40(SYSCALL_CLEAR_SCREEN, 0, 0, 0, 0, 0, 0);
    ncd_exec(cmd, args);
    if (pause) {
        /* Write directly to VGA: the child owned the screen */
        syscall_int40(SYSCALL_WRITE_VGA, 0, 0, 0, 0, (u16)pak_msg, 0);
        kbd_get();
    }
    panel_refresh(&panel_left);
    panel_refresh(&panel_right);
    full_render();
}

/* --- Shell prompt --- */

static void shell_prompt(const char *prefill)
{
    char cmd[SHELL_BUF_SIZE];
    char cwd[PATH_MAX];
    u16  cmd_len = 0;
    unsigned k;

    /* Commands run in the active panel's directory */
    panel_sync_cwd(active_panel == PANEL_LEFT ? &panel_left : &panel_right);
    ncd_getcwd(cwd, sizeof(cwd));

    /* Pre-fill from prefill if provided */
    cmd[0] = '\0';
    cmd_len = 0;
    if (prefill) {
        while (*prefill && cmd_len < SHELL_BUF_SIZE - 1)
            cmd[cmd_len++] = *prefill++;
        cmd[cmd_len] = '\0';
    }

    /* Render shell prompt */
    vid_fill(ROW_SHELL, 0, COLS, ' ', A_SHELL);
    vid_puts(ROW_SHELL, 0, "> ", A_SHELL);
    vid_puts(ROW_SHELL, 2, cwd, A_SHELL);
    vid_puts(ROW_SHELL, 2 + (u16)strlen(cwd), "> ", A_SHELL);
    vid_puts(ROW_SHELL, 2 + (u16)strlen(cwd) + 2, cmd, A_SHELL);
    vid_cursor(ROW_SHELL, 2 + (u16)strlen(cwd) + 2 + cmd_len);
    vid_flush();

    /* Read command */
    for (;;) {
        k = kbd_get();
        switch (k) {
        case K_ESC:
            /* Cancel: restore browser view */
            full_render();
            return;
        case K_ENTER:
            cmd[cmd_len] = '\0';
            /* Execute command if non-empty (SHELL.COM one-shot mode) */
            if (cmd_len > 0) {
                launch_child("SHELL.COM", cmd, 1);
            } else {
                full_render();
            }
            return;
        case K_BS:
            if (cmd_len > 0) {
                cmd_len--;
                cmd[cmd_len] = '\0';
                /* Redraw prompt line */
                vid_fill(ROW_SHELL, 0, COLS, ' ', A_SHELL);
                vid_puts(ROW_SHELL, 0, "> ", A_SHELL);
                vid_puts(ROW_SHELL, 2, cwd, A_SHELL);
                vid_puts(ROW_SHELL, 2 + (u16)strlen(cwd), "> ", A_SHELL);
                vid_puts(ROW_SHELL, 2 + (u16)strlen(cwd) + 2, cmd, A_SHELL);
                vid_cursor(ROW_SHELL, 2 + (u16)strlen(cwd) + 2 + cmd_len);
                vid_flush();
            }
            break;
        default:
            if (k >= 32 && k < 127 && cmd_len < SHELL_BUF_SIZE - 1) {
                cmd[cmd_len++] = (char)k;
                cmd[cmd_len] = '\0';
                /* Redraw prompt line */
                vid_fill(ROW_SHELL, 0, COLS, ' ', A_SHELL);
                vid_puts(ROW_SHELL, 0, "> ", A_SHELL);
                vid_puts(ROW_SHELL, 2, cwd, A_SHELL);
                vid_puts(ROW_SHELL, 2 + (u16)strlen(cwd), "> ", A_SHELL);
                vid_puts(ROW_SHELL, 2 + (u16)strlen(cwd) + 2, cmd, A_SHELL);
                vid_cursor(ROW_SHELL, 2 + (u16)strlen(cwd) + 2 + cmd_len);
                vid_flush();
            }
            break;
        }
    }
}

/* --- F-key handlers --- */

static void handle_f3(void)
{
    panel_t *p = active_panel == PANEL_LEFT ? &panel_left : &panel_right;
    panel_entry_t e;
    panel_sync_cwd(p);
    panel_entry_get(p, p->sel, &e);
    if (p->sel < p->count) {
        if (e.is_dir) {
            panel_enter_dir(p);
            panel_refresh(&panel_left);
            panel_refresh(&panel_right);
            full_render();
        } else {
            viewer_open(e.name);
            /* Refresh after viewer closes */
            panel_refresh(&panel_left);
            panel_refresh(&panel_right);
            full_render();
        }
    }
}

static void handle_f4(void)
{
    panel_t *p = active_panel == PANEL_LEFT ? &panel_left : &panel_right;
    panel_entry_t e;
    panel_sync_cwd(p);
    panel_entry_get(p, p->sel, &e);
    if (p->sel < p->count && !e.is_dir) {
        /* Launch EDIT.COM with the selected file (no pause: fullscreen) */
        launch_child("EDIT.COM", e.name, 0);
    }
}

/* Shift-F4: ask for a name, create the empty file in the active
 * directory, then open it in EDIT. */
static void handle_shift_f4(void)
{
    panel_t *p = active_panel == PANEL_LEFT ? &panel_left : &panel_right;
    char name[NAME_MAX];
    u16 h;

    name[0] = '\0';
    if (dlg_input("New file:", name, NAME_MAX - 1) != DR_OK
        || name[0] == '\0') {
        full_render();
        return;
    }

    panel_sync_cwd(p);

    /* Create the file if it doesn't exist yet */
    h = ncd_create(name);
    if (h < 16)
        ncd_close(h);

    launch_child("EDIT.COM", name, 0);
}

static void handle_f5(void)
{
    panel_t *src_p = active_panel == PANEL_LEFT ? &panel_left : &panel_right;
    panel_t *dst_p = active_panel == PANEL_LEFT ? &panel_right : &panel_left;
    char dst_path[PATH_MAX];
    char msg[96];
    u16 i;
    u16 copied = 0;
    u16 sel_count;

    if (src_p->count == 0)
        return;

    /* Copying onto itself would free the source's clusters mid-read */
    if (m_strcmp(src_p->cwd, dst_p->cwd) == 0) {
        dlg_msgbox("Panels show the same directory", DB_OK);
        full_render();
        return;
    }

    sel_count = panel_count_selected(src_p);

    /* Confirmation: "Copy NAME to /TMP?" / "Copy N files to /TMP?" */
    strcpy(msg, "Copy ");
    if (sel_count > 0) {
        char nbuf[8];
        m_u32toa((u32)sel_count, nbuf);
        strcat(msg, nbuf);
        strcat(msg, " file(s)");
    } else {
        panel_entry_t e_tmp;
        panel_entry_get(src_p, src_p->sel, &e_tmp);
        strcat(msg, e_tmp.name);
    }
    strcat(msg, " to ");
    strcat(msg, dst_p->cwd);
    strcat(msg, "?");
    if (dlg_msgbox(msg, DB_JNA) != DR_YES) {
        full_render();
        return;
    }

    /* Change to source directory */
    panel_sync_cwd(src_p);

    /* Copy selected files, or the current file if none selected */
    if (sel_count > 0) {
        for (i = 0; i < src_p->count; i++) {
            panel_entry_t e_tmp;
            panel_entry_get(src_p, i, &e_tmp);
            if (!e_tmp.selected) continue;
            strcpy(dst_path, dst_p->cwd);
            strcat(dst_path, "/");
            strcat(dst_path, e_tmp.name);
            ncd_copy_recursive(e_tmp.name, dst_path);
            copied++;
        }
    } else {
        panel_entry_t e_tmp;
        panel_entry_get(src_p, src_p->sel, &e_tmp);
        strcpy(dst_path, dst_p->cwd);
        strcat(dst_path, "/");
        strcat(dst_path, e_tmp.name);
        ncd_copy_recursive(e_tmp.name, dst_path);
        copied = 1;
    }

    /* Refresh both panels (each re-syncs its own CWD) */
    panel_refresh(&panel_left);
    panel_refresh(&panel_right);
    full_render();

    /* Show result on the message row */
    if (copied > 0) {
        m_u32toa((u32)copied, msg);
        strcat(msg, " copied");
        vid_puts(ROW_MSG, 1, msg, A_STATUS);
        vid_flush();
    }
}

static void handle_f6(void)
{
    panel_t *p = active_panel == PANEL_LEFT ? &panel_left : &panel_right;
    char new_name[NAME_MAX];
    char old_cwd[PATH_MAX];
    panel_entry_t e;

    if (p->sel >= p->count) return;

    panel_entry_get(p, p->sel, &e);
    strcpy(new_name, e.name);

    /* Save CWD and change to panel's directory */
    ncd_getcwd(old_cwd, sizeof(old_cwd));
    ncd_chdir(p->cwd);

    /* Show rename dialog */
    if (dlg_input("Rename/Move:", new_name, NAME_MAX - 1) == DR_OK) {
        if (m_strcmp(new_name, e.name) != 0) {
            ncd_rename(e.name, new_name);
        }
    }

    /* Restore CWD */
    ncd_chdir(old_cwd);

    /* Refresh both panels */
    panel_refresh(&panel_left);
    panel_refresh(&panel_right);
    full_render();
}

static void handle_f7(void)
{
    panel_t *p = active_panel == PANEL_LEFT ? &panel_left : &panel_right;
    char dir_name[NAME_MAX];
    char old_cwd[PATH_MAX];

    dir_name[0] = '\0';

    /* Save CWD and change to panel's directory */
    ncd_getcwd(old_cwd, sizeof(old_cwd));
    ncd_chdir(p->cwd);

    /* Show mkdir dialog */
    if (dlg_input("Create directory:", dir_name, NAME_MAX - 1) == DR_OK) {
        if (dir_name[0] != '\0') {
            ncd_mkdir(dir_name);
        }
    }

    /* Restore CWD */
    ncd_chdir(old_cwd);

    /* Refresh both panels */
    panel_refresh(&panel_left);
    panel_refresh(&panel_right);
    full_render();
}

static void handle_f8(void)
{
    panel_t *p = active_panel == PANEL_LEFT ? &panel_left : &panel_right;
    char old_cwd[PATH_MAX];
    u16 i;
    u16 sel_count;

    /* Count selected files */
    sel_count = panel_count_selected(p);
    if (sel_count == 0) sel_count = 1;  /* delete current file */

    /* Confirm deletion */
    if (dlg_msgbox("Delete selected file(s)?", DB_JNA) != DR_OK)
        return;

    /* Save CWD and change to panel's directory */
    ncd_getcwd(old_cwd, sizeof(old_cwd));
    ncd_chdir(p->cwd);

    /* Delete selected files, or current file if none selected */
    if (panel_count_selected(p) > 0) {
        for (i = 0; i < p->count; i++) {
            panel_entry_t e_tmp;
            panel_entry_get(p, i, &e_tmp);
            if (e_tmp.selected) {
                ncd_delete_recursive(e_tmp.name);
            }
        }
    } else if (p->sel < p->count) {
        panel_entry_t e_tmp;
        panel_entry_get(p, p->sel, &e_tmp);
        ncd_delete_recursive(e_tmp.name);
    }

    /* Restore CWD */
    ncd_chdir(old_cwd);

    /* Refresh both panels */
    panel_refresh(&panel_left);
    panel_refresh(&panel_right);
    full_render();
}
