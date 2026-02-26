/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Driver Catalog Implementation
 *
 * In-memory catalog of available drivers.
 * Populated from the marketplace API response.
 */

#include "catalog.h"
#include "../hal/hal.h"
#include "../lib/string.h"

static catalog_entry_t g_catalog[CATALOG_MAX_ENTRIES];
static uint32_t g_count;

void catalog_init(void)
{
    g_count = 0;
    for (uint32_t i = 0; i < CATALOG_MAX_ENTRIES; i++) {
        uint8_t *p = (uint8_t *)&g_catalog[i];
        for (uint64_t j = 0; j < sizeof(catalog_entry_t); j++)
            p[j] = 0;
    }
}

hal_status_t catalog_add(const catalog_entry_t *entry)
{
    if (g_count >= CATALOG_MAX_ENTRIES)
        return HAL_NO_MEMORY;

    /* Copy entry */
    uint8_t *dst = (uint8_t *)&g_catalog[g_count];
    const uint8_t *src = (const uint8_t *)entry;
    for (uint64_t i = 0; i < sizeof(catalog_entry_t); i++)
        dst[i] = src[i];

    g_count++;
    return HAL_OK;
}

const catalog_entry_t *catalog_find(uint16_t vendor_id, uint16_t device_id, uint32_t arch)
{
    for (uint32_t i = 0; i < g_count; i++) {
        if (g_catalog[i].vendor_id == vendor_id &&
            g_catalog[i].device_id == device_id &&
            (g_catalog[i].arch == arch || g_catalog[i].arch == AJDRV_ARCH_ANY)) {
            return &g_catalog[i];
        }
    }
    return NULL;
}

uint32_t catalog_find_by_category(uint32_t category, catalog_entry_t *out, uint32_t max)
{
    uint32_t found = 0;
    for (uint32_t i = 0; i < g_count && found < max; i++) {
        if (g_catalog[i].category == category) {
            uint8_t *dst = (uint8_t *)&out[found];
            const uint8_t *src = (const uint8_t *)&g_catalog[i];
            for (uint64_t j = 0; j < sizeof(catalog_entry_t); j++)
                dst[j] = src[j];
            found++;
        }
    }
    return found;
}

uint32_t catalog_count(void)
{
    return g_count;
}
