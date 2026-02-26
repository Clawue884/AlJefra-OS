/* SPDX-License-Identifier: MIT */
/* AlJefra OS — DHCP Client */

#ifndef ALJEFRA_DHCP_H
#define ALJEFRA_DHCP_H

#include <stdint.h>
#include "../hal/hal.h"

/* DHCP lease info */
typedef struct {
    uint32_t client_ip;
    uint32_t server_ip;
    uint32_t gateway;
    uint32_t subnet_mask;
    uint32_t dns_server;
    uint32_t lease_time;   /* seconds */
} dhcp_lease_t;

/* Run DHCP discovery. Returns HAL_OK on success.
 * ip, gateway, dns are returned in host byte order.
 */
hal_status_t dhcp_discover(uint32_t *ip, uint32_t *gateway, uint32_t *dns);

/* Get the full lease info from last successful discover */
const dhcp_lease_t *dhcp_get_lease(void);

#endif /* ALJEFRA_DHCP_H */
