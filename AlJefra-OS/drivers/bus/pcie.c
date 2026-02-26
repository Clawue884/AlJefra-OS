/* SPDX-License-Identifier: MIT */
/* AlJefra OS — PCIe Bus Enumeration Driver Implementation
 * ECAM-based PCIe enumeration with legacy PCI fallback.
 */

#include "pcie.h"

/* ── ECAM address calculation ── */
/* ECAM: config space for bus/dev/func/reg is at:
 * ecam_base + ((bus - start_bus) << 20 | dev << 15 | func << 12 | reg) */

static volatile void *pcie_ecam_addr(pcie_enum_t *pcie, uint16_t bus,
                                      uint8_t dev, uint8_t func, uint16_t reg)
{
    uint64_t offset = ((uint64_t)(bus - pcie->start_bus) << 20) |
                      ((uint64_t)dev << 15) |
                      ((uint64_t)func << 12) |
                      (reg & 0xFFF);
    return (volatile void *)((uint8_t *)pcie->ecam_base + offset);
}

/* ── Legacy PCI config access (x86 only, via 0xCF8/0xCFC) ── */

static uint32_t pcie_legacy_read32(uint16_t bus, uint8_t dev, uint8_t func,
                                    uint16_t reg)
{
    uint32_t addr = (1u << 31) |                    /* Enable bit */
                    ((uint32_t)(bus & 0xFF) << 16) |
                    ((uint32_t)(dev & 0x1F) << 11) |
                    ((uint32_t)(func & 0x07) << 8) |
                    (reg & 0xFC);
    hal_port_out32(PCI_CONFIG_ADDR, addr);
    return hal_port_in32(PCI_CONFIG_DATA);
}

static void pcie_legacy_write32(uint16_t bus, uint8_t dev, uint8_t func,
                                 uint16_t reg, uint32_t val)
{
    uint32_t addr = (1u << 31) |
                    ((uint32_t)(bus & 0xFF) << 16) |
                    ((uint32_t)(dev & 0x1F) << 11) |
                    ((uint32_t)(func & 0x07) << 8) |
                    (reg & 0xFC);
    hal_port_out32(PCI_CONFIG_ADDR, addr);
    hal_port_out32(PCI_CONFIG_DATA, val);
}

/* ── Config read/write (ECAM or legacy) ── */

uint32_t pcie_config_read32(pcie_enum_t *pcie, uint16_t bus, uint8_t dev,
                             uint8_t func, uint16_t reg)
{
    if (pcie->use_ecam) {
        volatile void *addr = pcie_ecam_addr(pcie, bus, dev, func, reg);
        return hal_mmio_read32(addr);
    } else {
        return pcie_legacy_read32(bus, dev, func, reg);
    }
}

void pcie_config_write32(pcie_enum_t *pcie, uint16_t bus, uint8_t dev,
                          uint8_t func, uint16_t reg, uint32_t val)
{
    if (pcie->use_ecam) {
        volatile void *addr = pcie_ecam_addr(pcie, bus, dev, func, reg);
        hal_mmio_write32(addr, val);
    } else {
        pcie_legacy_write32(bus, dev, func, reg, val);
    }
}

/* ── BAR sizing ── */

static uint64_t pcie_size_bar(pcie_enum_t *pcie, uint16_t bus, uint8_t dev,
                               uint8_t func, uint32_t bar_offset)
{
    /* Save original BAR value */
    uint32_t orig = pcie_config_read32(pcie, bus, dev, func, bar_offset);

    /* Write all 1s */
    pcie_config_write32(pcie, bus, dev, func, bar_offset, 0xFFFFFFFF);

    /* Read back to determine size */
    uint32_t mask = pcie_config_read32(pcie, bus, dev, func, bar_offset);

    /* Restore original */
    pcie_config_write32(pcie, bus, dev, func, bar_offset, orig);

    if (mask == 0 || mask == 0xFFFFFFFF)
        return 0;

    /* Memory BAR: bits [3:0] are type/prefetchable flags */
    if (!(orig & 1)) {
        mask &= ~0x0F;  /* Clear lower 4 bits */
        if (mask == 0)
            return 0;
        return (~mask) + 1;
    } else {
        /* I/O BAR: bits [1:0] are type flag */
        mask &= ~0x03;
        if (mask == 0)
            return 0;
        return (~mask + 1) & 0xFFFF;
    }
}

/* ── Scan a single device/function ── */

static bool pcie_scan_func(pcie_enum_t *pcie, uint16_t bus, uint8_t dev,
                            uint8_t func, hal_device_t *out)
{
    uint32_t id_reg = pcie_config_read32(pcie, bus, dev, func, PCI_VENDOR_ID);
    uint16_t vendor = (uint16_t)(id_reg & 0xFFFF);
    uint16_t device = (uint16_t)(id_reg >> 16);

    if (vendor == 0xFFFF || vendor == 0x0000)
        return false;

    out->bus_type = HAL_BUS_PCIE;
    out->vendor_id = vendor;
    out->device_id = device;
    out->bus = bus;
    out->dev = dev;
    out->func = func;

    /* Read class/subclass/prog_if */
    uint32_t class_reg = pcie_config_read32(pcie, bus, dev, func, PCI_REVISION_ID);
    out->prog_if    = (uint8_t)((class_reg >> 8) & 0xFF);
    out->subclass   = (uint8_t)((class_reg >> 16) & 0xFF);
    out->class_code = (uint8_t)((class_reg >> 24) & 0xFF);

    /* Read interrupt line */
    uint32_t int_reg = pcie_config_read32(pcie, bus, dev, func, PCI_INTERRUPT_LINE);
    out->irq = (uint8_t)(int_reg & 0xFF);

    /* Read BARs */
    for (uint32_t i = 0; i < 6; i++) {
        uint32_t bar_offset = PCI_BAR0 + i * 4;
        uint32_t bar_val = pcie_config_read32(pcie, bus, dev, func, bar_offset);

        if (bar_val == 0) {
            out->bar[i] = 0;
            out->bar_size[i] = 0;
            continue;
        }

        if (bar_val & 1) {
            /* I/O BAR */
            out->bar[i] = bar_val & ~0x03ULL;
        } else {
            /* Memory BAR */
            uint8_t type = (bar_val >> 1) & 0x03;
            out->bar[i] = bar_val & ~0x0FULL;

            if (type == 0x02) {
                /* 64-bit BAR: read upper 32 bits from next BAR */
                if (i < 5) {
                    uint32_t bar_hi = pcie_config_read32(pcie, bus, dev, func,
                                                          bar_offset + 4);
                    out->bar[i] |= ((uint64_t)bar_hi << 32);
                    i++;  /* Skip next BAR slot */
                    out->bar[i] = 0;
                    out->bar_size[i] = 0;
                }
            }
        }

        out->bar_size[i > 0 && ((pcie_config_read32(pcie, bus, dev, func,
                        PCI_BAR0 + (i-1)*4) >> 1) & 0x03) == 0x02 ? i-1 : i]
            = pcie_size_bar(pcie, bus, dev, func, bar_offset);
    }

    /* Re-read BARs properly for sizes (the loop above is a first pass) */
    for (uint32_t i = 0; i < 6; i++) {
        uint32_t bar_offset = PCI_BAR0 + i * 4;
        uint32_t bar_val = pcie_config_read32(pcie, bus, dev, func, bar_offset);

        if (bar_val == 0) {
            out->bar_size[i] = 0;
            continue;
        }

        out->bar_size[i] = pcie_size_bar(pcie, bus, dev, func, bar_offset);

        /* Skip the high part of 64-bit BARs */
        if (!(bar_val & 1) && ((bar_val >> 1) & 0x03) == 0x02)
            i++;
    }

    out->compatible[0] = '\0';

    return true;
}

/* ── Public API ── */

hal_status_t pcie_init(pcie_enum_t *pcie, uint64_t ecam_base)
{
    pcie->initialized = false;
    pcie->seg = 0;
    pcie->start_bus = 0;
    pcie->end_bus = 255;

    if (ecam_base != 0) {
        /* Use provided ECAM base — map it */
        /* ECAM for 256 buses = 256 * 32 * 8 * 4096 = 256MB */
        pcie->ecam_base = (volatile void *)(uintptr_t)ecam_base;
        pcie->use_ecam = true;
    } else if (hal_arch() == HAL_ARCH_X86_64) {
        /* Try legacy PCI config space */
        hal_port_out32(PCI_CONFIG_ADDR, 0x80000000);
        uint32_t test = hal_port_in32(PCI_CONFIG_ADDR);
        if (test == 0x80000000) {
            pcie->use_ecam = false;
            pcie->ecam_base = NULL;
        } else {
            return HAL_NO_DEVICE;
        }
    } else {
        return HAL_NO_DEVICE;
    }

    pcie->initialized = true;
    return HAL_OK;
}

uint32_t pcie_scan(pcie_enum_t *pcie, hal_device_t *devs, uint32_t max)
{
    if (!pcie->initialized)
        return 0;

    uint32_t count = 0;

    for (uint16_t bus = pcie->start_bus; bus <= pcie->end_bus && count < max; bus++) {
        for (uint8_t dev = 0; dev < 32 && count < max; dev++) {
            /* Check function 0 first */
            uint32_t id_reg = pcie_config_read32(pcie, bus, dev, 0, PCI_VENDOR_ID);
            if ((id_reg & 0xFFFF) == 0xFFFF)
                continue;

            /* Check if multi-function device */
            uint32_t hdr_reg = pcie_config_read32(pcie, bus, dev, 0, PCI_HEADER_TYPE);
            uint8_t header_type = (uint8_t)((hdr_reg >> 16) & 0xFF);
            uint8_t max_func = (header_type & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < max_func && count < max; func++) {
                if (pcie_scan_func(pcie, bus, dev, func, &devs[count]))
                    count++;
            }
        }
    }

    return count;
}

void pcie_enable_device(pcie_enum_t *pcie, hal_device_t *dev)
{
    uint32_t cmd = pcie_config_read32(pcie, dev->bus, dev->dev, dev->func,
                                       PCI_COMMAND);
    cmd |= PCI_CMD_MEM_SPACE | PCI_CMD_BUS_MASTER;
    cmd &= ~PCI_CMD_INT_DISABLE;
    pcie_config_write32(pcie, dev->bus, dev->dev, dev->func, PCI_COMMAND, cmd);
}

uint64_t pcie_bar_size(pcie_enum_t *pcie, hal_device_t *dev, uint32_t bar_idx)
{
    if (bar_idx >= 6)
        return 0;
    return pcie_size_bar(pcie, dev->bus, dev->dev, dev->func,
                         PCI_BAR0 + bar_idx * 4);
}

bool pcie_find_msix(pcie_enum_t *pcie, hal_device_t *dev, pcie_msix_cap_t *cap)
{
    /* Walk capability list */
    uint32_t status = pcie_config_read32(pcie, dev->bus, dev->dev, dev->func,
                                          PCI_STATUS);
    if (!((status >> 16) & (1u << 4)))  /* Capabilities List bit */
        return false;

    uint8_t cap_ptr = (uint8_t)(pcie_config_read32(pcie, dev->bus, dev->dev,
                                                     dev->func, PCI_CAP_PTR) & 0xFF);

    while (cap_ptr != 0) {
        uint32_t cap_hdr = pcie_config_read32(pcie, dev->bus, dev->dev,
                                               dev->func, cap_ptr);
        uint8_t cap_id = (uint8_t)(cap_hdr & 0xFF);

        if (cap_id == PCI_CAP_MSIX) {
            uint16_t msg_ctrl = (uint16_t)((cap_hdr >> 16) & 0xFFFF);
            cap->table_size = (msg_ctrl & 0x7FF) + 1;

            uint32_t table_reg = pcie_config_read32(pcie, dev->bus, dev->dev,
                                                     dev->func, cap_ptr + 4);
            cap->table_bar = (uint8_t)(table_reg & 0x07);
            cap->table_offset = table_reg & ~0x07u;

            uint32_t pba_reg = pcie_config_read32(pcie, dev->bus, dev->dev,
                                                    dev->func, cap_ptr + 8);
            cap->pba_bar = (uint8_t)(pba_reg & 0x07);
            cap->pba_offset = pba_reg & ~0x07u;

            return true;
        }

        cap_ptr = (uint8_t)((cap_hdr >> 8) & 0xFF);
    }

    return false;
}

uint32_t pcie_find_by_class(pcie_enum_t *pcie, uint8_t class_code,
                             uint8_t subclass, hal_device_t *out, uint32_t max)
{
    if (!pcie->initialized)
        return 0;

    uint32_t count = 0;

    for (uint16_t bus = pcie->start_bus; bus <= pcie->end_bus && count < max; bus++) {
        for (uint8_t dev = 0; dev < 32 && count < max; dev++) {
            uint32_t id_reg = pcie_config_read32(pcie, bus, dev, 0, PCI_VENDOR_ID);
            if ((id_reg & 0xFFFF) == 0xFFFF)
                continue;

            uint32_t hdr_reg = pcie_config_read32(pcie, bus, dev, 0, PCI_HEADER_TYPE);
            uint8_t max_func = ((hdr_reg >> 16) & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < max_func && count < max; func++) {
                uint32_t class_reg = pcie_config_read32(pcie, bus, dev, func,
                                                         PCI_REVISION_ID);
                uint8_t cc = (uint8_t)((class_reg >> 24) & 0xFF);
                uint8_t sc = (uint8_t)((class_reg >> 16) & 0xFF);

                if (cc == class_code && sc == subclass) {
                    if (pcie_scan_func(pcie, bus, dev, func, &out[count]))
                        count++;
                }
            }
        }
    }

    return count;
}

uint32_t pcie_find_by_id(pcie_enum_t *pcie, uint16_t vendor, uint16_t device,
                          hal_device_t *out, uint32_t max)
{
    if (!pcie->initialized)
        return 0;

    uint32_t count = 0;

    for (uint16_t bus = pcie->start_bus; bus <= pcie->end_bus && count < max; bus++) {
        for (uint8_t dev = 0; dev < 32 && count < max; dev++) {
            uint32_t id_reg = pcie_config_read32(pcie, bus, dev, 0, PCI_VENDOR_ID);
            if ((id_reg & 0xFFFF) == 0xFFFF)
                continue;

            uint32_t hdr_reg = pcie_config_read32(pcie, bus, dev, 0, PCI_HEADER_TYPE);
            uint8_t max_func = ((hdr_reg >> 16) & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < max_func && count < max; func++) {
                uint32_t id = pcie_config_read32(pcie, bus, dev, func, PCI_VENDOR_ID);
                uint16_t vid = (uint16_t)(id & 0xFFFF);
                uint16_t did = (uint16_t)(id >> 16);

                if (vid == vendor && did == device) {
                    if (pcie_scan_func(pcie, bus, dev, func, &out[count]))
                        count++;
                }
            }
        }
    }

    return count;
}
