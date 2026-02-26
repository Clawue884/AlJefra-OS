/* SPDX-License-Identifier: MIT */
/* AlJefra OS — HAL Memory Management Interface
 * Architecture-independent page table and memory mapping operations.
 *
 * x86-64:  4-level page tables (PML4 → PDPT → PD → PT)
 * AArch64: 4-level page tables (4KB granule, 48-bit VA)
 * RISC-V:  Sv48 page tables
 */

#ifndef ALJEFRA_HAL_MMU_H
#define ALJEFRA_HAL_MMU_H

#include <stdint.h>

/* Page sizes */
#define HAL_PAGE_4K    (4096ULL)
#define HAL_PAGE_2M    (2 * 1024 * 1024ULL)
#define HAL_PAGE_1G    (1024ULL * 1024 * 1024)

/* Memory type / caching attributes */
typedef enum {
    HAL_MEM_NORMAL    = 0,  /* Write-back cacheable (RAM) */
    HAL_MEM_DEVICE    = 1,  /* Device memory / uncacheable (MMIO) */
    HAL_MEM_WC        = 2,  /* Write-combining (framebuffer) */
} hal_mem_type_t;

/* Page permission flags */
#define HAL_PAGE_READ     (1u << 0)
#define HAL_PAGE_WRITE    (1u << 1)
#define HAL_PAGE_EXEC     (1u << 2)
#define HAL_PAGE_USER     (1u << 3)

/* Memory region descriptor (for reporting available RAM) */
typedef struct {
    uint64_t base;      /* Physical base address */
    uint64_t size;      /* Size in bytes */
    uint32_t type;      /* 1 = usable, 2 = reserved, 3 = ACPI reclaimable */
} hal_mem_region_t;

/* Initialize MMU and create identity mapping for usable RAM + MMIO.
 * Called once during boot. */
hal_status_t hal_mmu_init(void);

/* Map a physical page range into virtual address space.
 * On bare-metal exokernel, this typically adds an identity mapping. */
hal_status_t hal_mmu_map(uint64_t virt, uint64_t phys, uint64_t size,
                          uint32_t perms, hal_mem_type_t type);

/* Unmap a virtual address range */
hal_status_t hal_mmu_unmap(uint64_t virt, uint64_t size);

/* Invalidate TLB entries for a virtual address range */
void hal_mmu_invalidate(uint64_t virt, uint64_t size);

/* Query the memory map (from firmware/bootloader).
 * Returns the number of regions written to `regions`. */
uint32_t hal_mmu_get_memory_map(hal_mem_region_t *regions, uint32_t max);

/* Get total usable RAM in bytes */
uint64_t hal_mmu_total_ram(void);

/* Get free (unallocated) RAM in bytes */
uint64_t hal_mmu_free_ram(void);

/* Simple physical page allocator (returns physical address, 0 on failure) */
uint64_t hal_mmu_alloc_pages(uint32_t count);

/* Free physical pages */
void hal_mmu_free_pages(uint64_t phys, uint32_t count);

#endif /* ALJEFRA_HAL_MMU_H */
