/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- RISC-V 64-bit I/O Implementation
 * Implements hal/io.h for RISC-V.
 *
 * RISC-V has no port-based I/O; all device access is memory-mapped.
 * Port I/O functions are no-ops that return 0.
 */

#include "../../hal/hal.h"

/* ------------------------------------------------------------------ */
/* Port I/O: no-ops on RISC-V                                         */
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
    /* FENCE: order all memory operations.
     * fence iorw, iorw ensures device MMIO ordering.
     * fence.tso could be used for weaker TSO ordering. */
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

/* ------------------------------------------------------------------ */
/* DMA Allocator                                                       */
/* ------------------------------------------------------------------ */

/* Simple bump allocator for DMA buffers.
 * On freestanding with identity mapping, virtual == physical. */

#define DMA_POOL_BASE   0x88000000ULL   /* Above 2GB mark (after normal RAM) */
#define DMA_POOL_SIZE   (16 * 1024 * 1024)  /* 16MB DMA pool */
#define DMA_ALIGN       4096

static uint64_t dma_next = DMA_POOL_BASE;

void *hal_dma_alloc(uint64_t size, uint64_t *phys)
{
    /* Align size up */
    size = (size + DMA_ALIGN - 1) & ~((uint64_t)DMA_ALIGN - 1);

    uint64_t addr = dma_next;

    if ((addr + size) > (DMA_POOL_BASE + DMA_POOL_SIZE)) {
        if (phys) *phys = 0;
        return (void *)0;
    }

    dma_next = addr + size;

    if (phys) *phys = addr;

    /* Zero the allocated region */
    volatile uint8_t *p = (volatile uint8_t *)addr;
    for (uint64_t i = 0; i < size; i++)
        p[i] = 0;

    return (void *)addr;
}

void hal_dma_free(void *virt, uint64_t size)
{
    uint64_t addr = (uint64_t)virt;
    uint64_t aligned_size = (size + DMA_ALIGN - 1) & ~((uint64_t)DMA_ALIGN - 1);

    /* Reclaim if this was the last allocation */
    if (addr + aligned_size == dma_next)
        dma_next = addr;
}
