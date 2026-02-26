/* SPDX-License-Identifier: MIT */
/* AlJefra OS — AI Bootstrap Interface
 *
 * The AI bootstrap module is the brain of universal boot.
 * It performs:
 *   1. Hardware manifest creation (list of all detected devices)
 *   2. Network bringup (DHCP if available)
 *   3. TLS connection to AlJefra marketplace
 *   4. Send manifest → receive driver recommendations
 *   5. Download and load each recommended driver
 *   6. Verify system is fully operational
 */

#ifndef ALJEFRA_AI_BOOTSTRAP_H
#define ALJEFRA_AI_BOOTSTRAP_H

#include "../hal/hal.h"

/* Bootstrap state machine */
typedef enum {
    BOOTSTRAP_INIT         = 0,
    BOOTSTRAP_NET_UP       = 1,
    BOOTSTRAP_CONNECTED    = 2,
    BOOTSTRAP_MANIFEST_SENT= 3,
    BOOTSTRAP_DOWNLOADING  = 4,
    BOOTSTRAP_COMPLETE     = 5,
    BOOTSTRAP_FAILED       = 6,
} bootstrap_state_t;

/* Hardware manifest entry */
typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  has_driver;     /* 1 if a built-in driver matched */
    uint8_t  reserved;
} manifest_entry_t;

/* Hardware manifest */
typedef struct {
    hal_arch_t       arch;
    uint32_t         entry_count;
    manifest_entry_t entries[HAL_BUS_MAX_DEVICES];
    char             cpu_vendor[16];
    char             cpu_model[48];
    uint64_t         ram_bytes;
} hardware_manifest_t;

/* Run the AI bootstrap sequence.
 * devices/count = output from hal_bus_scan().
 * Returns HAL_OK if the system is fully operational.
 */
hal_status_t ai_bootstrap(hal_device_t *devices, uint32_t count);

/* Get current bootstrap state */
bootstrap_state_t ai_bootstrap_state(void);

/* Build a hardware manifest from detected devices */
void ai_bootstrap_build_manifest(hal_device_t *devices, uint32_t count,
                                  hardware_manifest_t *manifest);

#endif /* ALJEFRA_AI_BOOTSTRAP_H */
