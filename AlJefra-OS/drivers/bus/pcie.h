/* SPDX-License-Identifier: MIT */
/* AlJefra OS — PCIe Bus Enumeration Driver
 * ECAM-based PCIe enumeration with legacy PCI fallback on x86.
 * Architecture-independent; uses HAL for all hardware access.
 */

#ifndef ALJEFRA_DRV_PCIE_H
#define ALJEFRA_DRV_PCIE_H

#include "../../hal/hal.h"

/* ── PCI Configuration Space offsets ── */
#define PCI_VENDOR_ID       0x00    /* Vendor ID (16-bit) */
#define PCI_DEVICE_ID       0x02    /* Device ID (16-bit) */
#define PCI_COMMAND         0x04    /* Command Register (16-bit) */
#define PCI_STATUS          0x06    /* Status Register (16-bit) */
#define PCI_REVISION_ID     0x08    /* Revision ID (8-bit) */
#define PCI_PROG_IF         0x09    /* Programming Interface (8-bit) */
#define PCI_SUBCLASS        0x0A    /* Subclass (8-bit) */
#define PCI_CLASS_CODE      0x0B    /* Class Code (8-bit) */
#define PCI_CACHE_LINE      0x0C    /* Cache Line Size (8-bit) */
#define PCI_LATENCY_TIMER   0x0D    /* Latency Timer (8-bit) */
#define PCI_HEADER_TYPE     0x0E    /* Header Type (8-bit) */
#define PCI_BAR0            0x10    /* Base Address Register 0 */
#define PCI_BAR1            0x14
#define PCI_BAR2            0x18
#define PCI_BAR3            0x1C
#define PCI_BAR4            0x20
#define PCI_BAR5            0x24
#define PCI_SUBSYS_VENDOR   0x2C    /* Subsystem Vendor ID (16-bit) */
#define PCI_SUBSYS_ID       0x2E    /* Subsystem ID (16-bit) */
#define PCI_INTERRUPT_LINE  0x3C    /* Interrupt Line (8-bit) */
#define PCI_INTERRUPT_PIN   0x3D    /* Interrupt Pin (8-bit) */
#define PCI_CAP_PTR         0x34    /* Capabilities Pointer (8-bit) */

/* ── PCI Command register bits ── */
#define PCI_CMD_IO_SPACE    (1u << 0)   /* I/O Space Enable */
#define PCI_CMD_MEM_SPACE   (1u << 1)   /* Memory Space Enable */
#define PCI_CMD_BUS_MASTER  (1u << 2)   /* Bus Mastering Enable */
#define PCI_CMD_INT_DISABLE (1u << 10)  /* Interrupt Disable */

/* ── PCI Capability IDs ── */
#define PCI_CAP_MSI         0x05    /* Message Signalled Interrupts */
#define PCI_CAP_VENDOR      0x09    /* Vendor Specific */
#define PCI_CAP_MSIX        0x11    /* MSI-X */
#define PCI_CAP_PCIE        0x10    /* PCI Express */

/* ── PCI Class Codes ── */
#define PCI_CLASS_STORAGE    0x01
#define PCI_CLASS_NETWORK    0x02
#define PCI_CLASS_DISPLAY    0x03
#define PCI_CLASS_BRIDGE     0x06
#define PCI_CLASS_INPUT      0x09
#define PCI_CLASS_SERIAL     0x0C    /* Serial Bus (USB, etc.) */

/* ── Storage Subclasses ── */
#define PCI_SUBCLASS_SCSI    0x00
#define PCI_SUBCLASS_IDE     0x01
#define PCI_SUBCLASS_AHCI    0x06
#define PCI_SUBCLASS_NVME    0x08

/* ── Network Subclasses ── */
#define PCI_SUBCLASS_ETHERNET 0x00

/* ── Serial Bus Subclasses ── */
#define PCI_SUBCLASS_USB     0x03

/* ── USB Prog IF ── */
#define PCI_PROGIF_UHCI      0x00
#define PCI_PROGIF_OHCI      0x10
#define PCI_PROGIF_EHCI      0x20
#define PCI_PROGIF_XHCI      0x30

/* ── Legacy PCI I/O ports (x86) ── */
#define PCI_CONFIG_ADDR      0x0CF8
#define PCI_CONFIG_DATA      0x0CFC

/* ── MSI-X Table Entry ── */
typedef struct __attribute__((packed)) {
    uint32_t msg_addr_lo;   /* Message Address (lower 32) */
    uint32_t msg_addr_hi;   /* Message Address (upper 32) */
    uint32_t msg_data;      /* Message Data */
    uint32_t vector_ctrl;   /* Vector Control (bit 0 = mask) */
} pcie_msix_entry_t;

/* ── MSI-X Capability ── */
typedef struct {
    uint16_t table_size;     /* Number of entries (0-based) */
    uint8_t  table_bar;      /* BAR index for table */
    uint32_t table_offset;   /* Offset within BAR */
    uint8_t  pba_bar;        /* BAR index for PBA */
    uint32_t pba_offset;     /* Offset within BAR */
} pcie_msix_cap_t;

/* ── PCIe enumeration state ── */
typedef struct {
    volatile void *ecam_base;   /* ECAM base address (NULL if not available) */
    uint16_t       seg;         /* PCI segment group */
    uint8_t        start_bus;   /* Start bus number for ECAM */
    uint8_t        end_bus;     /* End bus number for ECAM */
    bool           use_ecam;    /* true = ECAM, false = legacy PCI config */
    bool           initialized;
} pcie_enum_t;

/* ── Public API ── */

/* Initialize PCIe enumeration.
 * ecam_base: ECAM physical base address (0 to auto-detect or use legacy).
 * On x86 without ECAM, falls back to legacy PCI config space (0xCF8/0xCFC). */
hal_status_t pcie_init(pcie_enum_t *pcie, uint64_t ecam_base);

/* Scan all buses and return discovered devices.
 * Returns number of devices found. */
uint32_t pcie_scan(pcie_enum_t *pcie, hal_device_t *devs, uint32_t max);

/* Read a 32-bit PCI config register for a specific BDF */
uint32_t pcie_config_read32(pcie_enum_t *pcie, uint16_t bus, uint8_t dev,
                             uint8_t func, uint16_t reg);

/* Write a 32-bit PCI config register */
void pcie_config_write32(pcie_enum_t *pcie, uint16_t bus, uint8_t dev,
                          uint8_t func, uint16_t reg, uint32_t val);

/* Enable bus mastering + memory space for a device */
void pcie_enable_device(pcie_enum_t *pcie, hal_device_t *dev);

/* Size a BAR (returns size in bytes, 0 if not present) */
uint64_t pcie_bar_size(pcie_enum_t *pcie, hal_device_t *dev, uint32_t bar_idx);

/* Find MSI-X capability. Returns true if found. */
bool pcie_find_msix(pcie_enum_t *pcie, hal_device_t *dev, pcie_msix_cap_t *cap);

/* Find devices by class/subclass */
uint32_t pcie_find_by_class(pcie_enum_t *pcie, uint8_t class_code,
                             uint8_t subclass, hal_device_t *out, uint32_t max);

/* Find devices by vendor/device ID */
uint32_t pcie_find_by_id(pcie_enum_t *pcie, uint16_t vendor, uint16_t device,
                          hal_device_t *out, uint32_t max);

#endif /* ALJEFRA_DRV_PCIE_H */
