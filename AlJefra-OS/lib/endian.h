/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Portable byte-order helpers
 *
 * All network code should #include this instead of defining local
 * htons/htonl/ntohs/ntohl.
 *
 * Uses shift-based swap (portable across all architectures including
 * RISC-V where __builtin_bswap may emit libgcc calls).
 */

#ifndef ALJEFRA_LIB_ENDIAN_H
#define ALJEFRA_LIB_ENDIAN_H

#include <stdint.h>

static inline uint16_t htons(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

static inline uint32_t htonl(uint32_t v)
{
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v & 0xFF0000) >> 8) | ((v >> 24) & 0xFF);
}

static inline uint16_t ntohs(uint16_t v)
{
    return htons(v);
}

static inline uint32_t ntohl(uint32_t v)
{
    return htonl(v);
}

#endif /* ALJEFRA_LIB_ENDIAN_H */
