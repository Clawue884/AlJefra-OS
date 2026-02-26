/* SPDX-License-Identifier: MIT */
/* AlJefra OS — HAL I/O Interface
 * Memory-mapped I/O and (on x86) port I/O wrappers with proper barriers.
 */

#ifndef ALJEFRA_HAL_IO_H
#define ALJEFRA_HAL_IO_H

#include <stdint.h>

/* ── Memory-Mapped I/O (all architectures) ── */

static inline uint8_t hal_mmio_read8(volatile void *addr)
{
    uint8_t v = *(volatile uint8_t *)addr;
    __asm__ volatile("" ::: "memory");
    return v;
}

static inline uint16_t hal_mmio_read16(volatile void *addr)
{
    uint16_t v = *(volatile uint16_t *)addr;
    __asm__ volatile("" ::: "memory");
    return v;
}

static inline uint32_t hal_mmio_read32(volatile void *addr)
{
    uint32_t v = *(volatile uint32_t *)addr;
    __asm__ volatile("" ::: "memory");
    return v;
}

static inline uint64_t hal_mmio_read64(volatile void *addr)
{
    uint64_t v = *(volatile uint64_t *)addr;
    __asm__ volatile("" ::: "memory");
    return v;
}

static inline void hal_mmio_write8(volatile void *addr, uint8_t val)
{
    __asm__ volatile("" ::: "memory");
    *(volatile uint8_t *)addr = val;
}

static inline void hal_mmio_write16(volatile void *addr, uint16_t val)
{
    __asm__ volatile("" ::: "memory");
    *(volatile uint16_t *)addr = val;
}

static inline void hal_mmio_write32(volatile void *addr, uint32_t val)
{
    __asm__ volatile("" ::: "memory");
    *(volatile uint32_t *)addr = val;
}

static inline void hal_mmio_write64(volatile void *addr, uint64_t val)
{
    __asm__ volatile("" ::: "memory");
    *(volatile uint64_t *)addr = val;
}

/* Full memory barrier (arch-specific implementation) */
void hal_mmio_barrier(void);

/* ── Port I/O (x86-64 only, no-ops on other architectures) ── */

uint8_t  hal_port_in8(uint16_t port);
uint16_t hal_port_in16(uint16_t port);
uint32_t hal_port_in32(uint16_t port);
void     hal_port_out8(uint16_t port, uint8_t val);
void     hal_port_out16(uint16_t port, uint16_t val);
void     hal_port_out32(uint16_t port, uint32_t val);

/* ── DMA helpers ── */

/* Allocate a physically-contiguous DMA buffer.
 * Returns virtual address; physical address written to *phys.
 * On bare-metal with identity mapping, virt == phys.
 */
void *hal_dma_alloc(uint64_t size, uint64_t *phys);

/* Free a DMA buffer */
void hal_dma_free(void *virt, uint64_t size);

#endif /* ALJEFRA_HAL_IO_H */
