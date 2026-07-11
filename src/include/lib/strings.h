#ifndef LIB_STRINGS_H
#define LIB_STRINGS_H

#include "types.h"

uint16_t strlen(const char *s);
int16_t  strcmp(const char *a, const char *b);
int16_t  strncmp(const char *a, const char *b, uint16_t n);
void     strcpy(char *dst, const char *src);
void     memset(void *dst, uint8_t val, uint16_t len);
void     memcpy(void *dst, const void *src, uint16_t len);
void     itoa(int16_t val, char *buf, uint8_t base);
void     hex_to_str(uint32_t val, char *buf);

#endif /* LIB_STRINGS_H */