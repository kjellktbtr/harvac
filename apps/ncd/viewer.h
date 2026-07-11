#ifndef VIEWER_H
#define VIEWER_H

#include "ncd.h"

/* Open a file in the fullscreen viewer. Blocks until F3 or Esc. */
void viewer_open(const char *path);

#endif /* VIEWER_H */
