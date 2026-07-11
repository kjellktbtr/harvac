/* memory.c -- Bitmap heap allocator (16-byte pages) */

#include "kernel.h"
#include "drivers/memory.h"

/* Heap region: starts right after kernel binary at 0x21000 */
#define HEAP_START      0x21000UL
#define HEAP_SIZE       0x10000UL   /* 64 KB heap */
#define PAGE_SIZE       16          /* Must be power of 2 */
#define PAGE_SHIFT      4           /* log2(PAGE_SIZE) */
#define NUM_PAGES       4096        /* HEAP_SIZE / PAGE_SIZE */
#define BITMAP_SIZE     512         /* NUM_PAGES / 8 */

/* Bitmap: 1 = free, 0 = allocated -- stored in kernel BSS/data */
static uint8_t bitmap[BITMAP_SIZE];

/* Total pages allocated (for quick info, also used for OOM check) */
static uint16_t pages_used;

void memory_init(void)
{
    uint16_t i;

    /* Mark all pages as free */
    for (i = 0; i < BITMAP_SIZE; i++)
        bitmap[i] = 0xFF;

    pages_used = 0;
}

uint32_t mem_alloc(void)
{
    uint16_t i;
    uint16_t byte_idx;
    uint8_t bit_mask;

    /* Scan bitmap for first free page */
    for (i = 0; i < NUM_PAGES; i++) {
        byte_idx = i >> 3;          /* i / 8 */
        bit_mask = (uint8_t)(1 << (i & 7));

        if (bitmap[byte_idx] & bit_mask) {
            /* Mark as allocated */
            bitmap[byte_idx] &= (uint8_t)~bit_mask;
            pages_used++;
            return HEAP_START + (uint32_t)i * PAGE_SIZE;
        }
    }

    return 0;  /* Out of memory */
}

void mem_free(uint32_t page)
{
    uint16_t i;
    uint16_t byte_idx;
    uint8_t bit_mask;

    if (page == 0)
        return;

    if (page < HEAP_START)
        return;

    i = (uint16_t)((page - HEAP_START) >> PAGE_SHIFT);
    if (i >= NUM_PAGES)
        return;

    byte_idx = i >> 3;
    bit_mask = (uint8_t)(1 << (i & 7));

    /* Only mark free if currently allocated */
    if (!(bitmap[byte_idx] & bit_mask)) {
        bitmap[byte_idx] |= bit_mask;
        pages_used--;
    }
}