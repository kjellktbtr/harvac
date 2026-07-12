---
title: POSIX-like Userspace API (lib/posix)
type: code-map
sources:
  - lib/include/string.h
  - lib/include/errno.h
  - lib/include/fcntl.h
  - lib/include/unistd.h
  - lib/include/dirent.h
  - lib/include/sys/stat.h
  - lib/include/stdio.h
  - lib/include/stdlib.h
  - lib/posix/string.c
  - lib/posix/errno.c
  - lib/posix/fcntl.c
  - lib/posix/unistd.c
  - lib/posix/dirent.c
  - lib/posix/stat.c
  - lib/posix/stdio.c
  - lib/posix/stdlib.c
  - lib/posix/fd.h
  - lib/posix/fd.c
related:
  - "[[harva-dev-kit]]"
  - "[[syscall-dispatch]]"
  - "[[user-apps]]"
created: 2026-07-12
updated: 2026-07-12
confidence: high
---

# POSIX-like Userspace API

`lib/posix/` provides a freestanding POSIX-compatible C runtime for Harvac `.COM`
user programs. Headers live in `lib/include/` and are compiled into every app via
the `-i=lib/include` flag in `build.py`.

## File Descriptor Model

A private table (`lib/posix/fd.c`) maps libc fd 0–15 to kernel handle or console:

| fd | Kind       | Routes to |
|----|-----------|-----------|
| 0  | FK_CONSOLE | `SYSCALL_READ_STDIN` |
| 1  | FK_CONSOLE | `SYSCALL_WRITE_CHAR` loop |
| 2  | FK_CONSOLE | `SYSCALL_WRITE_STDERR` |
| 3–15 | FK_FILE / FK_DEVICE | kernel file handle / serial |

`open("/dev/com1")` (or `"CON"`, `"/dev/tty"`) allocates a FK_DEVICE fd that
routes `read`/`write` to the kernel's OF_KIND_SERIAL path (serial_getchar /
serial_putchar). See [[syscall-dispatch]] for the kernel side.

## Headers

### `string.h`
Standard string functions using real POSIX names:
`strlen, strcmp, strncmp, strcasecmp, strcpy, strncpy, strcat, strncat,
strchr, memcpy, memmove, memset, memcmp`

Extensions (Harvac-specific):
- `ends_with(str, suffix)` — case-insensitive suffix test
- `utoa(val, buf)` — `uint16_t` to decimal string
- `u32toa(val, buf)` — `uint32_t` to decimal string
- `utoa_pad2(val, buf)` — zero-padded 2-digit string (for dates/times)
- `format_size(size, buf)` — right-aligned 5-char size with K suffix
- `atoi(s)` — string to `int`

### `fcntl.h` / `fcntl.c`
```c
int open(const char *path, int flags);  /* returns fd, -1 on error */
int creat(const char *path);
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  4
#define O_APPEND 8
```

### `unistd.h` / `unistd.c`
```c
int    read(int fd, void *buf, uint16_t count);
int    write(int fd, const void *buf, uint16_t count);
int    close(int fd);
off_t  lseek(int fd, off_t offset, int whence); /* SEEK_SET/CUR/END */
int    isatty(int fd);
void   _exit(int code);
int    getpid(void);
int    spawn(const char *cmd, const char *args);

/* Filesystem */
int    unlink(const char *path);
int    rmdir(const char *path);
int    mkdir(const char *path);
int    chdir(const char *path);
char  *getcwd(char *buf, uint16_t size);
int    rename(const char *old, const char *new);
```

`off_t` is `int32_t` (32-bit signed), matching FAT16 file sizes up to 4 GB.

### `dirent.h` / `dirent.c`
POSIX-style directory iteration (hides raw FAT16 byte layout):
```c
struct dirent { char d_name[13]; uint8_t d_attr; uint16_t d_date, d_time; uint32_t d_size; };
DIR *opendir(const char *path);   /* always opens CWD, path ignored */
struct dirent *readdir(DIR *dp);
int closedir(DIR *dp);
```
Skips deleted entries (0xE5), volume labels, and "." entries automatically.
Pool of 2 static `DIR` slots; nesting is not supported.

### `sys/stat.h` / `stat.c`
```c
struct stat { uint32_t st_size; uint8_t st_attr; uint16_t st_mtime, st_mdate; };
int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);  /* not yet implemented */
int mkdir_p(const char *path);
#define S_ISDIR(a)  ((a) & 0x10)
#define S_ISREG(a)  (!((a) & 0x18))
```

### `stdio.h` / `stdio.c`
Minimal console I/O:
```c
int    putchar(int c);
int    puts(const char *s);
int    fputs(const char *s);       /* writes to stdout */
char  *gets(char *buf, int max);
int    getchar(void);
void   eputstr(const char *s);     /* writes to stderr */
```

### `stdlib.h` / `stdlib.c`
```c
void     exit(int code);
int      abs(int x);
uint16_t alloc_paras(uint16_t n);  /* SYSCALL_ALLOC wrapper */
```

### `errno.h` / `errno.c`
```c
extern int errno;
int set_errno(unsigned err);   /* maps kernel ERR_* → POSIX code */
```
Failing wrappers set `errno` and return -1. Codes: `ENOENT=2, EACCES=13,
EBADF=9, EINVAL=22, ENOMEM=12, EIO=5, EEXIST=17, EMFILE=24, ENOSYS=38`.

## Writing a .COM against this library

```c
#include "types.h"
#include "stdio.h"
#include "fcntl.h"
#include "unistd.h"

void __far _main(void)          /* _main must be the first function */
{
    int fd = open("README.TXT", O_RDONLY);
    if (fd < 0) { eputstr("not found\r\n"); return; }
    /* ... read/write/close ... */
}
```

Compile: `wcc -0 -ms -os -s -zl -d0 -bt=dos -i=src/include -i=lib/include`
Link: `wlink format raw bin option offset=0x100 name OUT.COM file main.obj file lib_*.obj`

See `build.py:link_app()` for the actual flags.

## Kernel requirements

- `SYSCALL_WRITE_STDERR (0x04)` — stderr writes (never redirected)
- `SYSCALL_SEEK (0x14)` — supports `AL` = whence (SEEK_SET/CUR/END)
- `SYSCALL_TELL (0x19)` — returns 32-bit file position
- `SYSCALL_GET_PID (0x42)` — returns process slot id
- Serial device fd routing in OPEN/READ/WRITE via `OF_KIND_SERIAL` flag

All in `src/kernel/syscalls.c`.
