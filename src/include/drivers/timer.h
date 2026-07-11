#ifndef DRIVERS_TIMER_H
#define DRIVERS_TIMER_H

#include "types.h"

/* Timer ticks since boot (increments ~100 times per second) */
extern volatile uint32_t timer_ticks;

/* Initialize PIT channel 0 at 100 Hz and install IRQ0 handler */
void timer_init(void);

/* Busy-wait for approximately 'ms' milliseconds */
void timer_delay(uint16_t ms);

#endif /* DRIVERS_TIMER_H */