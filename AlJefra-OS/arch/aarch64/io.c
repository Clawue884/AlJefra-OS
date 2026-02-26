/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- AArch64 I/O Implementation
 * Implements hal/io.h for AArch64.
 *
 * ARM has no port-based I/O (IN/OUT); all device access is memory-mapped.
 * Port I/O functions are no-ops that return 0.
 */

#include "../../hal/hal.h"

/* ------------------------------------------------------------------ */
/* Port I/O: no-ops on ARM                                             */
/* ------------------------------------------------------------------ */

uint8_t hal_port_in8(uint16_t port)
{
    (void)port;
    return 0;
}

uint16_t hal_port_in16(uint16_t port)
{
    (void)port;
    return 0;
}

uint32_t hal_port_in32(uint16_t port)
{
    (void)port;
    return 0;
}

void hal_port_out8(uint16_t port, uint8_t val)
{
    (void)port;
    (void)val;
}

void hal_port_out16(uint16_t port, uint16_t val)
{
    (void)port;
    (void)val;
}

void hal_port_out32(uint16_t port, uint32_t val)
{
    (void)port;
    (void)val;
}

/* ------------------------------------------------------------------ */
/* MMIO Barrier                                                        */
/* ------------------------------------------------------------------ */

void hal_mmio_barrier(void)
{
    /* DSB SY: full data synchronization barrier, system-wide.
     * Ensures all previous memory accesses (including device/MMIO)
     * are complete before any subsequent instructions execute. */
    __asm__ volatile("dsb sy" ::: "memory");
}

/* ------------------------------------------------------------------ */
/* DMA Allocator                                                       */
/* ------------------------------------------------------------------ */

/* Simple bump allocator for DMA buffers.
 * On freestanding with identity mapping, virtual == physical.
 * Aligned to 4KB page boundaries for DMA coherence. */

#define DMA_POOL_BASE   0x4E000000ULL   /* Near end of 256MB RAM (0x40000000+224MB) */
#define DMA_POOL_SIZE   (16 * 1024 * 1024)  /* 16MB DMA pool */
#define DMA_ALIGN       4096

static uint64_t dma_next = DMA_POOL_BASE;

void *hal_dma_alloc(uint64_t size, uint64_t *phys)
{
    /* Align size up to DMA_ALIGN */
    size = (size + DMA_ALIGN - 1) & ~((uint64_t)DMA_ALIGN - 1);

    uint64_t addr = dma_next;

    /* Check pool overflow */
    if ((addr + size) > (DMA_POOL_BASE + DMA_POOL_SIZE)) {
        if (phys) *phys = 0;
        return (void *)0;
    }

    dma_next = addr + size;

    /* On freestanding with identity mapping, VA == PA */
    if (phys) *phys = addr;

    /* Zero the allocated region (no memset in freestanding) */
    volatile uint8_t *p = (volatile uint8_t *)addr;
    for (uint64_t i = 0; i < size; i++)
        p[i] = 0;

    return (void *)addr;
}

void hal_dma_free(void *virt, uint64_t size)
{
    /* Simple bump allocator -- no real free.
     * If this is the last allocation, we can reclaim. */
    (void)size;
    uint64_t addr = (uint64_t)virt;
    uint64_t aligned_size = (size + DMA_ALIGN - 1) & ~((uint64_t)DMA_ALIGN - 1);

    if (addr + aligned_size == dma_next)
        dma_next = addr;
    /* Otherwise, the memory is leaked until reset */
}
