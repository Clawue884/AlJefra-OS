/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- RISC-V 64-bit MMU Implementation (Sv39)
 * Implements hal/mmu.h using RISC-V Sv39 page tables.
 *
 * Sv39 (3-level page tables, 39-bit virtual address):
 *   Level 0 (root): PPN[2] -- 512 entries, each covers 1GB (gigapage)
 *   Level 1:        PPN[1] -- 512 entries, each covers 2MB (megapage)
 *   Level 2 (leaf): PPN[0] -- 512 entries, each covers 4KB (page)
 *
 * Page Table Entry (PTE) format:
 *   [63:54] reserved
 *   [53:10] PPN (Physical Page Number)
 *   [9:8]   RSW (reserved for SW)
 *   [7]     D (Dirty)
 *   [6]     A (Accessed)
 *   [5]     G (Global)
 *   [4]     U (User)
 *   [3]     X (Execute)
 *   [2]     W (Write)
 *   [1]     R (Read)
 *   [0]     V (Valid)
 *
 * satp CSR format (Sv39):
 *   [63:60] MODE = 8 (Sv39)
 *   [59:44] ASID
 *   [43:0]  PPN of root page table
 *
 * QEMU virt memory map:
 *   0x00000000 - 0x02000000  : low I/O (CLINT at 0x02000000)
 *   0x02000000 - 0x0C000000  : CLINT, test, ...
 *   0x0C000000 - 0x10000000  : PLIC
 *   0x10000000 - 0x10009000  : UART + VirtIO MMIO
 *   0x30000000 - 0x40000000  : PCIe ECAM
 *   0x40000000 - 0x80000000  : PCIe MMIO
 *   0x80000000 - end         : RAM (kernel + data)
 */

#include "../../hal/hal.h"

/* ------------------------------------------------------------------ */
/* Page table constants                                                */
/* ------------------------------------------------------------------ */

#define PAGE_SIZE       4096ULL
#define PAGE_SHIFT      12
#define PTE_PER_TABLE   512

/* PTE bits */
#define PTE_V   (1ULL << 0)    /* Valid */
#define PTE_R   (1ULL << 1)    /* Read */
#define PTE_W   (1ULL << 2)    /* Write */
#define PTE_X   (1ULL << 3)    /* Execute */
#define PTE_U   (1ULL << 4)    /* User */
#define PTE_G   (1ULL << 5)    /* Global */
#define PTE_A   (1ULL << 6)    /* Accessed */
#define PTE_D   (1ULL << 7)    /* Dirty */

/* SATP modes */
#define SATP_MODE_SV39  (8ULL << 60)

/* PTE helpers */
#define PTE_PPN_SHIFT   10
#define PA_TO_PPN(pa)   (((pa) >> PAGE_SHIFT) << PTE_PPN_SHIFT)
#define PPN_TO_PA(pte)  ((((pte) >> PTE_PPN_SHIFT) & 0xFFFFFFFFFFFULL) << PAGE_SHIFT)

/* ------------------------------------------------------------------ */
/* Page table memory pool                                              */
/* ------------------------------------------------------------------ */

#define PT_POOL_PAGES   64
static uint64_t pt_pool[PT_POOL_PAGES][PTE_PER_TABLE]
    __attribute__((aligned(PAGE_SIZE)));
static uint32_t pt_pool_next = 0;

#define ROOT_TABLE  pt_pool[0]

/* ------------------------------------------------------------------ */
/* Page frame allocator                                                */
/* ------------------------------------------------------------------ */

#define PHYS_PAGE_BITMAP_SIZE  (1024 * 1024 / 8)
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
/* Allocate a zeroed page table page                                   */
/* ------------------------------------------------------------------ */

static uint64_t *alloc_pt_page(void)
{
    if (pt_pool_next >= PT_POOL_PAGES)
        return (uint64_t *)0;

    uint64_t *page = pt_pool[pt_pool_next++];
    for (int i = 0; i < PTE_PER_TABLE; i++)
        page[i] = 0;
    return page;
}

/* ------------------------------------------------------------------ */
/* CSR helpers                                                         */
/* ------------------------------------------------------------------ */

static inline void write_satp(uint64_t v)
{
    __asm__ volatile("csrw satp, %0" : : "r"(v));
    /* Flush TLB after changing satp */
    __asm__ volatile("sfence.vma zero, zero");
}

static inline uint64_t read_satp(void)
{
    uint64_t v;
    __asm__ volatile("csrr %0, satp" : "=r"(v));
    return v;
}

/* ------------------------------------------------------------------ */
/* Map a 1GB gigapage (Sv39 level-0 leaf)                              */
/* ------------------------------------------------------------------ */

static void map_gigapage(uint64_t va, uint64_t pa, uint64_t pte_flags)
{
    /* Sv39 VA breakdown:
     * [38:30] = VPN[2] (level 0 index, root) -- gigapage leaf
     * [29:21] = VPN[1] (level 1 index) -- megapage leaf
     * [20:12] = VPN[0] (level 2 index) -- 4KB page leaf
     */
    uint64_t vpn2 = (va >> 30) & 0x1FF;

    /* Gigapage: set R/W/X bits to make it a leaf PTE */
    ROOT_TABLE[vpn2] = PA_TO_PPN(pa & ~((1ULL << 30) - 1)) |
                       PTE_V | PTE_A | PTE_D | PTE_G | pte_flags;
}

/* ------------------------------------------------------------------ */
/* Map a 2MB megapage (Sv39 level-1 leaf)                              */
/* ------------------------------------------------------------------ */

static void map_megapage(uint64_t va, uint64_t pa, uint64_t pte_flags)
{
    /* Sv39 VA breakdown:
     * [38:30] = VPN[2] (level 0 index, root)
     * [29:21] = VPN[1] (level 1 index) -- megapage leaf
     */
    uint64_t vpn2 = (va >> 30) & 0x1FF;
    uint64_t vpn1 = (va >> 21) & 0x1FF;

    /* Level 0 -> Level 1 */
    if (!(ROOT_TABLE[vpn2] & PTE_V)) {
        uint64_t *l1 = alloc_pt_page();
        if (!l1) return;
        ROOT_TABLE[vpn2] = PA_TO_PPN((uint64_t)l1) | PTE_V;
    } else if (ROOT_TABLE[vpn2] & (PTE_R | PTE_W | PTE_X)) {
        /* Already a gigapage leaf -- cannot create sub-table.
         * Skip this mapping. */
        return;
    }
    uint64_t *l1 = (uint64_t *)PPN_TO_PA(ROOT_TABLE[vpn2]);

    /* Level 1 leaf (megapage): must have at least one of R/W/X set */
    l1[vpn1] = PA_TO_PPN(pa & ~((2ULL * 1024 * 1024) - 1)) |
               PTE_V | PTE_A | PTE_D | PTE_G | pte_flags;
}

/* ------------------------------------------------------------------ */
/* HAL Interface Implementation                                        */
/* ------------------------------------------------------------------ */

hal_status_t hal_mmu_init(void)
{
    /* Zero root table */
    for (int i = 0; i < PTE_PER_TABLE; i++)
        ROOT_TABLE[i] = 0;
    pt_pool_next = 1;

    /* --- Identity map the QEMU virt address space using gigapages --- */

    /* 0x00000000 - 0x3FFFFFFF (first 1GB): I/O devices
     * Contains CLINT, PLIC, UART, VirtIO MMIO.
     * Map as RW (no execute, device memory). */
    map_gigapage(0x00000000ULL, 0x00000000ULL, PTE_R | PTE_W);

    /* 0x40000000 - 0x7FFFFFFF (second 1GB): PCIe MMIO + more I/O
     * Map as RW (device). */
    map_gigapage(0x40000000ULL, 0x40000000ULL, PTE_R | PTE_W);

    /* 0x80000000 - 0xBFFFFFFF (third 1GB): RAM
     * Contains OpenSBI (0x80000000) and kernel (0x80200000+).
     * Map as RWX (normal memory, executable). */
    map_gigapage(0x80000000ULL, 0x80000000ULL, PTE_R | PTE_W | PTE_X);

    /* 0xC0000000 - 0xFFFFFFFF (fourth 1GB): more RAM if -m > 1GB
     * Map as RWX. */
    map_gigapage(0xC0000000ULL, 0xC0000000ULL, PTE_R | PTE_W | PTE_X);

    /* Enable Sv39: write satp with mode=8 and PPN of root table */
    uint64_t root_ppn = ((uint64_t)&ROOT_TABLE) >> PAGE_SHIFT;
    write_satp(SATP_MODE_SV39 | root_ppn);

    /* Verify satp was accepted (if mode field stuck, MMU is enabled) */
    uint64_t satp_check = read_satp();
    if ((satp_check >> 60) != 8) {
        /* Sv39 not supported or rejected -- continue in bare mode */
        hal_console_puts("[HAL] WARNING: Sv39 not accepted, running without MMU\n");
    }

    /* Initialize page allocator */
    total_ram_bytes = 256ULL * 1024 * 1024;  /* Default 256MB (QEMU -m 256) */
    free_ram_bytes  = total_ram_bytes;

    /* Zero bitmap */
    for (uint32_t i = 0; i < PHYS_PAGE_BITMAP_SIZE; i++)
        page_bitmap[i] = 0;

    /* Reserve first 128MB (from 0x80000000) for kernel + page tables + DMA.
     * Page frame numbers relative to RAM base. */
    uint64_t ram_base_pfn = 0x80000000ULL / PAGE_SIZE;
    uint64_t reserved_pages = (128 * 1024 * 1024) / PAGE_SIZE;
    for (uint64_t i = 0; i < reserved_pages && (ram_base_pfn + i) < (PHYS_PAGE_BITMAP_SIZE * 8); i++) {
        bitmap_set(ram_base_pfn + i);
        free_ram_bytes -= PAGE_SIZE;
    }

    return HAL_OK;
}

hal_status_t hal_mmu_map(uint64_t virt, uint64_t phys, uint64_t size,
                          uint32_t perms, hal_mem_type_t type)
{
    (void)type;  /* RISC-V doesn't have MAIR-like memory type attributes */

    uint64_t flags = 0;
    if (perms & HAL_PAGE_READ)  flags |= PTE_R;
    if (perms & HAL_PAGE_WRITE) flags |= PTE_W;
    if (perms & HAL_PAGE_EXEC)  flags |= PTE_X;
    if (perms & HAL_PAGE_USER)  flags |= PTE_U;

    /* If no permissions specified, default to RW */
    if (!(flags & (PTE_R | PTE_W | PTE_X)))
        flags = PTE_R | PTE_W;

    uint64_t end = virt + size;
    while (virt < end) {
        if ((virt & ((2ULL * 1024 * 1024) - 1)) == 0 &&
            (phys & ((2ULL * 1024 * 1024) - 1)) == 0 &&
            (end - virt) >= (2ULL * 1024 * 1024)) {
            map_megapage(virt, phys, flags);
            virt += 2 * 1024 * 1024;
            phys += 2 * 1024 * 1024;
        } else {
            /* TODO: Implement 4KB page mapping for sub-2MB regions */
            map_megapage(virt & ~((2ULL * 1024 * 1024) - 1),
                         phys & ~((2ULL * 1024 * 1024) - 1), flags);
            virt += PAGE_SIZE;
            phys += PAGE_SIZE;
        }
    }

    hal_mmu_invalidate(virt - size, size);
    return HAL_OK;
}

hal_status_t hal_mmu_unmap(uint64_t virt, uint64_t size)
{
    uint64_t vpn2 = (virt >> 30) & 0x1FF;

    if (ROOT_TABLE[vpn2] & PTE_V) {
        /* Check if gigapage */
        if (ROOT_TABLE[vpn2] & (PTE_R | PTE_W | PTE_X)) {
            /* Gigapage -- just clear it */
            ROOT_TABLE[vpn2] = 0;
        } else {
            uint64_t *l1 = (uint64_t *)PPN_TO_PA(ROOT_TABLE[vpn2]);
            uint64_t end = virt + size;
            for (uint64_t a = virt; a < end; a += (2 * 1024 * 1024)) {
                uint64_t idx = (a >> 21) & 0x1FF;
                l1[idx] = 0;
            }
        }
    }

    hal_mmu_invalidate(virt, size);
    return HAL_OK;
}

void hal_mmu_invalidate(uint64_t virt, uint64_t size)
{
    /* sfence.vma: flush TLB entries.
     * sfence.vma rs1, rs2: rs1=vaddr (0=all), rs2=ASID (0=all) */
    uint64_t end = virt + size;
    for (uint64_t addr = virt; addr < end; addr += PAGE_SIZE) {
        __asm__ volatile("sfence.vma %0, zero" : : "r"(addr));
    }
}

uint32_t hal_mmu_get_memory_map(hal_mem_region_t *regions, uint32_t max)
{
    /* TODO: Parse DTB memory node.
     * QEMU virt default: RAM at 0x80000000, size from -m flag. */
    uint32_t count = 0;

    if (count < max) {
        regions[count].base = 0x80000000ULL;
        regions[count].size = 0x10000000ULL;  /* 256MB (matches -m 256) */
        regions[count].type = 1;  /* usable */
        count++;
    }

    if (count < max) {
        regions[count].base = 0x0C000000ULL;
        regions[count].size = 0x04000000ULL;  /* PLIC 64MB */
        regions[count].type = 2;  /* reserved */
        count++;
    }

    if (count < max) {
        regions[count].base = 0x02000000ULL;
        regions[count].size = 0x00010000ULL;  /* CLINT */
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
    uint64_t ram_base_pfn = 0x80000000ULL / PAGE_SIZE;
    uint64_t max_pfn = ram_base_pfn + total_ram_bytes / PAGE_SIZE;
    uint32_t found = 0;
    uint64_t start_pfn = 0;

    for (uint64_t pfn = ram_base_pfn; pfn < max_pfn; pfn++) {
        if (!bitmap_test(pfn)) {
            if (found == 0) start_pfn = pfn;
            found++;
            if (found == count) {
                for (uint32_t i = 0; i < count; i++)
                    bitmap_set(start_pfn + i);
                free_ram_bytes -= (uint64_t)count * PAGE_SIZE;
                return start_pfn * PAGE_SIZE;
            }
        } else {
            found = 0;
        }
    }

    return 0;
}

void hal_mmu_free_pages(uint64_t phys, uint32_t count)
{
    uint64_t pfn = phys / PAGE_SIZE;
    for (uint32_t i = 0; i < count; i++)
        bitmap_clear(pfn + i);
    free_ram_bytes += (uint64_t)count * PAGE_SIZE;
}
