/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- AArch64 Bus / Device Discovery
 * Implements hal/bus.h for AArch64 platforms.
 *
 * ARM platforms typically use:
 *   - PCIe via ECAM (Enhanced Configuration Access Mechanism)
 *   - Device Tree for non-PCIe peripherals
 *
 * QEMU virt machine PCIe ECAM: 0x4010000000 (bus 0-255)
 * QEMU virt machine MMIO base: 0x3EFF0000 (high-MMIO aperture)
 */

#include "../../hal/hal.h"

/* ------------------------------------------------------------------ */
/* PCIe ECAM Configuration                                             */
/* ------------------------------------------------------------------ */

/* ECAM: config space is memory-mapped at base + (bus<<20 | dev<<15 | func<<12 | reg) */
#define ECAM_BASE       0x4010000000ULL   /* QEMU virt PCIe ECAM */
#define ECAM_BUS_MAX    256
#define ECAM_DEV_MAX    32
#define ECAM_FUNC_MAX   8

/* ------------------------------------------------------------------ */
/* Private state                                                       */
/* ------------------------------------------------------------------ */

static volatile uint8_t *ecam_base = (volatile uint8_t *)ECAM_BASE;
static hal_device_t device_cache[HAL_BUS_MAX_DEVICES];
static uint32_t device_count = 0;
static int bus_initialized = 0;

/* ------------------------------------------------------------------ */
/* ECAM helpers                                                        */
/* ------------------------------------------------------------------ */

static inline volatile uint8_t *ecam_addr(uint32_t bus, uint32_t dev,
                                           uint32_t func, uint32_t reg)
{
    uint64_t offset = ((uint64_t)bus << 20) |
                      ((uint64_t)dev << 15) |
                      ((uint64_t)func << 12) |
                      (uint64_t)reg;
    return ecam_base + offset;
}

static inline uint32_t ecam_read32(uint32_t bus, uint32_t dev,
                                    uint32_t func, uint32_t reg)
{
    volatile uint32_t *p = (volatile uint32_t *)ecam_addr(bus, dev, func, reg);
    uint32_t v = *p;
    __asm__ volatile("dmb ish" ::: "memory");
    return v;
}

static inline void ecam_write32(uint32_t bus, uint32_t dev,
                                 uint32_t func, uint32_t reg, uint32_t val)
{
    __asm__ volatile("dmb ish" ::: "memory");
    volatile uint32_t *p = (volatile uint32_t *)ecam_addr(bus, dev, func, reg);
    *p = val;
}

/* ------------------------------------------------------------------ */
/* BAR size detection                                                  */
/* ------------------------------------------------------------------ */

static uint64_t probe_bar_size(uint32_t bus, uint32_t dev, uint32_t func,
                                uint32_t bar_reg)
{
    uint32_t orig = ecam_read32(bus, dev, func, bar_reg);
    ecam_write32(bus, dev, func, bar_reg, 0xFFFFFFFF);
    uint32_t sized = ecam_read32(bus, dev, func, bar_reg);
    ecam_write32(bus, dev, func, bar_reg, orig);

    if (orig & 1) {
        /* I/O BAR */
        sized &= ~0x3;
        return (~sized) + 1;
    } else {
        /* Memory BAR */
        sized &= ~0xF;
        if (sized == 0) return 0;
        return (~sized) + 1;
    }
}

/* ------------------------------------------------------------------ */
/* PCIe BAR assignment                                                 */
/* ------------------------------------------------------------------ */

/* QEMU virt 32-bit PCIe MMIO window: 0x10000000 - 0x3EFEFFFF.
 * When booting with -kernel (no firmware), BARs are unassigned.
 * We assign them here from the MMIO window. */
#define PCIE_MMIO_BASE    0x10000000ULL
#define PCIE_MMIO_END     0x3EFF0000ULL
static uint64_t next_bar_addr = PCIE_MMIO_BASE;

static uint64_t align_up(uint64_t val, uint64_t align)
{
    return (val + align - 1) & ~(align - 1);
}

/* Assign a BAR address from the MMIO window if it's unassigned */
static void assign_bar(uint32_t bus, uint32_t dev, uint32_t func,
                        uint32_t bar_reg, uint64_t size, int is_64bit)
{
    if (size == 0 || size > (PCIE_MMIO_END - next_bar_addr))
        return;

    /* Align to BAR size (natural alignment) */
    next_bar_addr = align_up(next_bar_addr, size);
    if (next_bar_addr + size > PCIE_MMIO_END)
        return;

    /* Write low 32 bits (preserving type bits) */
    uint32_t bar_type = ecam_read32(bus, dev, func, bar_reg) & 0xF;
    ecam_write32(bus, dev, func, bar_reg,
                  (uint32_t)(next_bar_addr & 0xFFFFFFFF) | bar_type);

    if (is_64bit) {
        ecam_write32(bus, dev, func, bar_reg + 4,
                      (uint32_t)(next_bar_addr >> 32));
    }

    next_bar_addr += size;
}

/* ------------------------------------------------------------------ */
/* PCIe enumeration                                                    */
/* ------------------------------------------------------------------ */

static uint32_t pcie_scan(hal_device_t *devs, uint32_t max)
{
    uint32_t count = 0;

    for (uint32_t bus = 0; bus < ECAM_BUS_MAX && count < max; bus++) {
        for (uint32_t dev = 0; dev < ECAM_DEV_MAX && count < max; dev++) {
            for (uint32_t func = 0; func < ECAM_FUNC_MAX && count < max; func++) {
                uint32_t id = ecam_read32(bus, dev, func, 0x00);
                if (id == 0xFFFFFFFF || id == 0)
                    continue;

                hal_device_t *d = &devs[count];
                d->bus_type  = HAL_BUS_PCIE;
                d->vendor_id = id & 0xFFFF;
                d->device_id = (id >> 16) & 0xFFFF;

                uint32_t class_reg = ecam_read32(bus, dev, func, 0x08);
                d->class_code = (class_reg >> 24) & 0xFF;
                d->subclass   = (class_reg >> 16) & 0xFF;
                d->prog_if    = (class_reg >> 8)  & 0xFF;

                uint32_t intr = ecam_read32(bus, dev, func, 0x3C);
                d->irq = intr & 0xFF;

                d->bus  = bus;
                d->dev  = dev;
                d->func = func;

                /* Read and assign BARs */
                for (int bar = 0; bar < 6; bar++) {
                    uint32_t bar_reg = 0x10 + bar * 4;
                    uint32_t bar_val = ecam_read32(bus, dev, func, bar_reg);
                    int is_io = bar_val & 1;
                    int is_64bit = 0;

                    /* Probe BAR size */
                    uint64_t size = probe_bar_size(bus, dev, func, bar_reg);
                    d->bar_size[bar] = size;

                    if (is_io) {
                        /* I/O BAR — not used on ARM64 */
                        d->bar[bar] = bar_val & ~0x3ULL;
                    } else {
                        /* Memory BAR */
                        is_64bit = (((bar_val >> 1) & 3) == 2);
                        uint64_t addr = bar_val & ~0xFULL;

                        if (is_64bit && bar < 5) {
                            uint32_t hi = ecam_read32(bus, dev, func, bar_reg + 4);
                            addr |= ((uint64_t)hi << 32);
                        }

                        /* If BAR is unassigned (0), assign from MMIO window */
                        if (addr == 0 && size > 0) {
                            assign_bar(bus, dev, func, bar_reg, size, is_64bit);
                            /* Re-read after assignment */
                            bar_val = ecam_read32(bus, dev, func, bar_reg);
                            addr = bar_val & ~0xFULL;
                            if (is_64bit) {
                                uint32_t hi = ecam_read32(bus, dev, func, bar_reg + 4);
                                addr |= ((uint64_t)hi << 32);
                            }
                        }

                        d->bar[bar] = addr;

                        if (is_64bit && bar < 5) {
                            bar++;  /* Skip next BAR (used for upper 32 bits) */
                            d->bar[bar] = 0;
                            d->bar_size[bar] = 0;
                        }
                    }
                }

                d->compatible[0] = '\0';

                count++;

                /* If not multi-function and func==0, skip other functions */
                if (func == 0) {
                    uint32_t header = ecam_read32(bus, dev, func, 0x0C);
                    if (!((header >> 16) & 0x80))
                        break;
                }
            }
        }
    }

    return count;
}

/* ------------------------------------------------------------------ */
/* Device Tree enumeration placeholder                                 */
/* ------------------------------------------------------------------ */

/* TODO: Parse FDT (Flattened Device Tree) blob passed by firmware.
 * The DTB pointer is typically in x0 at EL1 entry on QEMU virt.
 * For now, we add hardcoded QEMU virt platform devices. */

static uint32_t dt_scan_qemu_virt(hal_device_t *devs, uint32_t start, uint32_t max)
{
    uint32_t count = start;

    if (count < max) {
        hal_device_t *d = &devs[count];
        d->bus_type  = HAL_BUS_DT;
        d->vendor_id = 0;
        d->device_id = 0;
        d->class_code = 0;
        d->subclass  = 0;
        d->prog_if   = 0;
        d->irq       = 33;  /* PL011 UART SPI 1 = GIC IRQ 33 */
        d->bar[0]    = 0x09000000;  /* PL011 MMIO base */
        d->bar_size[0] = 0x1000;
        for (int i = 1; i < HAL_BUS_MAX_BARS; i++) {
            d->bar[i] = 0;
            d->bar_size[i] = 0;
        }
        d->bus = 0; d->dev = 0; d->func = 0;
        /* Copy compatible string */
        const char *c = "arm,pl011";
        int j = 0;
        while (c[j] && j < 63) { d->compatible[j] = c[j]; j++; }
        d->compatible[j] = '\0';
        count++;
    }

    if (count < max) {
        hal_device_t *d = &devs[count];
        d->bus_type  = HAL_BUS_DT;
        d->vendor_id = 0;
        d->device_id = 0;
        d->class_code = 0;
        d->subclass  = 0;
        d->prog_if   = 0;
        d->irq       = 0;
        d->bar[0]    = 0x08000000;  /* GIC distributor */
        d->bar_size[0] = 0x10000;
        d->bar[1]    = 0x08010000;  /* GIC CPU interface (GICv2) */
        d->bar_size[1] = 0x10000;
        for (int i = 2; i < HAL_BUS_MAX_BARS; i++) {
            d->bar[i] = 0;
            d->bar_size[i] = 0;
        }
        d->bus = 0; d->dev = 0; d->func = 0;
        const char *c = "arm,gic-400";
        int j = 0;
        while (c[j] && j < 63) { d->compatible[j] = c[j]; j++; }
        d->compatible[j] = '\0';
        count++;
    }

    return count;
}

/* ------------------------------------------------------------------ */
/* HAL Interface Implementation                                        */
/* ------------------------------------------------------------------ */

hal_status_t hal_bus_init(void)
{
    device_count = 0;
    bus_initialized = 1;

    /* Scan PCIe ECAM */
    device_count = pcie_scan(device_cache, HAL_BUS_MAX_DEVICES);

    /* Add device-tree discovered devices */
    device_count = dt_scan_qemu_virt(device_cache, device_count, HAL_BUS_MAX_DEVICES);

    return HAL_OK;
}

uint32_t hal_bus_scan(hal_device_t *devs, uint32_t max)
{
    if (!bus_initialized)
        hal_bus_init();

    uint32_t n = (device_count < max) ? device_count : max;
    /* Manual copy */
    const char *src = (const char *)device_cache;
    char *dst = (char *)devs;
    for (uint64_t i = 0; i < n * sizeof(hal_device_t); i++)
        dst[i] = src[i];

    return n;
}

uint32_t hal_bus_pci_read32(uint32_t bdf, uint32_t reg)
{
    uint32_t bus  = (bdf >> 8) & 0xFF;
    uint32_t dev  = (bdf >> 3) & 0x1F;
    uint32_t func = bdf & 0x7;
    return ecam_read32(bus, dev, func, reg);
}

void hal_bus_pci_write32(uint32_t bdf, uint32_t reg, uint32_t val)
{
    uint32_t bus  = (bdf >> 8) & 0xFF;
    uint32_t dev  = (bdf >> 3) & 0x1F;
    uint32_t func = bdf & 0x7;
    ecam_write32(bus, dev, func, reg, val);
}

void hal_bus_pci_enable(hal_device_t *dev)
{
    uint32_t bdf = ((uint32_t)dev->bus << 8) | ((uint32_t)dev->dev << 3) | dev->func;
    uint32_t cmd = hal_bus_pci_read32(bdf, 0x04);
    cmd |= (1 << 1) | (1 << 2);  /* Memory Space + Bus Master */
    hal_bus_pci_write32(bdf, 0x04, cmd);
}

volatile void *hal_bus_map_bar(hal_device_t *dev, uint32_t bar_index)
{
    if (bar_index >= HAL_BUS_MAX_BARS)
        return (volatile void *)0;

    /* On freestanding with identity mapping, BAR physical == virtual */
    return (volatile void *)dev->bar[bar_index];
}

uint32_t hal_bus_find_by_class(uint8_t class_code, uint8_t subclass,
                                hal_device_t *out, uint32_t max)
{
    uint32_t found = 0;
    for (uint32_t i = 0; i < device_count && found < max; i++) {
        if (device_cache[i].class_code == class_code &&
            device_cache[i].subclass == subclass) {
            const char *src = (const char *)&device_cache[i];
            char *dst = (char *)&out[found];
            for (unsigned k = 0; k < sizeof(hal_device_t); k++)
                dst[k] = src[k];
            found++;
        }
    }
    return found;
}

uint32_t hal_bus_find_by_id(uint16_t vendor, uint16_t device,
                             hal_device_t *out, uint32_t max)
{
    uint32_t found = 0;
    for (uint32_t i = 0; i < device_count && found < max; i++) {
        if (device_cache[i].vendor_id == vendor &&
            device_cache[i].device_id == device) {
            const char *src = (const char *)&device_cache[i];
            char *dst = (char *)&out[found];
            for (unsigned k = 0; k < sizeof(hal_device_t); k++)
                dst[k] = src[k];
            found++;
        }
    }
    return found;
}
