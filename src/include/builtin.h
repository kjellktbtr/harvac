#ifndef SHELL_BUILTIN_H
#define SHELL_BUILTIN_H

#include "types.h"
#include "drivers/fat16.h"

/* Set the filesystem for builtin directory operations */
void builtin_set_fs(fat16_fs_t *fs);

/* Parse and execute a command line */
void builtin_dispatch(const char *line);

#endif /* SHELL_BUILTIN_H */