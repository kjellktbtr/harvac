#ifndef DRIVERS_SERIAL_H
#define DRIVERS_SERIAL_H

#include "types.h"

/* COM1 port addresses */
#define SERIAL_COM1         0x3F8
#define SERIAL_COM1_BAUD    0x3F9

/* Initialize COM1 at 115200 baud, 8N1 */
void serial_init(void);

/* Write a single character (blocking until transmit ready) */
void serial_putchar(char c);

/* Write a null-terminated string */
void serial_puts(const char *s);

/* Read a single character (blocking until data arrives) */
char serial_getchar(void);

/* Return nonzero if a character is available */
uint8_t serial_available(void);

#endif /* DRIVERS_SERIAL_H */