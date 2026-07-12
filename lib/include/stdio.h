/* stdio.h -- Minimal console I/O wrappers for HarvaC userspace.
 * Routes through fd 1 (stdout) and fd 0 (stdin). No FILE buffering. */
#ifndef STDIO_H
#define STDIO_H

#include "types.h"

/* Write a single character to stdout. Returns the char on success. */
int     putchar(int c);

/* Write a null-terminated string followed by '\n' to stdout.
 * Returns 0 on success, -1 on error. */
int     puts(const char *s);

/* Write a null-terminated string to stdout (no trailing newline).
 * Returns 0 on success, -1 on error. */
int     fputs(const char *s);

/* Read a line from stdin into buf (max-1 chars + NUL).
 * Returns buf on success, NULL at end/error. */
char   *gets(char *buf, uint16_t max);

/* Read a single character from stdin (blocking). Returns char value. */
int     getchar(void);

/* Write string to stderr (always goes to console, never redirected). */
int     eputstr(const char *s);

/* Read one line from fd into buf (at most max-1 chars + NUL).
 * Strips '\r'; stops at '\n' or EOF.
 * Returns the number of characters stored (without the NUL), or -1 at EOF
 * when no characters were read.
 * File fds are read in 512-byte chunks internally for efficiency. */
int     getline_fd(int fd, char *buf, uint16_t max);

/* Returns 1 if STDIN_FILENO is connected to the keyboard (no pipe redirect),
 * 0 if stdin has been redirected to a file (pipe). */
int     isatty(int fd);

#endif /* STDIO_H */
