/* args.h -- Minimal argv parser for HarvaC userspace .COM programs.
 * Tokenizes the PSP command tail at 0x0082 in place (writes NUL bytes).
 * argv[0] is the first argument (the program name is not included). */
#ifndef ARGS_H
#define ARGS_H

#include "types.h"

/* Parse the PSP command tail into argv[].
 * Splits on spaces; NUL-terminates each token in place.
 * Returns argc (number of tokens, 0 if the tail is empty).
 * max caps the number of pointers stored. */
int args_parse(char **argv, int max);

/* Return 1 if any argv[i] equals "-c" (e.g. '-r', '-i'). */
int args_has_flag(int argc, char **argv, char c);

/* Return the token immediately after "-c", or NULL if not present or last. */
const char *args_flag_val(int argc, char **argv, char c);

/* Return the index of the first non-flag argument (argv[i][0] != '-'),
 * or argc if none. */
int args_first_nonoption(int argc, char **argv);

#endif /* ARGS_H */
