/* fnmatch.h -- Wildcard pattern matching for FAT16 filenames.
 * Supports * (any sequence) and ? (any single char).
 * Matching is case-insensitive to match FAT16 conventions. */
#ifndef FNMATCH_H
#define FNMATCH_H

/* Return 1 if name matches pattern, 0 if not.
 * Pattern metacharacters: * = any sequence of chars, ? = any single char.
 * Matching is case-insensitive (A-Z treated equal to a-z). */
int fnmatch(const char *pattern, const char *name);

/* Return 1 if str contains any glob metacharacter (* or ?), 0 otherwise. */
int has_glob(const char *str);

#endif /* FNMATCH_H */
