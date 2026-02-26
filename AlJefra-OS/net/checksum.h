/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Internet checksum (RFC 1071)
 *
 * Used by IPv4, TCP, UDP, and ICMP headers.
 */

#ifndef ALJEFRA_NET_CHECKSUM_H
#define ALJEFRA_NET_CHECKSUM_H

#include <stdint.h>

static inline uint16_t ip_checksum(const void *data, uint32_t len)
{
    const uint16_t *p = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len == 1)
        sum += *(const uint8_t *)p;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

#endif /* ALJEFRA_NET_CHECKSUM_H */
