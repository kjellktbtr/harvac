/* serial.c -- COM1 serial port driver (115200 8N1) */

#include "kernel.h"
#include "drivers/serial.h"

void serial_init(void)
{
    uint8_t lcr;

    /* Set DLAB=1 to access baud rate divisor */
    lcr = 0x80;  /* DLAB = 1 */
    port_out_b(SERIAL_COM1 + 3, lcr);

    /* Divisor = 115200 / 115200 = 1 */
    port_out_b(SERIAL_COM1 + 0, 1);      /* Low byte */
    port_out_b(SERIAL_COM1 + 1, 0);      /* High byte */

    /* Line Control Register: 8N1 (8 bits, no parity, 1 stop bit) */
    lcr = 0x03;  /* DLAB = 0, 8 bits, no parity, 1 stop */
    port_out_b(SERIAL_COM1 + 3, lcr);

    /* FIFO: enable, trigger at 14 bytes, clear Tx/Rx */
    port_out_b(SERIAL_COM1 + 2, 0xC7);

    /* Drain any stale bytes remaining after FIFO clear.
     * Bounded to 16 iterations so a stuck UART (or absent hardware)
     * does not spin forever. */
    {
        uint8_t i;
        for (i = 0; i < 16 && (port_in_b(SERIAL_COM1 + 5) & 0x01); i++)
            (void)port_in_b(SERIAL_COM1);
    }

    /* RTS/DSR: set flow control signals */
    port_out_b(SERIAL_COM1 + 4, 0x0B);
}

void serial_putchar(char c)
{
    /* Wait for Transmitter Holding Register Empty (bit 5 of LSR) */
    while (!(port_in_b(SERIAL_COM1 + 5) & 0x20))
        ;

    port_out_b(SERIAL_COM1, (uint8_t)c);

    /* CR -> CR+LF translation for line endings */
    if (c == '\n') {
        while (!(port_in_b(SERIAL_COM1 + 5) & 0x20))
            ;
        port_out_b(SERIAL_COM1, '\r');
    }
}

void serial_puts(const char *s)
{
    while (*s != '\0') {
        serial_putchar(*s);
        s++;
    }
}

char serial_getchar(void)
{
    /* Wait for Data Ready (bit 0 of LSR) */
    while (!(port_in_b(SERIAL_COM1 + 5) & 0x01))
        ;

    return (char)port_in_b(SERIAL_COM1);
}

uint8_t serial_available(void)
{
    return (port_in_b(SERIAL_COM1 + 5) & 0x01) != 0;
}