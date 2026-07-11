/* timer.c -- PIT timer driver (100 Hz, IRQ0) */

#include "kernel.h"
#include "drivers/timer.h"

/* PIT I/O ports */
#define PIT_CH0         0x40
#define PIT_CMD         0x43

/* PIC I/O ports */
#define PIC1_CMD        0x20
#define PIC1_DATA       0x21

/* Command: channel 0, lobyte/hibyte, mode 2 (rate gen), binary */
#define PIT_CTRL_VAL    0x34

/* Divisor for 100 Hz (1193182 / 100 = 11931 = 0x2E9B) */
#define TIMER_DIVISOR   11931

/* Global tick counter */
volatile uint32_t timer_ticks = 0;

/* IRQ0 handler (called from assembly stub in syscall.asm) */
void timer_irq_handler(void)
{
    timer_ticks++;

    /* Send End-of-Interrupt to master PIC */
    port_out_b(PIC1_CMD, 0x20);
}

void timer_init(void)
{
    extern void irq0_stub(void);

    /* Program PIT channel 0: divisor for 100 Hz */
    port_out_b(PIT_CMD, PIT_CTRL_VAL);
    port_out_b(PIT_CH0, (uint8_t)(TIMER_DIVISOR & 0xFF));        /* low byte */
    port_out_b(PIT_CH0, (uint8_t)((TIMER_DIVISOR >> 8) & 0xFF)); /* high byte */

    /* Unmask IRQ0 on master PIC (clear bit 0 of IMR) */
    {
        uint8_t imr;
        imr = port_in_b(PIC1_DATA);
        port_out_b(PIC1_DATA, imr & ~0x01);
    }

    /* Install IRQ0 handler in IVT entry 0x08 */
    write_far_w(0x0000, 0x0008 * 4, (uint16_t)&irq0_stub);          /* offset */
    write_far_w(0x0000, 0x0008 * 4 + 2, KERNEL_SEGMENT);          /* segment */
}

void timer_delay(uint16_t ms)
{
    /* Each tick = ~10 ms, so ticks = ms / 10 */
    uint32_t target = timer_ticks + ms / 10 + 1;

    while (timer_ticks < target)
        ;
}