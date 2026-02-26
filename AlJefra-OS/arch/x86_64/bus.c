/* SPDX-License-Identifier: MIT */
/* AlJefra OS — x86-64 Bus HAL Implementation (Standalone)
 *
 * PCIe config space access via legacy I/O ports 0xCF8/0xCFC.
 * No dependency on AlJefra b_system().
 */

#include "../../hal/hal.h"

/* PCI Configuration Space I/O Ports */
#define PCI_CONFIG_ADDR   0x0CF8
#define PCI_CONFIG_DATA   0x0CFC

/* PCI configuration register offsets */
#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_REVISION_ID     0x08
#define PCI_CLASS_CODE      0x08  /* Byte 3 = class, byte 2 = subclass, byte 1 = prog IF */
#define PCI_CACHE_LINE_SIZE 0x0C
#define PCI_HEADER_TYPE     0x0E
#define PCI_BAR0            0x10
#define PCI_BAR1            0x14
#define PCI_BAR2            0x18
#define PCI_BAR3            0x1C
#define PCI_BAR4            0x20
#define PCI_BAR5            0x24
#define PCI_INTERRUPT_LINE  0x3C

/* PCI command register bits */
#define PCI_CMD_IO_SPACE      (1u << 0)
#define PCI_CMD_MEMORY_SPACE  (1u << 1)
#define PCI_CMD_BUS_MASTER    (1u << 2)

/* -------------------------------------------------------------------------- */
/* Port I/O helpers                                                           */
/* -------------------------------------------------------------------------- */

static inline uint32_t inl(uint16_t port)
{
    uint32_t val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* -------------------------------------------------------------------------- */
/* PCI Config Space Access (0xCF8/0xCFC)                                      */
/* -------------------------------------------------------------------------- */

/* Build PCI config address for 0xCF8 port:
 * Bit 31    = Enable (1)
 * Bits 23:16 = Bus
 * Bits 15:11 = Device
 * Bits 10:8  = Function
 * Bits 7:2   = Register (dword aligned)
 * Bits 1:0   = 0
 */
static inline uint32_t pci_cf8_addr(uint32_t bus, uint32_t dev, uint32_t func, uint32_t reg)
{
    return (1u << 31) | (bus << 16) | (dev << 11) | (func << 8) | (reg & 0xFC);
}

static inline uint32_t pci_read32(uint32_t bus, uint32_t dev, uint32_t func, uint32_t reg)
{
    outl(PCI_CONFIG_ADDR, pci_cf8_addr(bus, dev, func, reg));
    return inl(PCI_CONFIG_DATA);
}

static inline void pci_write32(uint32_t bus, uint32_t dev, uint32_t func, uint32_t reg, uint32_t val)
{
    outl(PCI_CONFIG_ADDR, pci_cf8_addr(bus, dev, func, reg));
    outl(PCI_CONFIG_DATA, val);
}

/* -------------------------------------------------------------------------- */
/* BAR Probing                                                                */
/* -------------------------------------------------------------------------- */

static void probe_bar(uint32_t bus, uint32_t dev, uint32_t func,
                       uint32_t bar_reg, uint64_t *base_out, uint64_t *size_out)
{
    uint32_t orig = pci_read32(bus, dev, func, bar_reg);

    if (orig & 1) {
        /* I/O BAR */
        *base_out = orig & ~0x03u;
        pci_write32(bus, dev, func, bar_reg, 0xFFFFFFFF);
        uint32_t mask = pci_read32(bus, dev, func, bar_reg);
        pci_write32(bus, dev, func, bar_reg, orig);
        mask &= ~0x03u;
        *size_out = (mask == 0) ? 0 : (~mask) + 1;
    } else {
        /* Memory BAR */
        uint32_t type = (orig >> 1) & 0x03;

        if (type == 0x02) {
            /* 64-bit BAR */
            uint32_t orig_hi = pci_read32(bus, dev, func, bar_reg + 4);
            *base_out = ((uint64_t)orig_hi << 32) | (orig & ~0x0Fu);

            pci_write32(bus, dev, func, bar_reg, 0xFFFFFFFF);
            pci_write32(bus, dev, func, bar_reg + 4, 0xFFFFFFFF);
            uint32_t mask_lo = pci_read32(bus, dev, func, bar_reg);
            uint32_t mask_hi = pci_read32(bus, dev, func, bar_reg + 4);
            pci_write32(bus, dev, func, bar_reg, orig);
            pci_write32(bus, dev, func, bar_reg + 4, orig_hi);

            uint64_t mask64 = ((uint64_t)mask_hi << 32) | (mask_lo & ~0x0FUL);
            *size_out = (mask64 == 0) ? 0 : (~mask64) + 1;
        } else {
            /* 32-bit BAR */
            *base_out = orig & ~0x0Fu;
            pci_write32(bus, dev, func, bar_reg, 0xFFFFFFFF);
            uint32_t mask = pci_read32(bus, dev, func, bar_reg);
            pci_write32(bus, dev, func, bar_reg, orig);
            mask &= ~0x0Fu;
            *size_out = (mask == 0) ? 0 : (~mask) + 1;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* HAL Bus API                                                                */
/* -------------------------------------------------------------------------- */

hal_status_t hal_bus_init(void)
{
    /* PCI config space via 0xCF8/0xCFC is available immediately */
    return HAL_OK;
}

uint32_t hal_bus_scan(hal_device_t *devs, uint32_t max)
{
    uint32_t count = 0;

    for (uint32_t bus = 0; bus < 256 && count < max; bus++) {
        for (uint32_t dev = 0; dev < 32 && count < max; dev++) {
            for (uint32_t func = 0; func < 8 && count < max; func++) {
                uint32_t id = pci_read32(bus, dev, func, PCI_VENDOR_ID);
                uint16_t vendor = id & 0xFFFF;
                uint16_t device = (id >> 16) & 0xFFFF;

                if (vendor == 0xFFFF || vendor == 0x0000)
                    continue;

                hal_device_t *d = &devs[count];
                for (uint64_t i = 0; i < sizeof(hal_device_t); i++)
                    ((uint8_t *)d)[i] = 0;

                d->bus_type  = HAL_BUS_PCIE;
                d->vendor_id = vendor;
                d->device_id = device;
                d->bus       = (uint16_t)bus;
                d->dev       = (uint8_t)dev;
                d->func      = (uint8_t)func;

                uint32_t class_reg = pci_read32(bus, dev, func, PCI_CLASS_CODE);
                d->class_code = (class_reg >> 24) & 0xFF;
                d->subclass   = (class_reg >> 16) & 0xFF;
                d->prog_if    = (class_reg >> 8)  & 0xFF;

                uint32_t int_reg = pci_read32(bus, dev, func, PCI_INTERRUPT_LINE);
                d->irq = (uint8_t)(int_reg & 0xFF);

                /* Probe BARs */
                uint32_t header = pci_read32(bus, dev, func, PCI_CACHE_LINE_SIZE);
                uint8_t header_type = (header >> 16) & 0x7F;

                int max_bars = (header_type == 0) ? 6 : 2;
                for (int b = 0; b < max_bars && b < HAL_BUS_MAX_BARS; b++) {
                    uint32_t bar_reg = PCI_BAR0 + (b * 4);
                    probe_bar(bus, dev, func, bar_reg,
                              &d->bar[b], &d->bar_size[b]);

                    uint32_t bar_val = pci_read32(bus, dev, func, bar_reg);
                    if (!(bar_val & 1) && (((bar_val >> 1) & 0x03) == 0x02)) {
                        b++; /* Skip upper 32 bits of 64-bit BAR */
                    }
                }

                count++;

                /* If not multi-function, only check function 0 */
                if (func == 0) {
                    uint32_t ht = pci_read32(bus, dev, func, PCI_CACHE_LINE_SIZE);
                    if (!((ht >> 16) & 0x80))
                        break;
                }
            }
        }
    }

    return count;
}

uint32_t hal_bus_pci_read32(uint32_t bdf, uint32_t reg)
{
    uint32_t bus  = (bdf >> 8) & 0xFF;
    uint32_t dev  = (bdf >> 3) & 0x1F;
    uint32_t func = bdf & 0x07;
    return pci_read32(bus, dev, func, reg);
}

void hal_bus_pci_write32(uint32_t bdf, uint32_t reg, uint32_t val)
{
    uint32_t bus  = (bdf >> 8) & 0xFF;
    uint32_t dev  = (bdf >> 3) & 0x1F;
    uint32_t func = bdf & 0x07;
    pci_write32(bus, dev, func, reg, val);
}

void hal_bus_pci_enable(hal_device_t *dev)
{
    uint32_t cmd = pci_read32(dev->bus, dev->dev, dev->func, PCI_COMMAND);
    cmd |= PCI_CMD_MEMORY_SPACE | PCI_CMD_BUS_MASTER;
    pci_write32(dev->bus, dev->dev, dev->func, PCI_COMMAND, cmd);
}

volatile void *hal_bus_map_bar(hal_device_t *dev, uint32_t bar_index)
{
    if (bar_index >= HAL_BUS_MAX_BARS)
        return (volatile void *)0;

    uint64_t phys = dev->bar[bar_index];
    if (phys == 0)
        return (volatile void *)0;

    return (volatile void *)(uintptr_t)phys;
}

uint32_t hal_bus_find_by_class(uint8_t class_code, uint8_t subclass,
                                hal_device_t *out, uint32_t max)
{
    hal_device_t all_devs[HAL_BUS_MAX_DEVICES];
    uint32_t total = hal_bus_scan(all_devs, HAL_BUS_MAX_DEVICES);
    uint32_t found = 0;

    for (uint32_t i = 0; i < total && found < max; i++) {
        if (all_devs[i].class_code == class_code &&
            all_devs[i].subclass == subclass) {
            uint8_t *dst = (uint8_t *)&out[found];
            uint8_t *src = (uint8_t *)&all_devs[i];
            for (uint64_t j = 0; j < sizeof(hal_device_t); j++)
                dst[j] = src[j];
            found++;
        }
    }

    return found;
}

uint32_t hal_bus_find_by_id(uint16_t vendor, uint16_t device,
                             hal_device_t *out, uint32_t max)
{
    hal_device_t all_devs[HAL_BUS_MAX_DEVICES];
    uint32_t total = hal_bus_scan(all_devs, HAL_BUS_MAX_DEVICES);
    uint32_t found = 0;

    for (uint32_t i = 0; i < total && found < max; i++) {
        if (all_devs[i].vendor_id == vendor &&
            all_devs[i].device_id == device) {
            uint8_t *dst = (uint8_t *)&out[found];
            uint8_t *src = (uint8_t *)&all_devs[i];
            for (uint64_t j = 0; j < sizeof(hal_device_t); j++)
                dst[j] = src[j];
            found++;
        }
    }

    return found;
}
