#ifndef PORT_IO_H
#define PORT_IO_H

#include "types.h"

/* Declarations for pragma aux functions */
unsigned char port_in_b(unsigned short port);
void port_out_b(unsigned short port, unsigned char val);
unsigned short port_in_w(unsigned short port);
void port_out_w(unsigned short port, unsigned short val);
unsigned char read_far_b(unsigned short seg, unsigned short off);
void write_far_b(unsigned short seg, unsigned short off, unsigned char val);
unsigned short read_far_w(unsigned short seg, unsigned short off);
void write_far_w(unsigned short seg, unsigned short off, unsigned short val);
unsigned short disk_chs_read_int(unsigned short cx, unsigned short dx,
                                 unsigned short seg, unsigned short off);
unsigned short disk_chs_write_int(unsigned short cx, unsigned short dx,
                                  unsigned short seg, unsigned short off);
unsigned short disk_query_geom_int(unsigned char drive);
void disk_reset_int(unsigned char drive);
unsigned short syscall_int40(unsigned char ah, unsigned char al,
                   unsigned short bx, unsigned short cx,
                   unsigned short dx, unsigned short si,
                   unsigned short di);

/* Interrupt control */
void enable_interrupts(void);
void disable_interrupts(void);

/* -- 8-bit port I/O -- */

#pragma aux port_in_b = \
    "in al, dx" \
    parm [dx] value [al] modify [al];

#pragma aux port_out_b = \
    "out dx, al" \
    parm [dx] [al];

/* -- 16-bit port I/O -- */

#pragma aux port_in_w = \
    "in ax, dx" \
    parm [dx] value [ax] modify [ax];

#pragma aux port_out_w = \
    "out dx, ax" \
    parm [dx] [ax];

/* -- Far memory access -- */
/* Parameters: DX=segment, AX=offset, value/result in AL/CL/AX */
/* DS saved/restored with push/pop, so NOT in modify list */
/* BX saved/restored with push/pop, so NOT in modify list */

#pragma aux read_far_b = \
    "push ds" \
    "push bx" \
    "mov ds, dx" \
    "mov bx, ax" \
    "mov al, [bx]" \
    "pop bx" \
    "pop ds" \
    parm [dx] [ax] value [al] modify [al];

#pragma aux write_far_b = \
    "push ds" \
    "push bx" \
    "mov ds, dx" \
    "mov bx, ax" \
    "mov [bx], cl" \
    "pop bx" \
    "pop ds" \
    parm [dx] [ax] [cl];

#pragma aux read_far_w = \
    "push ds" \
    "push bx" \
    "mov ds, dx" \
    "mov bx, ax" \
    "mov ax, [bx]" \
    "pop bx" \
    "pop ds" \
    parm [dx] [ax] value [ax] modify [ax];

#pragma aux write_far_w = \
    "push ds" \
    "push bx" \
    "mov ds, dx" \
    "mov bx, ax" \
    "mov [bx], cx" \
    "pop bx" \
    "pop ds" \
    parm [dx] [ax] [cx];

/* -- INT 0x13 disk I/O (CHS, AH=02h/03h: works on all BIOSes incl.
 *    pre-EDD 386/XT-era and XTIDE; EDD AH=42h does not exist there) -- */
/* CX/DX preloaded with cylinder/sector and head/drive per INT 13h
 * convention; buffer segment passed in SI, offset in BX. */

#pragma aux disk_chs_read_int = \
    "push es" \
    "mov es, si" \
    "mov ax, 0201h" \
    "int 13h" \
    "jnc success" \
    "mov ax, 1" \
    "jmp done" \
    "success:" \
    "xor ax, ax" \
    "done:" \
    "pop es" \
    parm [cx] [dx] [si] [bx] value [ax] modify [ax bx cx dx si di];

#pragma aux disk_chs_write_int = \
    "push es" \
    "mov es, si" \
    "mov ax, 0301h" \
    "int 13h" \
    "jnc success" \
    "mov ax, 1" \
    "jmp done" \
    "success:" \
    "xor ax, ax" \
    "done:" \
    "pop es" \
    parm [cx] [dx] [si] [bx] value [ax] modify [ax bx cx dx si di];

/* AH=08h get drive parameters. Returns AH = max head, AL = sectors per
 * track (0 on failure). ES:DI zeroed first to guard buggy BIOSes. */
#pragma aux disk_query_geom_int = \
    "push es" \
    "push di" \
    "xor di, di" \
    "mov es, di" \
    "mov ah, 8" \
    "int 13h" \
    "jc fail" \
    "and cl, 3Fh" \
    "mov al, cl" \
    "mov ah, dh" \
    "jmp done" \
    "fail:" \
    "xor ax, ax" \
    "done:" \
    "pop di" \
    "pop es" \
    parm [dl] value [ax] modify [ax bx cx dx];

#pragma aux disk_reset_int = \
    "xor ax, ax" \
    "int 13h" \
    parm [dl] modify [ax];

/* -- INT 0x40 syscall -- */
#pragma aux syscall_int40 = \
    "int 40h" \
    parm [ah] [al] [bx] [cx] [dx] [si] [di] \
    value [ax] modify [ax bx cx dx si di];

/* -- Interrupt control -- */
#pragma aux enable_interrupts = "sti";
#pragma aux disable_interrupts = "cli" modify [];

#endif /* PORT_IO_H */
