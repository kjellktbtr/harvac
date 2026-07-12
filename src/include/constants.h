#ifndef CONSTANTS_H
#define CONSTANTS_H

/* ─── Syscall interrupt vector ─── */
#define SYSINT              0x40

/* ─── Syscall numbers ─── */
/* Console I/O (0x00-0x0F) */
#define SYSCALL_WRITE_STDOUT    0x00
#define SYSCALL_WRITE_CHAR      0x01
#define SYSCALL_READ_STDIN      0x02
#define SYSCALL_READ_CHAR       0x03
#define SYSCALL_WRITE_STDERR    0x04
#define SYSCALL_CLEAR_SCREEN    0x05
#define SYSCALL_SET_CURSOR      0x06
#define SYSCALL_GET_CURSOR      0x07
#define SYSCALL_SET_STDOUT      0x08  /* BX = file handle (0xFFFF to clear) */
#define SYSCALL_GET_SHIFT       0x09  /* returns modifier byte: shift|ctrl|alt */
#define SYSCALL_WRITE_VGA       0x0A  /* SI = string; VGA-only (never serial) */
#define SYSCALL_KEY_AVAILABLE   0x0B  /* nonzero if a key event is buffered
                                       * (keyboard only, never serial) */

/* File I/O (0x10-0x1F) */
#define SYSCALL_OPEN            0x10
#define SYSCALL_CLOSE           0x11
#define SYSCALL_READ            0x12
#define SYSCALL_WRITE           0x13
#define SYSCALL_SEEK            0x14
#define SYSCALL_CREATE          0x15
#define SYSCALL_DELETE          0x16
#define SYSCALL_STAT            0x17
#define SYSCALL_RENAME          0x18  /* SI = old name offset, DI = new name offset */

/* Directory (0x20-0x2F) */
#define SYSCALL_OPENDIR         0x20
#define SYSCALL_READDIR         0x21
#define SYSCALL_CLOSEDIR        0x22
#define SYSCALL_MKDIR           0x23
#define SYSCALL_RMDIR           0x24
#define SYSCALL_CHDIR           0x25
#define SYSCALL_GETCWD          0x26

/* Filesystem (0x30-0x3F) */
#define SYSCALL_MOUNT           0x30
#define SYSCALL_UNMOUNT         0x31
#define SYSCALL_LIST_MOUNTS     0x32

/* Process (0x40-0x4F) */
#define SYSCALL_EXEC            0x40
#define SYSCALL_EXIT            0x41
#define SYSCALL_GET_PID         0x42

/* Memory (0x50-0x5F) */
#define SYSCALL_ALLOC           0x50
#define SYSCALL_FREE            0x51
#define SYSCALL_MEM_INFO        0x52

/* Time (0x60-0x6F) */
#define SYSCALL_GET_TIME        0x60
#define SYSCALL_SET_TIME        0x61
#define SYSCALL_GET_DATE        0x62
#define SYSCALL_SET_DATE        0x63

/* Driver Mgmt (0x70-0x7F) */
#define SYSCALL_LOAD_DRIVER     0x70
#define SYSCALL_UNLOAD_DRIVER   0x71
#define SYSCALL_LIST_DRIVERS    0x72

/* Misc (0x80-0x8F) */
#define SYSCALL_REBOOT          0x80
#define SYSCALL_HALT            0x81
#define SYSCALL_GET_VERSION     0x82
#define SYSCALL_IOCTL           0x83

/* ─── File open flags ─── */
#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     4
#define O_APPEND    8

/* ─── Seek whence ─── */
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

/* ─── VGA text mode ─── */
#define VGA_COLS            80
#define VGA_ROWS            25
#define VGA_BUFFER          0xB8000
#define VGA_ATTR_NORMAL     0x07
#define VGA_ATTR_HIGHLIGHT  0x0F
#define VGA_ATTR_ERROR      0x0C
#define VGA_ATTR_STATUS     0x02

/* ─── Serial port ─── */
#define COM1_BASE       0x3F8
#define COM1_BAUD_GEN   0x3F9
#define COM1_BAUD_DIV   3    /* 115200 baud */

/* ─── PIC ─── */
#define PIC1_CMD        0x20
#define PIC1_DATA       0x21
#define PIC2_CMD        0xA0
#define PIC2_DATA       0xA1
#define PIC_EOI         0x20

/* ─── PIT ─── */
#define PIT_CHANNEL0    0x40
#define PIT_COMMAND     0x43

/* ─── Keyboard controller ─── */
#define KBC_CMD         0x64
#define KBC_DATA        0x60

/* ─── CMOS/RTC ─── */
#define CMOS_ADDR       0x70
#define CMOS_DATA       0x71

/* ─── Kernel memory layout ─── */
#define KERNEL_SEGMENT      0x2000
#define KERNEL_BASE         0x10000UL
#define STACK_TOP           0x9F000UL
#define MAX_CONVENTIONAL    0xA0000UL

/* Per-process allocation: 24 KB per exec'd .COM (code + BSS + stack).
 * Must exceed the largest app's near data + stack, otherwise its BSS runs
 * past the slot and collides with the far buffer it allocates at
 * child_seg+PROC_PARAS. EDIT.COM's near data reaches ~0x40E8 (just over
 * 16 KB), so 16 KB is too small: loading a file overwrote EDIT's globals
 * (menu_wrap_flag etc.) and hung its layout. 24 KB leaves headroom. */
#define PROC_PARAS          0x0600   /* 24 KB in paragraphs */
#define PROC_SP             0x5FFE   /* initial child SP (top of 24 KB) */

/* ─── FAT16 ─── */
#define FAT16_EOF       0xFFF8
#define FAT16_BAD       0xFFF7
#define FAT16_FREE      0x0000
#define FAT16_RESERVED  0x0001

/* ─── Directory attributes ─── */
#define DIR_ATTR_READONLY   0x01
#define DIR_ATTR_HIDDEN     0x02
#define DIR_ATTR_SYSTEM     0x04
#define DIR_ATTR_VOLUME     0x08
#define DIR_ATTR_DIRECTORY  0x10
#define DIR_ATTR_ARCHIVE    0x20

/* ─── Limits ─── */
#define MAX_OPEN_FILES  16
#define MAX_MOUNTS      8
#define MAX_DRIVERS     8

/* ─── Error codes ─── */
#define ERR_NONE            0
#define ERR_NOT_FOUND       1
#define ERR_ACCESS_DENIED   2
#define ERR_INVALID_HANDLE  3
#define ERR_INVALID_PARAM   4
#define ERR_NO_MEMORY       5
#define ERR_DISK_ERROR      6
#define ERR_FILE_EXISTS     7
#define ERR_TOO_MANY_FILES  8
#define ERR_INVALID_DRIVER  9
#define ERR_NOT_IMPLEMENTED 10

#endif /* CONSTANTS_H */
