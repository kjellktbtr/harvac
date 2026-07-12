#ifndef TYPES_H
#define TYPES_H

/* Basic type definitions for freestanding 16-bit environment */
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long  uint32_t;

typedef signed char    int8_t;
typedef signed short   int16_t;
typedef signed long    int32_t;

/* Useful for far pointer construction */
typedef uint16_t seg_t;

/* Boolean */
#define TRUE  1
#define FALSE 0

/* NULL pointer */
#define NULL ((void *)0)

#endif /* TYPES_H */
