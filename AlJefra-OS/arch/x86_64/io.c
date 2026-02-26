/* SPDX-License-Identifier: MIT */
/* AlJefra OS — x86-64 I/O HAL Implementation
 * Port I/O (IN/OUT), MMIO barrier, and DMA bump allocator.
 */

#include "../../hal/hal.h"


/* -------------------------------------------------------------------------- */
/* Port I/O — x86-64 IN/OUT instructions                                     */
/* -------------------------------------------------------------------------- */

uint8_t hal_port_in8(uint16_t port)
{
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

uint16_t hal_port_in16(uint16_t port)
{
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

uint32_t hal_port_in32(uint16_t port)
{
    uint32_t val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

void hal_port_out8(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void hal_port_out16(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

void hal_port_out32(uint16_t port, uint32_t val)
{
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* -------------------------------------------------------------------------- */
/* MMIO Barrier                                                               */
/* -------------------------------------------------------------------------- */

void hal_mmio_barrier(void)
{
    __asm__ volatile ("mfence" ::: "memory");
}

/* -------------------------------------------------------------------------- */
/* DMA Allocator — Simple bump allocator                                      */
/*                                                                            */
/* On AlJefra with identity mapping (virt == phys), we allocate from a      */
/* reserved DMA region above the payload.  The region starts at 0x800000      */
/* (8 MB) which is well above the kernel (loaded at 0x100000) and payload     */
/* (loaded at 0x1E0000) but below the free memory pool.                       */
/*                                                                            */
/* This is intentionally simple: allocations are never truly freed,           */
/* just the bump pointer is advanced.  For a freestanding exokernel this        */
/* is acceptable — DMA buffers are typically long-lived.                       */
/* -------------------------------------------------------------------------- */

/* DMA region: 8MB to 16MB (8MB total) */
#define DMA_REGION_BASE   0x00800000ULL
#define DMA_REGION_SIZE   0x00800000ULL  /* 8 MB */
#define DMA_REGION_END    (DMA_REGION_BASE + DMA_REGION_SIZE)

/* Alignment for DMA buffers (4KB page-aligned) */
#define DMA_ALIGN         4096ULL

static uint64_t dma_bump_ptr = DMA_REGION_BASE;

/* Align value up to the given power-of-two alignment */
static inline uint64_t align_up(uint64_t val, uint64_t align)
{
    return (val + align - 1) & ~(align - 1);
}

void *hal_dma_alloc(uint64_t size, uint64_t *phys)
{
    if (size == 0) {
        if (phys) *phys = 0;
        return (void *)0;
    }

    /* Align the bump pointer */
    uint64_t aligned = align_up(dma_bump_ptr, DMA_ALIGN);

    /* Align the requested size up to page boundary */
    uint64_t alloc_size = align_up(size, DMA_ALIGN);

    if (aligned + alloc_size > DMA_REGION_END) {
        /* Out of DMA memory */
        if (phys) *phys = 0;
        return (void *)0;
    }

    dma_bump_ptr = aligned + alloc_size;

    /* Identity-mapped: virtual == physical */
    if (phys) *phys = aligned;
    return (void *)(uintptr_t)aligned;
}

void hal_dma_free(void *virt, uint64_t size)
{
    /* Bump allocator does not support free.
     * In a more sophisticated implementation, we would maintain a free list.
     * For the exokernel use case, DMA buffers are typically allocated once
     * at driver init and never freed. */
    (void)virt;
    (void)size;
}
