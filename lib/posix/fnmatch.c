/* fnmatch.c -- Wildcard pattern matching for FAT16 filenames.
 * Case-insensitive iterative (non-recursive) glob match. */

#include "fnmatch.h"

/* Case-insensitive character equality (A-Z folds to a-z). */
static int ci_eq(char a, char b)
{
    if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
    if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
    return a == b;
}

/* fnmatch: return 1 if name matches pattern.
 * Uses the classic non-recursive "star/np" backtracking algorithm. */
int fnmatch(const char *pat, const char *name)
{
    const char *star = (const char *)0;
    const char *np   = name;

    while (*name) {
        if (*pat == '*') {
            star = pat++;
            np   = name;
        } else if (*pat == '?' || ci_eq(*pat, *name)) {
            pat++;
            name++;
        } else if (star) {
            pat  = star + 1;
            name = ++np;
        } else {
            return 0;
        }
    }
    while (*pat == '*') pat++;
    return *pat == '\0';
}

/* has_glob: return 1 if str contains * or ?, 0 otherwise. */
int has_glob(const char *str)
{
    while (*str) {
        if (*str == '*' || *str == '?') return 1;
        str++;
    }
    return 0;
}
