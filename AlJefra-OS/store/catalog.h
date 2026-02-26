/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Driver Catalog */

#ifndef ALJEFRA_CATALOG_H
#define ALJEFRA_CATALOG_H

#include <stdint.h>
#include "../hal/hal.h"
#include "package.h"

/* Catalog entry */
typedef struct {
    char     name[64];
    char     version[16];
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t arch;
    uint32_t category;
    uint32_t size_bytes;
} catalog_entry_t;

/* Local driver catalog (cached from marketplace) */
#define CATALOG_MAX_ENTRIES  128

/* Initialize the catalog */
void catalog_init(void);

/* Add an entry to the local catalog */
hal_status_t catalog_add(const catalog_entry_t *entry);

/* Find a driver for a specific device */
const catalog_entry_t *catalog_find(uint16_t vendor_id, uint16_t device_id, uint32_t arch);

/* Find all drivers for a given category */
uint32_t catalog_find_by_category(uint32_t category, catalog_entry_t *out, uint32_t max);

/* Get catalog entry count */
uint32_t catalog_count(void);

#endif /* ALJEFRA_CATALOG_H */
