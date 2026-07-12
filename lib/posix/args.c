/* args.c -- Minimal argv parser for HarvaC userspace .COM programs.
 * The PSP command tail lives at near address 0x0082 and is writable.
 * We tokenize it by inserting NUL bytes so argv[] pointers are valid
 * for the lifetime of the process. */

#include "types.h"
#include "args.h"

int args_parse(char **argv, int max)
{
    char *p = (char *)0x0082;   /* PSP argv area */
    int argc = 0;

    while (*p == ' ') p++;      /* skip leading spaces */

    while (*p != '\0' && argc < max) {
        argv[argc++] = p;
        /* advance to end of token */
        while (*p != '\0' && *p != ' ') p++;
        if (*p == ' ') {
            *p = '\0';          /* NUL-terminate token in place */
            p++;
            while (*p == ' ') p++;
        }
    }
    return argc;
}

int args_has_flag(int argc, char **argv, char c)
{
    int i;
    for (i = 0; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == c && argv[i][2] == '\0')
            return 1;
    }
    return 0;
}

const char *args_flag_val(int argc, char **argv, char c)
{
    int i;
    for (i = 0; i < argc - 1; i++) {
        if (argv[i][0] == '-' && argv[i][1] == c && argv[i][2] == '\0')
            return argv[i + 1];
    }
    return (const char *)0;
}

int args_first_nonoption(int argc, char **argv)
{
    int i;
    for (i = 0; i < argc; i++) {
        if (argv[i][0] != '-')
            return i;
    }
    return argc;
}
