#ifndef SHELL_H
#define SHELL_H

#include "types.h"

/* Maximum command line length */
#define SHELL_MAX_LINE   128

/* Shell prompt string */
#define SHELL_PROMPT     "Harvac> "

/* Initialize and run the shell REPL (never returns) */
void boot_shell(void);

#endif /* SHELL_H */