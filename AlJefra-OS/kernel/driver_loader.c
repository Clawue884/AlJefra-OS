/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Driver Loader Implementation
 *
 * Manages built-in and runtime-loaded drivers.
 * Built-in drivers are registered at init time.
 * Runtime drivers come from .ajdrv packages via the marketplace.
 */

#include "driver_loader.h"
#include "../hal/hal.h"
#include "../store/verify.h"
#include "../lib/string.h"

/* ── Built-in driver registry ── */

static const driver_ops_t *g_builtin_drivers[MAX_DRIVERS];
static uint32_t g_builtin_count;

/* Loaded (active) drivers */
static struct {
    const driver_ops_t *ops;
    int                 active;
} g_loaded[MAX_DRIVERS];
static uint32_t g_loaded_count;

void driver_register_builtin(const driver_ops_t *ops)
{
    if (g_builtin_count < MAX_DRIVERS) {
        g_builtin_drivers[g_builtin_count++] = ops;
    }
}

hal_status_t driver_load_builtin(const char *name, hal_device_t *dev)
{
    for (uint32_t i = 0; i < g_builtin_count; i++) {
        if (str_eq(g_builtin_drivers[i]->name, name)) {
            const driver_ops_t *ops = g_builtin_drivers[i];

            /* Initialize the driver with the device */
            hal_status_t rc = HAL_OK;
            if (ops->init) {
                rc = ops->init(dev);
            }

            if (rc != HAL_OK) {
                hal_console_printf("[driver] Failed to init '%s': %d\n", name, rc);
                return rc;
            }

            /* Add to loaded list */
            if (g_loaded_count < MAX_DRIVERS) {
                g_loaded[g_loaded_count].ops = ops;
                g_loaded[g_loaded_count].active = 1;
                g_loaded_count++;
            }

            hal_console_printf("[driver] Loaded '%s'\n", name);
            return HAL_OK;
        }
    }

    hal_console_printf("[driver] Built-in '%s' not found\n", name);
    return HAL_NO_DEVICE;
}

/* ── Runtime driver loading ── */

/* .ajdrv package format defined in store/package.h */

hal_status_t driver_load_runtime(const void *ajdrv_data, uint64_t size, hal_device_t *dev)
{
    if (size < sizeof(ajdrv_header_t))
        return HAL_ERROR;

    const ajdrv_header_t *hdr = (const ajdrv_header_t *)ajdrv_data;

    /* Validate magic */
    if (hdr->magic != AJDRV_MAGIC) {
        hal_console_printf("[driver] Invalid .ajdrv magic: 0x%08x\n", hdr->magic);
        return HAL_ERROR;
    }

    /* Check architecture */
    uint32_t my_arch = (uint32_t)hal_arch();
    if (hdr->arch != my_arch && hdr->arch != 0xFF) {
        hal_console_printf("[driver] Architecture mismatch: pkg=%u, sys=%u\n", hdr->arch, my_arch);
        return HAL_NOT_SUPPORTED;
    }

    /* Bounds check */
    if (hdr->code_offset + hdr->code_size > size ||
        hdr->name_offset + hdr->name_size > size) {
        hal_console_printf("[driver] Invalid offsets: code=%u+%u, name=%u+%u, pkg=%u\n",
                           hdr->code_offset, hdr->code_size,
                           hdr->name_offset, hdr->name_size, (uint32_t)size);
        return HAL_ERROR;
    }

    /* Extract driver name */
    const char *name = (const char *)ajdrv_data + hdr->name_offset;

    /* Verify Ed25519 signature if a trusted key has been configured.
     * In development mode (no key set), verification is skipped. */
    hal_status_t sig_rc = ajdrv_verify(ajdrv_data, size);
    if (sig_rc == HAL_NOT_SUPPORTED) {
        /* No trusted key set — development mode, skip verification */
    } else if (sig_rc != HAL_OK) {
        hal_console_printf("[driver] Signature verification FAILED for '%s'\n", name);
        return HAL_ERROR;
    }
    hal_console_printf("[driver] .ajdrv: '%s' v%u, code=%u bytes at 0x%x, entry=0x%x\n",
                       name, hdr->version, hdr->code_size, hdr->code_offset, hdr->entry_offset);

    /* Allocate memory for the driver code */
    uint64_t code_phys;
    void *code_buf = hal_dma_alloc(hdr->code_size, &code_phys);
    if (!code_buf) {
        hal_console_puts("[driver] Out of memory for driver code\n");
        return HAL_NO_MEMORY;
    }

    /* Copy code to executable memory */
    memcpy(code_buf, (const uint8_t *)ajdrv_data + hdr->code_offset, hdr->code_size);

    /* Build kernel API vtable for runtime drivers */
    static const kernel_api_t kapi = {
        .puts        = hal_console_puts,
        .mmio_read32 = hal_mmio_read32,
        .mmio_write32= hal_mmio_write32,
        .mmio_read8  = hal_mmio_read8,
        .mmio_write8 = hal_mmio_write8,
        .mmio_barrier= hal_mmio_barrier,
        .dma_alloc   = hal_dma_alloc,
        .dma_free    = hal_dma_free,
        .delay_us    = hal_timer_delay_us,
        .timer_ms    = hal_timer_ms,
        .pci_enable  = hal_bus_pci_enable,
        .map_bar     = hal_bus_map_bar,
        .pci_read32  = hal_bus_pci_read32,
        .pci_write32 = hal_bus_pci_write32,
    };

    /* The entry point receives a kernel API vtable and returns a driver_ops_t* */
    typedef const driver_ops_t *(*driver_entry_fn)(const kernel_api_t *api);
    driver_entry_fn entry = (driver_entry_fn)((uint8_t *)code_buf + hdr->entry_offset);

    hal_console_printf("[driver] Calling entry at %p (code at %p)\n", (void *)entry, code_buf);

    /* Call the driver's entry point to get its ops table */
    const driver_ops_t *ops = entry(&kapi);
    if (!ops) {
        hal_console_printf("[driver] '%s' entry returned NULL\n", name);
        hal_dma_free(code_buf, hdr->code_size);
        return HAL_ERROR;
    }

    /* Initialize driver with device */
    hal_status_t rc = HAL_OK;
    if (ops->init) {
        rc = ops->init(dev);
    }

    if (rc != HAL_OK) {
        hal_console_printf("[driver] Runtime '%s' init failed: %d\n", name, rc);
        hal_dma_free(code_buf, hdr->code_size);
        return rc;
    }

    /* Add to loaded list */
    if (g_loaded_count < MAX_DRIVERS) {
        g_loaded[g_loaded_count].ops = ops;
        g_loaded[g_loaded_count].active = 1;
        g_loaded_count++;
    }

    hal_console_printf("[driver] Loaded runtime driver '%s'\n", name);
    return HAL_OK;
}

/* ── Query functions ── */

const driver_ops_t *driver_find(driver_category_t cat)
{
    for (uint32_t i = 0; i < g_loaded_count; i++) {
        if (g_loaded[i].active && g_loaded[i].ops->category == cat)
            return g_loaded[i].ops;
    }
    return NULL;
}

const driver_ops_t *driver_find_by_name(const char *name)
{
    for (uint32_t i = 0; i < g_loaded_count; i++) {
        if (g_loaded[i].active && str_eq(g_loaded[i].ops->name, name))
            return g_loaded[i].ops;
    }
    return NULL;
}

const driver_ops_t *driver_get_network(void)
{
    return driver_find(DRIVER_CAT_NETWORK);
}

const driver_ops_t *driver_get_storage(void)
{
    return driver_find(DRIVER_CAT_STORAGE);
}

const driver_ops_t *driver_get_input(void)
{
    return driver_find(DRIVER_CAT_INPUT);
}

uint32_t driver_list(const driver_ops_t **out, uint32_t max)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < g_loaded_count && count < max; i++) {
        if (g_loaded[i].active)
            out[count++] = g_loaded[i].ops;
    }
    return count;
}
