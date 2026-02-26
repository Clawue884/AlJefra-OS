/*
 * Safe Instruction Substitution Patterns
 * Each pattern maps one instruction encoding to an equivalent one
 * that may be faster on modern x86-64 CPUs.
 */
#ifndef PATTERNS_H
#define PATTERNS_H

#include <stdint.h>

/* A substitution pattern */
typedef struct {
    const uint8_t *from_bytes;
    int            from_len;
    const uint8_t *to_bytes;
    int            to_len;
    const char    *description;
    int            size_change;    /* to_len - from_len */
} substitution_t;

/* Get the table of safe substitutions.
 * Returns number of entries. */
int patterns_get_substitutions(const substitution_t **out);

/* Try to find a substitution for the instruction at the given offset.
 * Returns the substitution index, or -1 if none found. */
int patterns_find_match(const uint8_t *code, int code_len, int offset);

#endif /* PATTERNS_H */
