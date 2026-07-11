#ifndef FAR_H
#define FAR_H

#include "types.h"

/* Far pointer construction (OpenWatcom 16-bit small model) */
#define MK_FP(seg, off) \
    ((void __far *)(((uint32_t)(uint16_t)(seg) << 16) | (uint16_t)(off)))
#define FP_SEG(p)  ((uint16_t)((uint32_t)(void __far *)(p) >> 16))
#define FP_OFF(p)  ((uint16_t)(uint32_t)(void __far *)(p))

void far_copy(uint8_t __far *dst, const uint8_t __far *src, uint16_t n);
void far_move(uint8_t __far *dst, const uint8_t __far *src, uint16_t n);

#endif /* FAR_H */
