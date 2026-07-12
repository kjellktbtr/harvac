/* shell.c -- User-space REPL shell for HarvaOS
 * .COM entry point, uses libc stdio/string/unistd for I/O and string ops.
 * Built-in commands loop within one SHELL.COM invocation.
 * External commands use SYSCALL_EXEC; since EXEC now returns (DOS model),
 * the shell is fully resident: it stays in memory while children run at
 * CHILD_SEGMENT (0x6000).
 * "exit" calls _exit(); root-level exit restarts boot_shell.
 *
 * Batch scripts (.BAT): typing NAME.BAT or NAME (falling back to NAME.BAT)
 * opens the file and runs lines in order; echo on/off and @ suppression work.
 * Scripts run fully within the shell loop - no kernel batch state needed.
 *
 * NOTE: _main MUST be the first function defined (wlink com places functions
 * in source order; file offset 0 = entry point at memory 0x0100).
 */

#include "types.h"
#include "constants.h"
#include "port_io.h"
#include "stdio.h"
#include "string.h"
#include "fcntl.h"
#include "unistd.h"
#include "dirent.h"
#include "fnmatch.h"

/* Line buffer size */
#define LINE_MAX    128

/* Sentinel for raw kernel redirect handle (NO_HANDLE = none active) */
#define NO_HANDLE  0xFFFFu

/* Glob expansion buffer (reused every command execution) */
#define GLOB_BUF_SIZE  512
/* PSP command tail spans 0x82..0xFF — 125 usable bytes before the entry point.
 * Stop adding glob tokens once this limit is reached (whole-token boundary)
 * so the kernel's arg-copy never corrupts the child's code at 0x100. */
#define PSP_ARG_MAX    124
static char _glob_buf[GLOB_BUF_SIZE];

/* Forward declarations */
static int         read_bat_line(int fd, char *buf, uint16_t maxlen);
static void        cmd_help(void);
static void        cmd_cd(const char *arg);
static void        cmd_pwd(void);
static const char *glob_expand_args(const char *args);

/* _main: entry point (__far so retf returns to kernel) */
void __far _main(void)
{
    char line[LINE_MAX];
    char cwd[64];
    char bat_name[16];   /* scratch for "CMD.BAT" fallback */
    uint16_t i, j, k;
    uint16_t ret;
    uint16_t rh;         /* stdout redirect handle (raw kernel); NO_HANDLE = none */
    int      bat_fd;     /* batch file fd; -1 = not running */
    uint16_t bat_echo;   /* 1 = echo batch lines, 0 = silent */
    uint16_t oneshot;    /* 1 = running the PSP command tail; exit after */
    uint16_t have_line;  /* 1 = line[] already holds the next command */

    bat_fd   = -1;
    bat_echo = 1;

    /* Command tail from PSP 0x0082 (written by SYSCALL_EXEC):
     * "SHELL.COM <cmd>" runs <cmd> once (including a full .BAT) and exits. */
    {
        const char *tail = (const char *)0x0082;
        while (*tail == ' ') tail++;
        oneshot = (*tail != '\0') ? 1 : 0;
        if (oneshot) {
            uint16_t n = 0;
            while (tail[n] != '\0' && n < LINE_MAX - 1) {
                line[n] = tail[n];
                n++;
            }
            line[n] = '\0';
        }
    }
    have_line = oneshot;

    if (!oneshot)
        fputs("Harvac Shell v0.4\r\n");

    for (;;) {
        rh = NO_HANDLE;

        /* --- Determine line source: pre-seeded, batch, or interactive --- */
        if (have_line) {
            have_line = 0;   /* line[] holds the PSP command tail */
        } else {
        if (bat_fd >= 0) {
            if (read_bat_line(bat_fd, line, LINE_MAX) == 0) {
                /* EOF: close batch, fall through to interactive */
                close(bat_fd);
                bat_fd = -1;
            }
        }
        if (bat_fd < 0) {
            /* One-shot command (and any batch it started) finished */
            if (oneshot)
                break;
            /* Interactive mode: print prompt, read line */
            getcwd(cwd, sizeof(cwd));
            fputs("\r\n");
            fputs(cwd);
            fputs("> ");
            gets(line, LINE_MAX);
        } else {
            /* Batch mode: handle '@' per-line suppress and echo */
            uint16_t li = 0;
            uint16_t echo_this = bat_echo;
            if (line[0] == '@') {
                echo_this = 0;
                /* shift line left by 1 to remove '@' */
                while (line[li]) { line[li] = line[li + 1]; li++; }
            }
            if (echo_this) {
                getcwd(cwd, sizeof(cwd));
                fputs(cwd);
                fputs("> ");
                fputs(line);
                fputs("\r\n");
            }
        }
        }   /* end line-source selection (have_line) */

        /* Skip leading spaces */
        for (i = 0; line[i] == ' '; i++)
            ;

        if (line[i] == '\0')
            continue;

        /* ─── Pipe detection: "cmd1 | cmd2 [| cmd3] [> file]" ─── */
        {
            /* Temp file paths for up to 3 pipe stages */
            static char ptmp0[] = "/TMP/PIPE0";
            static char ptmp1[] = "/TMP/PIPE1";
            static char ptmp2[] = "/TMP/PIPE2";
            char *ptmp[3];
            char *stg[4];       /* stage start pointers into line[] */
            int   ns;           /* number of stages */
            uint16_t pk;
            int has_pipe;

            ptmp[0] = ptmp0;
            ptmp[1] = ptmp1;
            ptmp[2] = ptmp2;

            /* Quick check for '|' */
            has_pipe = 0;
            for (pk = i; line[pk]; pk++) {
                if (line[pk] == '|') { has_pipe = 1; break; }
            }

            if (has_pipe) {
                /* Split line on '|' */
                ns = 0;
                stg[ns++] = line + i;
                for (pk = i; line[pk]; pk++) {
                    if (line[pk] == '|') {
                        uint16_t e = pk;
                        while (e > i && line[e - 1] == ' ') e--;
                        line[e] = '\0';
                        line[pk] = '\0';
                        for (pk++; line[pk] == ' '; pk++) ;
                        if (ns < 4 && line[pk])
                            stg[ns++] = line + pk;
                        pk--;
                    }
                }

                if (ns > 1) {
                    int ps;
                    uint16_t wh, srh, rdh;
                    static char empty_str[] = "";

                    for (ps = 0; ps < ns; ps++) {
                        char *sl = stg[ps];
                        uint16_t ci, cj, cn, ki;
                        char scmd[16];
                        char *sa;

                        /* Parse command word from stage */
                        ci = 0;
                        while (sl[ci] == ' ') ci++;
                        for (cj = ci; sl[cj] && sl[cj] != ' '; cj++) ;
                        cn = cj - ci;
                        if (cn > 15) cn = 15;
                        for (ki = 0; ki < cn; ki++) scmd[ki] = sl[ci + ki];
                        scmd[cn] = '\0';

                        /* Skip spaces to args */
                        while (sl[cj] == ' ') cj++;
                        sa = sl[cj] ? sl + cj : empty_str;

                        /* Handle '>' redirect on the last stage */
                        rdh = NO_HANDLE;
                        if (ps == ns - 1) {
                            char *gp = sa;
                            while (*gp && *gp != '>') gp++;
                            if (*gp == '>') {
                                char *gt = gp;
                                char *rfn;
                                while (gt > sa && gt[-1] == ' ') gt--;
                                *gt = '\0';
                                sa = (sa == gt) ? empty_str : sa;
                                gp++;
                                while (*gp == ' ') gp++;
                                rfn = gp;
                                if (*rfn) {
                                    char *rend = rfn;
                                    while (*rend && *rend != ' ') rend++;
                                    *rend = '\0';
                                    syscall_int40(SYSCALL_DELETE, 0, (uint16_t)rfn, 0, 0, 0, 0);
                                    rdh = syscall_int40(SYSCALL_CREATE, 0, (uint16_t)rfn, 0, 0, 0, 0);
                                    if (rdh < 16)
                                        syscall_int40(SYSCALL_SET_STDOUT, 0, rdh, 0, 0, 0, 0);
                                }
                            }
                        }

                        /* Non-last stage: stdout → temp pipe file */
                        wh = NO_HANDLE;
                        if (ps < ns - 1) {
                            syscall_int40(SYSCALL_DELETE, 0, (uint16_t)ptmp[ps], 0, 0, 0, 0);
                            wh = syscall_int40(SYSCALL_CREATE, 0, (uint16_t)ptmp[ps], 0, 0, 0, 0);
                            if (wh < 16)
                                syscall_int40(SYSCALL_SET_STDOUT, 0, wh, 0, 0, 0, 0);
                        }

                        /* Non-first stage: stdin ← previous temp pipe file */
                        srh = NO_HANDLE;
                        if (ps > 0) {
                            srh = syscall_int40(SYSCALL_OPEN, 0, (uint16_t)ptmp[ps - 1], 0, 0, 0, 0);
                            if (srh < 16)
                                syscall_int40(SYSCALL_SET_STDIN, 0, srh, 0, 0, 0, 0);
                        }

                        /* Execute stage as external command (with glob expansion) */
                        if (scmd[0])
                            syscall_int40(SYSCALL_EXEC, 0,
                                          (uint16_t)scmd,
                                          (uint16_t)glob_expand_args(sa),
                                          0, 0, 0);

                        /* Clear and close stdout redirect */
                        if (wh < 16) {
                            syscall_int40(SYSCALL_SET_STDOUT, 0, (uint16_t)NO_HANDLE, 0, 0, 0, 0);
                            syscall_int40(SYSCALL_CLOSE, 0, wh, 0, 0, 0, 0);
                        }
                        if (rdh < 16) {
                            syscall_int40(SYSCALL_SET_STDOUT, 0, (uint16_t)NO_HANDLE, 0, 0, 0, 0);
                            syscall_int40(SYSCALL_CLOSE, 0, rdh, 0, 0, 0, 0);
                        }

                        /* Clear stdin redirect and delete consumed temp file */
                        if (srh < 16) {
                            syscall_int40(SYSCALL_SET_STDIN, 0, (uint16_t)NO_HANDLE, 0, 0, 0, 0);
                            syscall_int40(SYSCALL_CLOSE, 0, srh, 0, 0, 0, 0);
                            syscall_int40(SYSCALL_DELETE, 0, (uint16_t)ptmp[ps - 1], 0, 0, 0, 0);
                        }
                    }
                    goto after_exec;
                }
            }
        }   /* end pipe detection */

        /* Scan for '>' redirect operator */
        for (k = i; line[k] && line[k] != '>'; k++)
            ;
        if (line[k] == '>') {
            uint16_t gt = k;
            char *redir;

            /* Null-terminate at '>'; trim trailing spaces from command portion */
            line[gt] = '\0';
            for (k = gt; k > i && line[k - 1] == ' '; k--)
                line[k - 1] = '\0';

            /* Find filename start after '>' (skip spaces) */
            for (k = gt + 1; line[k] == ' '; k++)
                ;
            redir = (line[k] != '\0') ? (line + k) : 0;

            /* Null-terminate filename at next space */
            if (redir) {
                for (k = (uint16_t)(redir - line); line[k] && line[k] != ' '; k++)
                    ;
                line[k] = '\0';
            }

            /* Delete any existing file (ignore result), then create via raw
             * kernel handle so we can pass it to SYSCALL_SET_STDOUT. */
            if (redir && *redir != '\0') {
                syscall_int40(SYSCALL_DELETE, 0, (uint16_t)redir, 0, 0, 0, 0);
                rh = syscall_int40(SYSCALL_CREATE, 0, (uint16_t)redir, 0, 0, 0, 0);
                if (rh >= 16) {
                    fputs("Cannot create redirect file\r\n");
                    continue;
                }
                syscall_int40(SYSCALL_SET_STDOUT, 0, rh, 0, 0, 0, 0);
            }
        }

        /* Find end of command word (space or null) */
        for (j = i; line[j] && line[j] != ' '; j++)
            ;
        if (line[j] == ' ') {
            line[j] = '\0';
            /* Advance past spaces to find argument */
            for (j++; line[j] == ' '; j++)
                ;
        }

        /* Dispatch built-in commands */
        if (strcmp(line + i, "help") == 0) {
            cmd_help();
        } else if (strcmp(line + i, "echo") == 0) {
            if (strcasecmp(line + j, "on") == 0) {
                bat_echo = 1;
            } else if (strcasecmp(line + j, "off") == 0) {
                bat_echo = 0;
            } else {
                if (line[j] != '\0')
                    fputs(line + j);
                fputs("\r\n");
            }
        } else if (strcmp(line + i, "clear") == 0) {
            syscall_int40(SYSCALL_CLEAR_SCREEN, 0, 0, 0, 0, 0, 0);
        } else if (strcmp(line + i, "cd") == 0) {
            cmd_cd(line + j);
        } else if (strcmp(line + i, "pwd") == 0) {
            cmd_pwd();
        } else if (strcmp(line + i, "exit") == 0) {
            fputs("Bye!\r\n");
            if (bat_fd >= 0) {
                close(bat_fd);
                bat_fd = -1;
            }
            break;
        } else {
            /* External command or batch script.
             *
             * If name ends in .bat: open as batch (do not exec).
             * Otherwise: try SYSCALL_EXEC; if not found and name has no
             * extension, try appending .bat before giving up. */
            if (ends_with(line + i, ".bat")) {
                /* Explicit .bat extension */
                int h = open(line + i, O_RDONLY);
                if (h >= 0) {
                    if (bat_fd >= 0)
                        close(bat_fd);
                    bat_fd = h;
                    bat_echo = 1;
                } else {
                    fputs("Not found: ");
                    fputs(line + i);
                    fputs("\r\n");
                }
            } else {
                /* Try exec as .COM (with glob expansion on args) */
                ret = syscall_int40(SYSCALL_EXEC, 0,
                                    (uint16_t)(line + i),
                                    (uint16_t)glob_expand_args(line + j),
                                    0, 0, 0);
                if (ret != 0) {
                    /* EXEC failed: if no extension in command, try .BAT */
                    if (strchr(line + i, '.') == (char *)0) {
                        int h;
                        uint16_t blen = 0;
                        const char *cmd = line + i;
                        while (cmd[blen] && blen < 11) {
                            bat_name[blen] = cmd[blen];
                            blen++;
                        }
                        bat_name[blen++] = '.';
                        bat_name[blen++] = 'b';
                        bat_name[blen++] = 'a';
                        bat_name[blen++] = 't';
                        bat_name[blen] = '\0';
                        h = open(bat_name, O_RDONLY);
                        if (h >= 0) {
                            if (bat_fd >= 0)
                                close(bat_fd);
                            bat_fd = h;
                            bat_echo = 1;
                            goto after_exec;
                        }
                    }
                    /* Unknown command: clear redirect before printing error */
                    if (rh != NO_HANDLE) {
                        syscall_int40(SYSCALL_SET_STDOUT, 0,
                                      (uint16_t)NO_HANDLE, 0, 0, 0, 0);
                        syscall_int40(SYSCALL_CLOSE, 0, rh, 0, 0, 0, 0);
                        rh = NO_HANDLE;
                    }
                    fputs("Unknown command: ");
                    fputs(line + i);
                    fputs("\r\n");
                }
            }
        }

        after_exec:
        /* Close redirect if still active */
        if (rh != NO_HANDLE) {
            syscall_int40(SYSCALL_SET_STDOUT, 0, (uint16_t)NO_HANDLE, 0, 0, 0, 0);
            syscall_int40(SYSCALL_CLOSE, 0, rh, 0, 0, 0, 0);
        }
    }

    _exit(0);
}

/* glob_expand_args: expand glob patterns (* and ?) in an args string.
 * Tokens that contain no glob chars are passed through unchanged.
 * Tokens with * or ? are matched against entries in the current directory;
 * matches are substituted space-separated. If no entry matches, the literal
 * token is kept (nullglob-off semantics).
 * Tokens containing '/' are never expanded (only bare names are matched).
 * Returns pointer to the static _glob_buf, or to the original args if no
 * expansion was needed. */
static const char *glob_expand_args(const char *args)
{
    const char *p;
    uint16_t has;
    uint16_t oi;
    char tok[NAME_MAX + 2];  /* one FAT16 name + NUL */
    uint16_t ti;
    DIR *dp;
    struct dirent *de;
    uint16_t found;
    uint16_t nl;

    /* Quick scan: if no glob chars at all, return as-is */
    has = 0;
    for (p = args; *p; p++) {
        if (*p == '*' || *p == '?') { has = 1; break; }
    }
    if (!has) return args;

    oi = 0;
    _glob_buf[0] = '\0';
    p = args;

    while (*p) {
        /* Skip leading spaces */
        while (*p == ' ') p++;
        if (!*p) break;

        /* Collect one token */
        ti = 0;
        while (*p && *p != ' ' && ti < sizeof(tok) - 1)
            tok[ti++] = *p++;
        tok[ti] = '\0';
        if (ti == 0) continue;

        /* Expand only if token has glob chars AND no path separator */
        if (has_glob(tok) && strchr(tok, '/') == (char *)0) {
            dp = opendir(".");
            found = 0;
            if (dp) {
                while ((de = readdir(dp)) != NULL) {
                    if (!fnmatch(tok, de->d_name)) continue;
                    nl = (uint16_t)strlen(de->d_name);
                    if (oi + nl + 2 < GLOB_BUF_SIZE && oi + nl + 1 < PSP_ARG_MAX) {
                        if (oi > 0) _glob_buf[oi++] = ' ';
                        strcpy(_glob_buf + oi, de->d_name);
                        oi += nl;
                        found = 1;
                    }
                }
                closedir(dp);
            }
            if (!found) {
                /* No match: keep literal token */
                if (oi + ti + 2 < GLOB_BUF_SIZE && oi + ti + 1 < PSP_ARG_MAX) {
                    if (oi > 0) _glob_buf[oi++] = ' ';
                    strcpy(_glob_buf + oi, tok);
                    oi += ti;
                }
            }
        } else {
            /* No glob: pass through */
            if (oi + ti + 2 < GLOB_BUF_SIZE && oi + ti + 1 < PSP_ARG_MAX) {
                if (oi > 0) _glob_buf[oi++] = ' ';
                strcpy(_glob_buf + oi, tok);
                oi += ti;
            }
        }
    }
    _glob_buf[oi] = '\0';
    return _glob_buf;
}

/* read_bat_line: read next line from batch file fd into buf.
 * Skips \r, stops at \n or EOF. Returns 1 if any bytes read, 0 on EOF. */
static int read_bat_line(int fd, char *buf, uint16_t maxlen)
{
    char c;
    uint16_t n = 0;
    int got = 0;
    while (n < maxlen - 1) {
        if (read(fd, &c, 1) <= 0)
            break;
        got = 1;
        if ((uint8_t)c == '\r') continue;
        if ((uint8_t)c == '\n') break;
        buf[n++] = c;
    }
    buf[n] = '\0';
    return got;
}

/* cmd_help: list available commands */
static void cmd_help(void)
{
    fputs("Built-in commands:\r\n");
    fputs("  help          - Show this help\r\n");
    fputs("  echo [text]   - Print text (echo on/off controls batch echo)\r\n");
    fputs("  clear         - Clear screen\r\n");
    fputs("  cd [path]     - Change directory\r\n");
    fputs("  pwd           - Print working directory\r\n");
    fputs("  exit          - Exit shell\r\n");
    fputs("  <name>        - Execute BIN/<name>.COM or <name>.BAT\r\n");
}

/* cmd_cd: change directory */
static void cmd_cd(const char *arg)
{
    if (!arg || *arg == '\0')
        arg = "/";
    if (chdir(arg) != 0)
        fputs("Directory not found\r\n");
}

/* cmd_pwd: print working directory */
static void cmd_pwd(void)
{
    char buf[64];
    getcwd(buf, sizeof(buf));
    fputs(buf);
    fputs("\r\n");
}
