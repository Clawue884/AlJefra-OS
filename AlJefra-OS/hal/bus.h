/* SPDX-License-Identifier: MIT */
/* AlJefra OS — HAL Bus / Device Discovery Interface
 * Unified device enumeration over PCIe, Device Tree, and ACPI.
 */

#ifndef ALJEFRA_HAL_BUS_H
#define ALJEFRA_HAL_BUS_H

#include <stdint.h>

/* Maximum BARs per device (PCIe = 6) */
#define HAL_BUS_MAX_BARS  6

/* Maximum devices returned by a single scan */
#define HAL_BUS_MAX_DEVICES  256

/* Bus type that discovered this device */
typedef enum {
    HAL_BUS_PCIE = 0,
    HAL_BUS_DT   = 1,   /* Device Tree (ARM/RISC-V) */
    HAL_BUS_ACPI = 2,   /* ACPI (x86) */
    HAL_BUS_MMIO = 3,   /* Platform / hardcoded MMIO */
} hal_bus_type_t;

/* Unified device descriptor */
typedef struct {
    hal_bus_type_t bus_type;
    uint16_t       vendor_id;     /* PCIe vendor ID (or DT-assigned) */
    uint16_t       device_id;     /* PCIe device ID */
    uint8_t        class_code;    /* PCI class code */
    uint8_t        subclass;      /* PCI subclass */
    uint8_t        prog_if;       /* Programming interface */
    uint8_t        irq;           /* Interrupt line */
    uint64_t       bar[HAL_BUS_MAX_BARS];  /* Base Address Registers (physical) */
    uint64_t       bar_size[HAL_BUS_MAX_BARS]; /* BAR region sizes */
    uint16_t       bus;           /* PCIe bus number */
    uint8_t        dev;           /* PCIe device number */
    uint8_t        func;          /* PCIe function number */
    char           compatible[64]; /* DT "compatible" string (ARM/RISC-V) */
} hal_device_t;

/* Initialize bus enumeration subsystem */
hal_status_t hal_bus_init(void);

/* Scan for all devices.  Returns number of devices found. */
uint32_t hal_bus_scan(hal_device_t *devs, uint32_t max);

/* Read a 32-bit PCI config register.  bdf = (bus<<8 | dev<<3 | func). */
uint32_t hal_bus_pci_read32(uint32_t bdf, uint32_t reg);

/* Write a 32-bit PCI config register. */
void hal_bus_pci_write32(uint32_t bdf, uint32_t reg, uint32_t val);

/* Enable bus-mastering + memory space for a device */
void hal_bus_pci_enable(hal_device_t *dev);

/* Map a device BAR into virtual address space (identity map on freestanding) */
volatile void *hal_bus_map_bar(hal_device_t *dev, uint32_t bar_index);

/* Find devices by class/subclass.  Returns count found. */
uint32_t hal_bus_find_by_class(uint8_t class_code, uint8_t subclass,
                                hal_device_t *out, uint32_t max);

/* Find devices by vendor/device ID.  Returns count found. */
uint32_t hal_bus_find_by_id(uint16_t vendor, uint16_t device,
                             hal_device_t *out, uint32_t max);

#endif /* ALJEFRA_HAL_BUS_H */
