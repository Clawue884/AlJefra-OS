/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- AArch64 MMU Implementation
 * Implements hal/mmu.h using ARMv8-A 4-level page tables.
 *
 * Configuration:
 *   - 4KB granule
 *   - 48-bit VA (T0SZ = 16)
 *   - 4 levels: L0 (512GB) -> L1 (1GB) -> L2 (2MB) -> L3 (4KB)
 *   - Identity mapping for first 4GB (sufficient for QEMU virt)
 *
 * MAIR_EL1 attribute indices:
 *   0: Device-nGnRnE (0x00) - strongly ordered device memory
 *   1: Normal, Write-back (0xFF) - Inner/Outer WB RW-Allocate
 *   2: Normal, Write-combining (0x44) - Inner/Outer Non-cacheable
 *
 * TCR_EL1:
 *   T0SZ  = 16 (48-bit VA)
 *   TG0   = 0b00 (4KB granule)
 *   SH0   = 0b11 (Inner Shareable)
 *   ORGN0 = 0b01 (WB RA WA Cacheable)
 *   IRGN0 = 0b01 (WB RA WA Cacheable)
 */

#include "../../hal/hal.h"

/* ------------------------------------------------------------------ */
/* Page table constants                                                */
/* ------------------------------------------------------------------ */

#define PAGE_SIZE       4096ULL
#define PAGE_SHIFT      12
#define ENTRIES_PER_TABLE 512

/* Descriptor bits */
#define PTE_VALID       (1ULL << 0)
#define PTE_TABLE       (1ULL << 1)   /* L0/L1/L2: points to next level table */
#define PTE_BLOCK       (0ULL << 1)   /* L1/L2: block descriptor (1GB/2MB) */
#define PTE_PAGE        (1ULL << 1)   /* L3: page descriptor (4KB) */
#define PTE_AF          (1ULL << 10)  /* Access Flag */
#define PTE_SH_ISH      (3ULL << 8)   /* Inner Shareable */
#define PTE_AP_RW_EL1   (0ULL << 6)   /* AP[2:1] = 00: EL1 R/W */
#define PTE_AP_RO_EL1   (2ULL << 6)   /* AP[2:1] = 10: EL1 R/O */
#define PTE_AP_RW_ALL   (1ULL << 6)   /* AP[2:1] = 01: EL0+EL1 R/W */
#define PTE_UXN         (1ULL << 54)  /* Unprivileged Execute-Never */
#define PTE_PXN         (1ULL << 53)  /* Privileged Execute-Never */
#define PTE_nG          (1ULL << 11)  /* non-Global */

/* MAIR attribute index encodings (in bits [4:2] of descriptor) */
#define PTE_ATTR_DEVICE  (0ULL << 2)  /* Index 0: Device-nGnRnE */
#define PTE_ATTR_NORMAL  (1ULL << 2)  /* Index 1: Normal WB */
#define PTE_ATTR_WC      (2ULL << 2)  /* Index 2: Write-Combining */

/* ------------------------------------------------------------------ */
/* MAIR / TCR / TTBR values                                            */
/* ------------------------------------------------------------------ */

/* MAIR_EL1: attr[0]=Device, attr[1]=Normal WB, attr[2]=WC */
#define MAIR_VALUE  ((0x00ULL << 0) | (0xFFULL << 8) | (0x44ULL << 16))

/* TCR_EL1: T0SZ=16, TG0=4KB, SH0=ISH, ORGN0=WB, IRGN0=WB */
#define TCR_T0SZ     (16ULL)
#define TCR_TG0_4KB  (0ULL << 14)
#define TCR_SH0_ISH  (3ULL << 12)
#define TCR_ORGN0_WB (1ULL << 10)
#define TCR_IRGN0_WB (1ULL << 8)
#define TCR_IPS_48   (5ULL << 32)   /* 48-bit physical address (256TB) */
#define TCR_VALUE    (TCR_T0SZ | TCR_TG0_4KB | TCR_SH0_ISH | \
                      TCR_ORGN0_WB | TCR_IRGN0_WB | TCR_IPS_48)

/* ------------------------------------------------------------------ */
/* Page table memory pool                                              */
/* ------------------------------------------------------------------ */

/* Statically allocate page tables (aligned to 4KB).
 * We need:
 *   1 L0 table (covers 512GB * 512 = 256TB)
 *   1 L1 table (covers 512 * 1GB = 512GB) -- enough for first 512GB
 *   4 L2 tables (covers 4 * 512 * 2MB = 4GB) -- identity map first 4GB
 * Total: 6 pages = 24KB */
#define PT_POOL_PAGES   64
static uint64_t pt_pool[PT_POOL_PAGES][ENTRIES_PER_TABLE]
    __attribute__((aligned(PAGE_SIZE)));
static uint32_t pt_pool_next = 0;

/* L0 table is always pool[0] */
#define L0_TABLE  pt_pool[0]

/* ------------------------------------------------------------------ */
/* Simple page frame allocator for RAM                                 */
/* ------------------------------------------------------------------ */

#define PHYS_PAGE_BITMAP_SIZE  (1024 * 1024 / 8)  /* Track up to 1M pages = 4GB */
static uint8_t page_bitmap[PHYS_PAGE_BITMAP_SIZE];
static uint64_t total_ram_bytes = 0;
static uint64_t free_ram_bytes  = 0;

static inline void bitmap_set(uint64_t pfn)
{
    page_bitmap[pfn / 8] |= (1 << (pfn % 8));
}

static inline void bitmap_clear(uint64_t pfn)
{
    page_bitmap[pfn / 8] &= ~(1 << (pfn % 8));
}

static inline int bitmap_test(uint64_t pfn)
{
    return (page_bitmap[pfn / 8] >> (pfn % 8)) & 1;
}

/* ------------------------------------------------------------------ */
/* Allocate a zeroed page table page from the pool                     */
/* ------------------------------------------------------------------ */

static uint64_t *alloc_pt_page(void)
{
    if (pt_pool_next >= PT_POOL_PAGES)
        return (uint64_t *)0;

    uint64_t *page = pt_pool[pt_pool_next++];
    for (int i = 0; i < ENTRIES_PER_TABLE; i++)
        page[i] = 0;
    return page;
}

/* ------------------------------------------------------------------ */
/* System register helpers                                             */
/* ------------------------------------------------------------------ */

static inline void write_mair_el1(uint64_t v)
{
    __asm__ volatile("msr MAIR_EL1, %0" : : "r"(v));
    __asm__ volatile("isb");
}

static inline void write_tcr_el1(uint64_t v)
{
    __asm__ volatile("msr TCR_EL1, %0" : : "r"(v));
    __asm__ volatile("isb");
}

static inline void write_ttbr0_el1(uint64_t v)
{
    __asm__ volatile("msr TTBR0_EL1, %0" : : "r"(v));
    __asm__ volatile("isb");
}

static inline uint64_t read_sctlr_el1(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, SCTLR_EL1" : "=r"(v));
    return v;
}

static inline void write_sctlr_el1(uint64_t v)
{
    __asm__ volatile("msr SCTLR_EL1, %0" : : "r"(v));
    __asm__ volatile("isb");
}

/* ------------------------------------------------------------------ */
/* Build identity map for a physical range                             */
/* ------------------------------------------------------------------ */

static void map_2mb_block(uint64_t phys, uint64_t attr_bits)
{
    /* L0 index: bits [47:39] */
    uint64_t l0_idx = (phys >> 39) & 0x1FF;
    /* L1 index: bits [38:30] */
    uint64_t l1_idx = (phys >> 30) & 0x1FF;
    /* L2 index: bits [29:21] */
    uint64_t l2_idx = (phys >> 21) & 0x1FF;

    /* Ensure L0 -> L1 table exists */
    if (!(L0_TABLE[l0_idx] & PTE_VALID)) {
        uint64_t *l1 = alloc_pt_page();
        if (!l1) return;
        L0_TABLE[l0_idx] = (uint64_t)l1 | PTE_VALID | PTE_TABLE;
    }
    uint64_t *l1_table = (uint64_t *)(L0_TABLE[l0_idx] & ~0xFFFULL);

    /* Ensure L1 -> L2 table exists */
    if (!(l1_table[l1_idx] & PTE_VALID)) {
        uint64_t *l2 = alloc_pt_page();
        if (!l2) return;
        l1_table[l1_idx] = (uint64_t)l2 | PTE_VALID | PTE_TABLE;
    }
    uint64_t *l2_table = (uint64_t *)(l1_table[l1_idx] & ~0xFFFULL);

    /* L2 block descriptor: maps 2MB */
    l2_table[l2_idx] = (phys & ~((1ULL << 21) - 1)) | PTE_VALID | PTE_BLOCK |
                        PTE_AF | PTE_SH_ISH | attr_bits;
}

/* ------------------------------------------------------------------ */
/* HAL Interface Implementation                                        */
/* ------------------------------------------------------------------ */

hal_status_t hal_mmu_init(void)
{
    /* Zero L0 table */
    for (int i = 0; i < ENTRIES_PER_TABLE; i++)
        L0_TABLE[i] = 0;
    pt_pool_next = 1;  /* Pool[0] is L0 */

    /* QEMU virt memory map:
     *   0x00000000 - 0x3FFFFFFF: I/O devices (flash, GIC, UART, virtio, PCIe window)
     *   0x40000000 - RAM_END:    RAM (size determined by -m flag, default 256MB)
     *   0x4010000000:            PCIe ECAM (256MB)
     *
     * We identity-map everything the kernel and drivers need access to. */

    /* Identity map device I/O region: 0x00000000 - 0x3FFFFFFF (1GB) */
    for (uint64_t addr = 0; addr < 0x40000000ULL; addr += (2 * 1024 * 1024)) {
        map_2mb_block(addr, PTE_ATTR_DEVICE | PTE_AP_RW_EL1 | PTE_PXN | PTE_UXN);
    }

    /* Identity map RAM: 0x40000000 - 0x4FFFFFFF (256MB, sufficient for default config)
     * For larger -m values, the DTB should be parsed for actual memory size. */
    for (uint64_t addr = 0x40000000ULL; addr < 0x50000000ULL; addr += (2 * 1024 * 1024)) {
        map_2mb_block(addr, PTE_ATTR_NORMAL | PTE_AP_RW_EL1);
    }

    /* Identity map PCIe ECAM region (0x4010000000, 256MB) as device */
    for (uint64_t addr = 0x4010000000ULL; addr < 0x4020000000ULL; addr += (2 * 1024 * 1024)) {
        map_2mb_block(addr, PTE_ATTR_DEVICE | PTE_AP_RW_EL1 | PTE_PXN | PTE_UXN);
    }

    /* Identity map PCIe MMIO window at 0x10000000-0x3EFEFFFF (already covered by I/O above)
     * and high MMIO at 0x8000000000 if needed in the future. */

    /* Set MAIR_EL1 */
    write_mair_el1(MAIR_VALUE);

    /* Set TCR_EL1 */
    write_tcr_el1(TCR_VALUE);

    /* Set TTBR0_EL1 to our L0 table */
    write_ttbr0_el1((uint64_t)&L0_TABLE);

    /* Invalidate all TLBs */
    __asm__ volatile("tlbi vmalle1is" ::: "memory");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");

    /* Enable MMU: set SCTLR_EL1.M (bit 0), C (bit 2), I (bit 12) */
    uint64_t sctlr = read_sctlr_el1();
    sctlr |= (1ULL << 0);   /* M: MMU enable */
    sctlr |= (1ULL << 2);   /* C: Data cache enable */
    sctlr |= (1ULL << 12);  /* I: Instruction cache enable */
    write_sctlr_el1(sctlr);

    /* Initialize page allocator.
     * QEMU virt default: 256MB at 0x40000000.
     * TODO: Parse DTB memory node for actual RAM size. */
    total_ram_bytes = 256ULL * 1024 * 1024;  /* 256MB RAM default */
    free_ram_bytes  = total_ram_bytes;

    /* Zero bitmap */
    for (uint32_t i = 0; i < PHYS_PAGE_BITMAP_SIZE; i++)
        page_bitmap[i] = 0;

    /* Reserve first 32MB of RAM for kernel image + BSS + stacks + page tables.
     * The kernel is loaded at 0x40000000 and BSS+stacks extend ~18MB beyond.
     * We reserve 32MB (8192 pages) to be safe. */
    uint64_t reserved_pages = (32 * 1024 * 1024) / PAGE_SIZE;
    for (uint64_t i = 0; i < reserved_pages; i++) {
        bitmap_set(i);
        free_ram_bytes -= PAGE_SIZE;
    }

    return HAL_OK;
}

hal_status_t hal_mmu_map(uint64_t virt, uint64_t phys, uint64_t size,
                          uint32_t perms, hal_mem_type_t type)
{
    uint64_t attr = PTE_AP_RW_EL1;

    switch (type) {
    case HAL_MEM_DEVICE:
        attr |= PTE_ATTR_DEVICE | PTE_PXN | PTE_UXN;
        break;
    case HAL_MEM_WC:
        attr |= PTE_ATTR_WC;
        break;
    case HAL_MEM_NORMAL:
    default:
        attr |= PTE_ATTR_NORMAL;
        break;
    }

    if (perms & HAL_PAGE_USER)
        attr = (attr & ~PTE_AP_RW_EL1) | PTE_AP_RW_ALL;
    if (!(perms & HAL_PAGE_WRITE))
        attr = (attr & ~PTE_AP_RW_EL1) | PTE_AP_RO_EL1;
    if (!(perms & HAL_PAGE_EXEC))
        attr |= PTE_PXN | PTE_UXN;

    /* Map using 2MB blocks where aligned, otherwise fall back to 4KB */
    uint64_t end = virt + size;
    while (virt < end) {
        if ((virt & ((2 * 1024 * 1024) - 1)) == 0 &&
            (phys & ((2 * 1024 * 1024) - 1)) == 0 &&
            (end - virt) >= (2 * 1024 * 1024)) {
            /* 2MB block */
            map_2mb_block(phys, attr);
            virt += 2 * 1024 * 1024;
            phys += 2 * 1024 * 1024;
        } else {
            /* TODO: Implement 4KB page mapping for sub-2MB regions.
             * This requires adding L3 page table support. */
            /* For now, round up to 2MB block */
            map_2mb_block(phys & ~((2ULL * 1024 * 1024) - 1), attr);
            virt += PAGE_SIZE;
            phys += PAGE_SIZE;
        }
    }

    /* Invalidate TLB for mapped range */
    hal_mmu_invalidate(virt - size, size);

    return HAL_OK;
}

hal_status_t hal_mmu_unmap(uint64_t virt, uint64_t size)
{
    /* TODO: Walk page tables and clear entries.
     * For now, invalidate TLB for the range. */
    uint64_t l0_idx = (virt >> 39) & 0x1FF;
    uint64_t l1_idx = (virt >> 30) & 0x1FF;

    if (L0_TABLE[l0_idx] & PTE_VALID) {
        uint64_t *l1 = (uint64_t *)(L0_TABLE[l0_idx] & ~0xFFFULL);
        if (l1[l1_idx] & PTE_VALID) {
            uint64_t *l2 = (uint64_t *)(l1[l1_idx] & ~0xFFFULL);
            /* Clear 2MB blocks covering the range */
            uint64_t end = virt + size;
            for (uint64_t a = virt; a < end; a += (2 * 1024 * 1024)) {
                uint64_t idx = (a >> 21) & 0x1FF;
                l2[idx] = 0;
            }
        }
    }

    hal_mmu_invalidate(virt, size);
    return HAL_OK;
}

void hal_mmu_invalidate(uint64_t virt, uint64_t size)
{
    uint64_t end = virt + size;
    for (uint64_t addr = virt; addr < end; addr += PAGE_SIZE) {
        /* TLBI VAE1IS: invalidate by VA, EL1, Inner Shareable */
        uint64_t val = (addr >> 12) & 0xFFFFFFFFFULL;
        __asm__ volatile("tlbi vae1is, %0" : : "r"(val));
    }
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
}

uint32_t hal_mmu_get_memory_map(hal_mem_region_t *regions, uint32_t max)
{
    /* TODO: Parse DTB memory node for actual memory map.
     * For now, return hardcoded QEMU virt default. */
    uint32_t count = 0;

    if (count < max) {
        regions[count].base = 0x40000000ULL;
        regions[count].size = 0x10000000ULL;  /* 256MB RAM at 1GB offset */
        regions[count].type = 1;  /* usable */
        count++;
    }

    if (count < max) {
        regions[count].base = 0x08000000ULL;
        regions[count].size = 0x00020000ULL;  /* GIC region */
        regions[count].type = 2;  /* reserved */
        count++;
    }

    if (count < max) {
        regions[count].base = 0x09000000ULL;
        regions[count].size = 0x00001000ULL;  /* UART */
        regions[count].type = 2;  /* reserved */
        count++;
    }

    return count;
}

uint64_t hal_mmu_total_ram(void)
{
    return total_ram_bytes;
}

uint64_t hal_mmu_free_ram(void)
{
    return free_ram_bytes;
}

uint64_t hal_mmu_alloc_pages(uint32_t count)
{
    uint64_t max_pfn = total_ram_bytes / PAGE_SIZE;
    uint32_t found = 0;
    uint64_t start_pfn = 0;

    /* Linear scan for `count` contiguous free pages */
    for (uint64_t pfn = 0; pfn < max_pfn; pfn++) {
        if (!bitmap_test(pfn)) {
            if (found == 0)
                start_pfn = pfn;
            found++;
            if (found == count) {
                /* Mark pages as allocated */
                for (uint32_t i = 0; i < count; i++) {
                    bitmap_set(start_pfn + i);
                }
                free_ram_bytes -= (uint64_t)count * PAGE_SIZE;
                return start_pfn * PAGE_SIZE;
            }
        } else {
            found = 0;
        }
    }

    return 0;  /* Allocation failed */
}

void hal_mmu_free_pages(uint64_t phys, uint32_t count)
{
    uint64_t pfn = phys / PAGE_SIZE;
    for (uint32_t i = 0; i < count; i++) {
        bitmap_clear(pfn + i);
    }
    free_ram_bytes += (uint64_t)count * PAGE_SIZE;
}
