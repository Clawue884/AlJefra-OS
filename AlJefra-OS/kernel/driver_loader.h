/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Driver Loader Interface
 *
 * Supports two kinds of drivers:
 *   1. Built-in: compiled into the kernel image, matched by name
 *   2. Runtime:  loaded from .ajdrv packages downloaded via marketplace
 *
 * Driver patterns used in the codebase:
 *
 *   REGISTERED DRIVERS (11) — implement driver_ops_t + _register():
 *     e1000, rtl8169, virtio_net, virtio_blk, ahci, nvme, touch, ufs,
 *     intel_wifi, xhci, bcm_wifi
 *     These are matched by PCI vendor/device ID during boot in
 *     kernel/main.c:load_builtin_drivers().
 *
 *   LIBRARY DRIVERS (11) — provide utility APIs called directly:
 *     ps2, usb_hid, lfb, serial_console, acpi_lite, dt_parser, pcie,
 *     emmc, wifi_framework, aes_ccmp, bcm_wifi (also library APIs)
 *     These expose functions (e.g. ps2_init(), lfb_clear()) used by
 *     the kernel or other drivers without going through driver_ops_t.
 */

#ifndef ALJEFRA_DRIVER_LOADER_H
#define ALJEFRA_DRIVER_LOADER_H

#include "../hal/hal.h"

/* Driver categories */
typedef enum {
    DRIVER_CAT_STORAGE  = 0,
    DRIVER_CAT_NETWORK  = 1,
    DRIVER_CAT_INPUT    = 2,
    DRIVER_CAT_DISPLAY  = 3,
    DRIVER_CAT_GPU      = 4,
    DRIVER_CAT_BUS      = 5,
    DRIVER_CAT_OTHER    = 6,
} driver_category_t;

/* Driver operations (vtable) */
typedef struct {
    const char       *name;
    driver_category_t category;
    hal_status_t    (*init)(hal_device_t *dev);
    void            (*shutdown)(void);

    /* Storage ops */
    int64_t         (*read)(void *buf, uint64_t lba, uint32_t count);
    int64_t         (*write)(const void *buf, uint64_t lba, uint32_t count);

    /* Network ops */
    int64_t         (*net_tx)(const void *frame, uint64_t len);
    int64_t         (*net_rx)(void *frame, uint64_t max_len);
    void            (*net_get_mac)(uint8_t mac[6]);

    /* Input ops */
    int             (*input_poll)(void); /* Returns keycode or -1 */
} driver_ops_t;

/* Kernel API vtable — passed to runtime-loaded drivers so they can
 * access HAL services without linking to kernel symbols directly. */
typedef struct {
    /* Console */
    void (*puts)(const char *s);

    /* MMIO */
    uint32_t (*mmio_read32)(volatile void *addr);
    void     (*mmio_write32)(volatile void *addr, uint32_t val);
    uint8_t  (*mmio_read8)(volatile void *addr);
    void     (*mmio_write8)(volatile void *addr, uint8_t val);
    void     (*mmio_barrier)(void);

    /* DMA */
    void *(*dma_alloc)(uint64_t size, uint64_t *phys);
    void  (*dma_free)(void *ptr, uint64_t size);

    /* Timer */
    void     (*delay_us)(uint64_t us);
    uint64_t (*timer_ms)(void);

    /* Bus / PCI */
    void          (*pci_enable)(hal_device_t *dev);
    volatile void *(*map_bar)(hal_device_t *dev, uint32_t idx);
    uint32_t      (*pci_read32)(uint32_t bdf, uint32_t reg);
    void          (*pci_write32)(uint32_t bdf, uint32_t reg, uint32_t val);
} kernel_api_t;

/* Maximum loaded drivers */
#define MAX_DRIVERS  32

/* Register a built-in driver (called at compile time via driver_register) */
void driver_register_builtin(const driver_ops_t *ops);

/* Load a built-in driver by name for a specific device */
hal_status_t driver_load_builtin(const char *name, hal_device_t *dev);

/* Load a runtime driver from a memory buffer (.ajdrv binary)
 * The buffer contains a simple relocatable binary + metadata header.
 */
hal_status_t driver_load_runtime(const void *ajdrv_data, uint64_t size, hal_device_t *dev);

/* Find a loaded driver by category */
const driver_ops_t *driver_find(driver_category_t cat);

/* Find a loaded driver by name */
const driver_ops_t *driver_find_by_name(const char *name);

/* Get the active network driver (first loaded network driver) */
const driver_ops_t *driver_get_network(void);

/* Get the active storage driver (first loaded storage driver) */
const driver_ops_t *driver_get_storage(void);

/* Get the active input driver */
const driver_ops_t *driver_get_input(void);

/* List all loaded drivers */
uint32_t driver_list(const driver_ops_t **out, uint32_t max);

#endif /* ALJEFRA_DRIVER_LOADER_H */
