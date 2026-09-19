/* test.c -- Minimal DOS test */
#include <dos.h>

void main(void)
{
    union REGS r;
    r.h.ah = 0x09;
    r.x.dx = (unsigned int)"Hello\r\n$";
    int86(0x21, &r, &r);
}