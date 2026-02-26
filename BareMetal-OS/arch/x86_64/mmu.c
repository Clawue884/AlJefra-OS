/* SPDX-License-Identifier: MIT */
/* AlJefra OS — x86-64 MMU HAL Implementation (Standalone)
 *
 * Page table manipulation for 4-level x86-64 paging (PML4/PDPT/PD/PT).
 * boot.S sets up identity mapping for the first 4GB using 2MB pages.
 *
 * Memory detection uses the Multiboot1 info structure passed by the
 * bootloader (QEMU, GRUB).  Falls back to 256 MB if no info available.
 */

#include "../../hal/hal.h"

/* -------------------------------------------------------------------------- */
/* Multiboot1 info parsing                                                    */
/* -------------------------------------------------------------------------- */

/* Multiboot1 info flags */
#define MB1_FLAG_MEM      (1u << 0)  /* mem_lower/mem_upper valid */
#define MB1_FLAG_MMAP     (1u << 6)  /* mmap_length/mmap_addr valid */

/* Multiboot1 bootloader magic */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* Memory map entry types */
#define MMAP_AVAILABLE 1
#define MMAP_RESERVED  2

/* boot.S saves the multiboot info pointer and magic here */
extern uint64_t boot_mb_info_ptr;
extern uint32_t boot_mb_magic;

/* Cached total RAM */
static uint64_t total_ram_bytes = 0;

/* Memory map cache */
#define MAX_MEM_REGIONS 32
static hal_mem_region_t mem_regions[MAX_MEM_REGIONS];
static uint32_t mem_region_count = 0;

static void parse_multiboot1_mmap(void)
{
    uint64_t mb_addr = boot_mb_info_ptr;

    if (mb_addr == 0 || boot_mb_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        /* No valid multiboot info — assume 256 MB */
        total_ram_bytes = 256ULL * 1024 * 1024;
        mem_regions[0].base = 0;
        mem_regions[0].size = total_ram_bytes;
        mem_regions[0].type = 1;
        mem_region_count = 1;
        return;
    }

    /* Multiboot1 info structure layout:
     *   offset  0: uint32_t flags
     *   offset  4: uint32_t mem_lower  (KB below 1MB)
     *   offset  8: uint32_t mem_upper  (KB above 1MB)
     *   offset 44: uint32_t mmap_length
     *   offset 48: uint32_t mmap_addr  */
    volatile uint32_t *info = (volatile uint32_t *)(uintptr_t)mb_addr;
    uint32_t flags = info[0];

    total_ram_bytes = 0;
    mem_region_count = 0;

    if (flags & MB1_FLAG_MMAP) {
        /* Parse the full memory map */
        uint32_t mmap_length = info[44 / 4];
        uint32_t mmap_addr   = info[48 / 4];
        uint8_t *ptr = (uint8_t *)(uintptr_t)mmap_addr;
        uint8_t *end = ptr + mmap_length;

        while (ptr < end && mem_region_count < MAX_MEM_REGIONS) {
            /* Each entry:
             *   uint32_t size     (size of rest of entry, not including this field)
             *   uint64_t addr
             *   uint64_t len
             *   uint32_t type     (1 = available) */
            uint32_t entry_size = *(uint32_t *)ptr;
            uint64_t base   = *(uint64_t *)(ptr + 4);
            uint64_t length = *(uint64_t *)(ptr + 12);
            uint32_t type   = *(uint32_t *)(ptr + 20);

            mem_regions[mem_region_count].base = base;
            mem_regions[mem_region_count].size = length;
            mem_regions[mem_region_count].type = type;
            mem_region_count++;

            if (type == MMAP_AVAILABLE)
                total_ram_bytes += length;

            /* Advance by size + 4 (the size field itself) */
            ptr += entry_size + 4;
        }
    } else if (flags & MB1_FLAG_MEM) {
        /* Use simple mem_lower/mem_upper */
        uint32_t mem_lower = info[4 / 4]; /* KB */
        uint32_t mem_upper = info[8 / 4]; /* KB */

        /* Lower memory (below 1MB) */
        mem_regions[0].base = 0;
        mem_regions[0].size = (uint64_t)mem_lower * 1024;
        mem_regions[0].type = 1;

        /* Upper memory (above 1MB) */
        mem_regions[1].base = 0x100000;
        mem_regions[1].size = (uint64_t)mem_upper * 1024;
        mem_regions[1].type = 1;

        mem_region_count = 2;
        total_ram_bytes = (uint64_t)(mem_lower + mem_upper) * 1024;
    }

    if (total_ram_bytes == 0)
        total_ram_bytes = 256ULL * 1024 * 1024;
}

/* -------------------------------------------------------------------------- */
/* x86-64 page table constants                                                */
/* -------------------------------------------------------------------------- */

#define PAGE_PRESENT    (1ULL << 0)
#define PAGE_WRITABLE   (1ULL << 1)
#define PAGE_USER       (1ULL << 2)
#define PAGE_PWT        (1ULL << 3)  /* Page-level Write-Through */
#define PAGE_PCD        (1ULL << 4)  /* Page-level Cache Disable */
#define PAGE_ACCESSED   (1ULL << 5)
#define PAGE_DIRTY      (1ULL << 6)
#define PAGE_HUGE       (1ULL << 7)  /* 2MB page (in PD entry) or 1GB (in PDPT) */
#define PAGE_GLOBAL     (1ULL << 8)
#define PAGE_NX         (1ULL << 63) /* No-Execute */

#define PAGE_SIZE_4K    0x1000ULL
#define PAGE_SIZE_2M    0x200000ULL

/* Page table index extraction macros */
#define PML4_INDEX(va)  (((va) >> 39) & 0x1FF)
#define PDPT_INDEX(va)  (((va) >> 30) & 0x1FF)
#define PD_INDEX(va)    (((va) >> 21) & 0x1FF)
#define PT_INDEX(va)    (((va) >> 12) & 0x1FF)

/* -------------------------------------------------------------------------- */
/* Page bitmap allocator                                                      */
/*                                                                            */
/* We maintain a bitmap of 4K pages for allocations needed by the HAL         */
/* (page table pages, etc.).  The bitmap covers a region from 16MB to 32MB    */
/* (the DMA allocator uses 8-16MB, kernel is below 8MB).                      */
/* -------------------------------------------------------------------------- */

#define PAGE_POOL_BASE   0x01000000ULL  /* 16 MB */
#define PAGE_POOL_SIZE   0x01000000ULL  /* 16 MB */
#define PAGE_POOL_PAGES  (PAGE_POOL_SIZE / PAGE_SIZE_4K)  /* 4096 pages */
#define BITMAP_SIZE      (PAGE_POOL_PAGES / 8)            /* 512 bytes */

static uint8_t page_bitmap[BITMAP_SIZE];
static bool mmu_initialized = false;

static inline void bitmap_set(uint32_t page_idx)
{
    page_bitmap[page_idx / 8] |= (1u << (page_idx % 8));
}

static inline void bitmap_clear(uint32_t page_idx)
{
    page_bitmap[page_idx / 8] &= ~(1u << (page_idx % 8));
}

static inline int bitmap_test(uint32_t page_idx)
{
    return (page_bitmap[page_idx / 8] >> (page_idx % 8)) & 1;
}

/* Allocate a single physical 4K page, returns physical address or 0 */
static uint64_t page_alloc_one(void)
{
    for (uint32_t i = 0; i < PAGE_POOL_PAGES; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            uint64_t addr = PAGE_POOL_BASE + (uint64_t)i * PAGE_SIZE_4K;
            /* Zero the page */
            uint64_t *p = (uint64_t *)(uintptr_t)addr;
            for (int j = 0; j < 512; j++)
                p[j] = 0;
            return addr;
        }
    }
    return 0; /* Out of pages */
}

/* Free a single physical 4K page */
static void page_free_one(uint64_t phys)
{
    if (phys < PAGE_POOL_BASE || phys >= PAGE_POOL_BASE + PAGE_POOL_SIZE)
        return; /* Not in our pool */
    uint32_t idx = (uint32_t)((phys - PAGE_POOL_BASE) / PAGE_SIZE_4K);
    bitmap_clear(idx);
}

/* -------------------------------------------------------------------------- */
/* CR3 access                                                                 */
/* -------------------------------------------------------------------------- */

static inline uint64_t read_cr3(void)
{
    uint64_t val;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(val));
    return val;
}

static inline void write_cr3(uint64_t val)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(val) : "memory");
}

static inline void invlpg(uint64_t virt)
{
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

/* -------------------------------------------------------------------------- */
/* HAL MMU API                                                                */
/* -------------------------------------------------------------------------- */

hal_status_t hal_mmu_init(void)
{
    /* Zero the page bitmap */
    for (uint32_t i = 0; i < BITMAP_SIZE; i++)
        page_bitmap[i] = 0;

    /* Parse multiboot1 memory map for RAM detection */
    parse_multiboot1_mmap();

    mmu_initialized = true;
    return HAL_OK;
}

hal_status_t hal_mmu_map(uint64_t virt, uint64_t phys, uint64_t size,
                          uint32_t perms, hal_mem_type_t type)
{
    if (!mmu_initialized)
        return HAL_ERROR;

    /* Get the current PML4 base from CR3 */
    uint64_t cr3 = read_cr3();
    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & ~0xFFFULL);

    /* Map each 4K page */
    uint64_t offset = 0;
    while (offset < size) {
        uint64_t va = virt + offset;
        uint64_t pa = phys + offset;

        /* Walk/create page table levels */
        uint32_t pml4i = PML4_INDEX(va);
        uint32_t pdpti = PDPT_INDEX(va);
        uint32_t pdi   = PD_INDEX(va);
        uint32_t pti   = PT_INDEX(va);

        /* PML4 -> PDPT */
        if (!(pml4[pml4i] & PAGE_PRESENT)) {
            uint64_t new_page = page_alloc_one();
            if (!new_page) return HAL_NO_MEMORY;
            pml4[pml4i] = new_page | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        }
        uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[pml4i] & ~0xFFFULL);

        /* PDPT -> PD */
        if (!(pdpt[pdpti] & PAGE_PRESENT)) {
            uint64_t new_page = page_alloc_one();
            if (!new_page) return HAL_NO_MEMORY;
            pdpt[pdpti] = new_page | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        }
        /* Check for 1GB huge page (can't split it here, skip) */
        if (pdpt[pdpti] & PAGE_HUGE) {
            offset += HAL_PAGE_1G;
            continue;
        }
        uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[pdpti] & ~0xFFFULL);

        /* PD -> PT */
        if (!(pd[pdi] & PAGE_PRESENT)) {
            uint64_t new_page = page_alloc_one();
            if (!new_page) return HAL_NO_MEMORY;
            pd[pdi] = new_page | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        }
        /* Check for 2MB huge page — we need to split it to map individual 4K pages */
        if (pd[pdi] & PAGE_HUGE) {
            /* Split 2MB page into 512 x 4K pages */
            uint64_t huge_base = pd[pdi] & ~0x1FFFFFULL;
            uint64_t new_pt_page = page_alloc_one();
            if (!new_pt_page) return HAL_NO_MEMORY;
            uint64_t *new_pt = (uint64_t *)(uintptr_t)new_pt_page;
            for (int i = 0; i < 512; i++) {
                new_pt[i] = (huge_base + i * PAGE_SIZE_4K) |
                            PAGE_PRESENT | PAGE_WRITABLE;
            }
            pd[pdi] = new_pt_page | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        }
        uint64_t *pt = (uint64_t *)(uintptr_t)(pd[pdi] & ~0xFFFULL);

        /* Build PTE flags */
        uint64_t flags = PAGE_PRESENT;
        if (perms & HAL_PAGE_WRITE) flags |= PAGE_WRITABLE;
        if (perms & HAL_PAGE_USER)  flags |= PAGE_USER;
        if (!(perms & HAL_PAGE_EXEC)) flags |= PAGE_NX;

        /* Cache attributes */
        switch (type) {
        case HAL_MEM_DEVICE:
            flags |= PAGE_PCD | PAGE_PWT; /* Uncacheable */
            break;
        case HAL_MEM_WC:
            flags |= PAGE_PWT; /* Write-combining (approximation via PWT) */
            break;
        case HAL_MEM_NORMAL:
        default:
            /* Write-back cacheable (no special bits) */
            break;
        }

        /* Set the PTE */
        pt[pti] = pa | flags;

        /* Invalidate TLB for this page */
        invlpg(va);

        offset += PAGE_SIZE_4K;
    }

    return HAL_OK;
}

hal_status_t hal_mmu_unmap(uint64_t virt, uint64_t size)
{
    if (!mmu_initialized)
        return HAL_ERROR;

    uint64_t cr3 = read_cr3();
    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & ~0xFFFULL);

    uint64_t offset = 0;
    while (offset < size) {
        uint64_t va = virt + offset;

        uint32_t pml4i = PML4_INDEX(va);
        if (!(pml4[pml4i] & PAGE_PRESENT)) { offset += PAGE_SIZE_4K; continue; }
        uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[pml4i] & ~0xFFFULL);

        uint32_t pdpti = PDPT_INDEX(va);
        if (!(pdpt[pdpti] & PAGE_PRESENT)) { offset += PAGE_SIZE_4K; continue; }
        if (pdpt[pdpti] & PAGE_HUGE) { offset += HAL_PAGE_1G; continue; }
        uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[pdpti] & ~0xFFFULL);

        uint32_t pdi = PD_INDEX(va);
        if (!(pd[pdi] & PAGE_PRESENT)) { offset += PAGE_SIZE_4K; continue; }
        if (pd[pdi] & PAGE_HUGE) { offset += HAL_PAGE_2M; continue; }
        uint64_t *pt = (uint64_t *)(uintptr_t)(pd[pdi] & ~0xFFFULL);

        uint32_t pti = PT_INDEX(va);
        pt[pti] = 0; /* Clear PTE */
        invlpg(va);

        offset += PAGE_SIZE_4K;
    }

    return HAL_OK;
}

void hal_mmu_invalidate(uint64_t virt, uint64_t size)
{
    uint64_t offset = 0;
    while (offset < size) {
        invlpg(virt + offset);
        offset += PAGE_SIZE_4K;
    }
}

uint32_t hal_mmu_get_memory_map(hal_mem_region_t *regions, uint32_t max)
{
    if (max == 0)
        return 0;

    uint32_t count = mem_region_count;
    if (count > max) count = max;

    for (uint32_t i = 0; i < count; i++) {
        regions[i].base = mem_regions[i].base;
        regions[i].size = mem_regions[i].size;
        regions[i].type = mem_regions[i].type;
    }

    return count;
}

uint64_t hal_mmu_total_ram(void)
{
    return total_ram_bytes;
}

uint64_t hal_mmu_free_ram(void)
{
    /* Estimate free RAM: total - kernel (2MB) - DMA (8MB) - page pool (16MB) */
    uint64_t used = 2ULL * 1024 * 1024     /* Kernel + boot structures */
                  + 8ULL * 1024 * 1024     /* DMA region (8-16 MB) */
                  + 16ULL * 1024 * 1024;   /* Page pool (16-32 MB) */

    /* Count allocated pages in our bitmap */
    uint32_t alloc_pages = 0;
    for (uint32_t i = 0; i < PAGE_POOL_PAGES; i++) {
        if (bitmap_test(i))
            alloc_pages++;
    }
    used += (uint64_t)alloc_pages * PAGE_SIZE_4K;

    if (total_ram_bytes > used)
        return total_ram_bytes - used;
    return 0;
}

uint64_t hal_mmu_alloc_pages(uint32_t count)
{
    if (count == 0) return 0;

    /* For a single page, use our bitmap allocator */
    if (count == 1) {
        return page_alloc_one();
    }

    /* For multiple contiguous pages, scan the bitmap for a run */
    uint32_t run_start = 0;
    uint32_t run_len = 0;

    for (uint32_t i = 0; i < PAGE_POOL_PAGES; i++) {
        if (!bitmap_test(i)) {
            if (run_len == 0) run_start = i;
            run_len++;
            if (run_len == count) {
                /* Found a contiguous run, mark all pages as allocated */
                for (uint32_t j = run_start; j < run_start + count; j++) {
                    bitmap_set(j);
                }
                uint64_t addr = PAGE_POOL_BASE + (uint64_t)run_start * PAGE_SIZE_4K;
                /* Zero all pages */
                uint64_t *p = (uint64_t *)(uintptr_t)addr;
                for (uint32_t j = 0; j < count * 512; j++)
                    p[j] = 0;
                return addr;
            }
        } else {
            run_len = 0;
        }
    }

    return 0; /* Not enough contiguous pages */
}

void hal_mmu_free_pages(uint64_t phys, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        page_free_one(phys + (uint64_t)i * PAGE_SIZE_4K);
    }
}
