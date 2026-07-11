/* shell.c -- User-space REPL shell for HarvaOS
 * .COM entry point, uses INT 0x40 syscalls for all I/O.
 * Built-in commands loop within one SHELL.COM invocation.
 * External commands use SYSCALL_EXEC; since EXEC now returns (DOS model),
 * the shell is fully resident: it stays in memory while children run at
 * CHILD_SEGMENT (0x6000).
 * "exit" calls SYSCALL_EXIT; root-level exit restarts boot_shell.
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

/* Line buffer size */
#define LINE_MAX    128

/* Sentinel: no active redirect or batch file */
#define NO_HANDLE  0xFFFFu

/* Forward declarations */
static void putstr(const char *s);
static void putch(char c);
static void read_line(char *buf, uint16_t maxlen);
static int str_eq(const char *a, const char *b);
static int str_eqi(const char *a, const char *b);
static uint16_t str_len(const char *s);
static int has_ext(const char *s);
static int is_bat_ext(const char *s);
static uint16_t read_bat_line(uint16_t fh, char *buf, uint16_t maxlen);
static void cmd_help(void);
static void cmd_cd(const char *arg);
static void cmd_pwd(void);

/* _main: entry point (__far so retf returns to kernel) */
void __far _main(void)
{
    char line[LINE_MAX];
    char cwd[64];
    char bat_name[16];   /* scratch for "CMD.BAT" fallback */
    uint16_t i, j, k;
    uint16_t ret;
    uint16_t rh;         /* stdout redirect handle; NO_HANDLE = none */
    uint16_t bat_h;      /* batch file handle; NO_HANDLE = not running */
    uint16_t bat_echo;   /* 1 = echo batch lines, 0 = silent */

    putstr("Harvac Shell v0.4\r\n");

    bat_h    = NO_HANDLE;
    bat_echo = 1;

    for (;;) {
        rh = NO_HANDLE;

        /* --- Determine line source: batch or interactive --- */
        if (bat_h != NO_HANDLE) {
            if (read_bat_line(bat_h, line, LINE_MAX) == 0) {
                /* EOF: close batch, fall through to interactive */
                syscall_int40(SYSCALL_CLOSE, 0, bat_h, 0, 0, 0, 0);
                bat_h = NO_HANDLE;
            }
        }
        if (bat_h == NO_HANDLE) {
            /* Interactive mode: print prompt, read line */
            syscall_int40(SYSCALL_GETCWD, 0, 0, (uint16_t)cwd, 64, 0, 0);
            putstr("\r\n");
            putstr(cwd);
            putstr("> ");
            read_line(line, LINE_MAX);
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
                syscall_int40(SYSCALL_GETCWD, 0, 0, (uint16_t)cwd, 64, 0, 0);
                putstr(cwd);
                putstr("> ");
                putstr(line);
                putstr("\r\n");
            }
        }

        /* Skip leading spaces */
        for (i = 0; line[i] == ' '; i++)
            ;

        if (line[i] == '\0')
            continue;

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

            /* Delete any existing file (ignore result), then create */
            if (redir && *redir != '\0') {
                syscall_int40(SYSCALL_DELETE, 0, (uint16_t)redir, 0, 0, 0, 0);
                rh = syscall_int40(SYSCALL_CREATE, 0, (uint16_t)redir, 0, 0, 0, 0);
                if (rh >= 16) {
                    putstr("Cannot create redirect file\r\n");
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
        if (str_eq(line + i, "help")) {
            cmd_help();
        } else if (str_eq(line + i, "echo")) {
            /* echo on/off controls batch line echoing */
            if (str_eqi(line + j, "on")) {
                bat_echo = 1;
            } else if (str_eqi(line + j, "off")) {
                bat_echo = 0;
            } else {
                if (line[j] != '\0')
                    putstr(line + j);
                putstr("\r\n");
            }
        } else if (str_eq(line + i, "clear")) {
            syscall_int40(SYSCALL_CLEAR_SCREEN, 0, 0, 0, 0, 0, 0);
        } else if (str_eq(line + i, "cd")) {
            cmd_cd(line + j);
        } else if (str_eq(line + i, "pwd")) {
            cmd_pwd();
        } else if (str_eq(line + i, "exit")) {
            putstr("Bye!\r\n");
            if (bat_h != NO_HANDLE) {
                syscall_int40(SYSCALL_CLOSE, 0, bat_h, 0, 0, 0, 0);
                bat_h = NO_HANDLE;
            }
            break;
        } else {
            /* External command or batch script.
             *
             * If name ends in .bat: open as batch (do not exec).
             * Otherwise: try SYSCALL_EXEC; if not found and name has no
             * extension, try appending .bat before giving up. */
            if (is_bat_ext(line + i)) {
                /* Explicit .bat extension */
                uint16_t h = syscall_int40(SYSCALL_OPEN, 0,
                                           (uint16_t)(line + i), 0, 0, 0, 0);
                if (h < 16) {
                    if (bat_h != NO_HANDLE)
                        syscall_int40(SYSCALL_CLOSE, 0, bat_h, 0, 0, 0, 0);
                    bat_h = h;
                    bat_echo = 1;
                } else {
                    putstr("Not found: ");
                    putstr(line + i);
                    putstr("\r\n");
                }
            } else {
                /* Try exec as .COM */
                ret = syscall_int40(SYSCALL_EXEC, 0,
                                    (uint16_t)(line + i),
                                    (uint16_t)(line + j), 0, 0, 0);
                if (ret != 0) {
                    /* EXEC failed: if no extension in command, try .BAT */
                    if (!has_ext(line + i)) {
                        uint16_t h;
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
                        h = syscall_int40(SYSCALL_OPEN, 0,
                                          (uint16_t)bat_name, 0, 0, 0, 0);
                        if (h < 16) {
                            if (bat_h != NO_HANDLE)
                                syscall_int40(SYSCALL_CLOSE, 0, bat_h, 0, 0, 0, 0);
                            bat_h = h;
                            bat_echo = 1;
                            /* Redirect cleanup below; no error message */
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
                    putstr("Unknown command: ");
                    putstr(line + i);
                    putstr("\r\n");
                }
            }
        }

        after_exec:
        /* Close redirect if still active (built-ins, successful EXEC, or batch launch) */
        if (rh != NO_HANDLE) {
            syscall_int40(SYSCALL_SET_STDOUT, 0, (uint16_t)NO_HANDLE, 0, 0, 0, 0);
            syscall_int40(SYSCALL_CLOSE, 0, rh, 0, 0, 0, 0);
        }
    }

    syscall_int40(SYSCALL_EXIT, 0, 0, 0, 0, 0, 0);
}

/* putstr: write NUL-terminated string via syscall */
static void putstr(const char *s)
{
    syscall_int40(SYSCALL_WRITE_STDOUT, 0, 0, 0, 0, (uint16_t)s, 0);
}

/* putch: write single character via syscall */
static void putch(char c)
{
    syscall_int40(SYSCALL_WRITE_CHAR, (uint8_t)c, 0, 0, 0, 0, 0);
}

/* read_line: read one line from serial/keyboard via syscall */
static void read_line(char *buf, uint16_t maxlen)
{
    syscall_int40(SYSCALL_READ_STDIN, 0, 0, 0, 0, (uint16_t)buf, maxlen);
}

/* str_eq: exact string comparison (0 = no match, 1 = match) */
static int str_eq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (*a == '\0' && *b == '\0');
}

/* str_eqi: case-insensitive string comparison */
static int str_eqi(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

/* str_len: string length */
static uint16_t str_len(const char *s)
{
    uint16_t n = 0;
    while (s[n]) n++;
    return n;
}

/* has_ext: returns 1 if string contains a '.' */
static int has_ext(const char *s)
{
    while (*s) { if (*s == '.') return 1; s++; }
    return 0;
}

/* is_bat_ext: returns 1 if string ends in .bat or .BAT (case-insensitive) */
static int is_bat_ext(const char *s)
{
    uint16_t len = str_len(s);
    if (len < 4) return 0;
    return (s[len - 4] == '.' &&
            (s[len - 3] == 'b' || s[len - 3] == 'B') &&
            (s[len - 2] == 'a' || s[len - 2] == 'A') &&
            (s[len - 1] == 't' || s[len - 1] == 'T'));
}

/* read_bat_line: read next line from batch file into buf.
 * Skips \r, stops at \n or EOF. Returns 1 if any bytes read, 0 on EOF. */
static uint16_t read_bat_line(uint16_t fh, char *buf, uint16_t maxlen)
{
    char c;
    uint16_t n = 0;
    uint16_t got = 0;
    while (n < maxlen - 1) {
        /* SYSCALL_READ: BX=handle, CX=buf_ptr, DX=count */
        if (syscall_int40(SYSCALL_READ, 0, fh, (uint16_t)&c, 1, 0, 0) == 0)
            break;  /* EOF */
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
    putstr("Built-in commands:\r\n");
    putstr("  help          - Show this help\r\n");
    putstr("  echo [text]   - Print text (echo on/off controls batch echo)\r\n");
    putstr("  clear         - Clear screen\r\n");
    putstr("  cd [path]     - Change directory\r\n");
    putstr("  pwd           - Print working directory\r\n");
    putstr("  exit          - Exit shell\r\n");
    putstr("  <name>        - Execute BIN/<name>.COM or <name>.BAT\r\n");
}

/* cmd_cd: change directory via syscall */
static void cmd_cd(const char *arg)
{
    uint16_t ret;
    if (!arg || *arg == '\0')
        arg = "/";
    ret = syscall_int40(SYSCALL_CHDIR, 0, (uint16_t)arg, 0, 0, 0, 0);
    if (ret != 0)
        putstr("Directory not found\r\n");
}

/* cmd_pwd: print working directory via syscall */
static void cmd_pwd(void)
{
    char buf[64];
    syscall_int40(SYSCALL_GETCWD, 0, 0, (uint16_t)buf, 64, 0, 0);
    putstr(buf);
    putstr("\r\n");
}
