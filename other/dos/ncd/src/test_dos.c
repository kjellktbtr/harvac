/* test_dos.c -- Minimal DOS COM test to verify output works */

#include <dos.h>

void main(void)
{
    union REGS regs;

    /* Print string using DOS INT 21h AH=09h */
    regs.h.ah = 0x09;
    regs.x.dx = (unsigned short)"Hello from NCD DOS port!\r\n$";
    int86(0x21, &regs, &regs);

    /* Wait for key */
    regs.h.ah = 0x00;
    int86(0x16, &regs, &regs);
}