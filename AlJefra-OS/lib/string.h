/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Shared string/memory utilities
 *
 * Every module should #include this instead of reimplementing memcpy/memset/etc.
 */

#ifndef ALJEFRA_LIB_STRING_H
#define ALJEFRA_LIB_STRING_H

#include <stdint.h>
#include <stddef.h>

/* Standard C memory functions (compiler may emit implicit calls) */
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int   memcmp(const void *a, const void *b, size_t n);

/* String comparison: returns 1 if equal, 0 if not */
int str_eq(const char *a, const char *b);

/* String length (no libc strlen to avoid name clash with compiler builtin) */
uint32_t str_len(const char *s);

/* String copy (bounded) */
void str_copy(char *dst, const char *src, uint32_t max);

#endif /* ALJEFRA_LIB_STRING_H */
