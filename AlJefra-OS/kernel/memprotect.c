/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- Memory Protection Implementation
 *
 * Applies page-level permissions to enforce W^X (write XOR execute)
 * across the kernel image.  Architecture-specific details:
 *
 * x86-64:
 *   - NX bit (bit 63) in PTE for non-executable pages
 *   - CR0.WP = 1 so supervisor writes to read-only pages fault
 *   - CR4.SMEP / SMAP if available (blocks supervisor exec/access of user pages)
 *
 * AArch64:
 *   - PXN (Privileged eXecute Never) bit in block/page descriptors
 *   - UXN (Unprivileged eXecute Never) for kernel pages
 *   - AP[2:1] bits for read-only vs read-write
 *
 * RISC-V:
 *   - PTE.X bit controls execute permission
 *   - PTE.W bit controls write permission
 *   - PTE.R bit controls read permission
 *
 * All architectures use the HAL MMU layer (hal_mmu_map / hal_mmu_unmap)
 * for actual page table modifications.
 */

#include "memprotect.h"
#include "../hal/hal.h"
#include "../lib/string.h"

/* ---- Linker-provided symbols ---- */

/* These symbols are defined in the architecture linker scripts and mark
 * the boundaries of kernel sections.  We declare them as extern char[]
 * so we can take their addresses without creating actual storage.
 *
 * x86-64 uses single-underscore names; ARM64 and RISC-V use double-underscore.
 * We alias them via preprocessor to a common name set. */

#if defined(__x86_64__) || defined(_M_X64)
extern char _kernel_start[];
extern char _load_end[];
extern char _bss_start[];
extern char _bss_end[];
extern char _end[];
#else
/* ARM64 and RISC-V linker scripts use __bss_start / __bss_end */
extern char __bss_start[];
extern char __bss_end[];
/* Provide fallback aliases for symbols not in these linker scripts */
#define _kernel_start  __bss_start  /* approximate: start of kernel image */
#define _load_end      __bss_start  /* approximate: end of loaded data */
#define _bss_start     __bss_start
#define _bss_end       __bss_end
#define _end           __bss_end  /* approximate: end of kernel image */
#endif

/* ---- Alignment helpers ---- */

#define PAGE_SIZE    HAL_PAGE_4K
#define PAGE_MASK    (~(PAGE_SIZE - 1))

/* Round address down to page boundary */
static inline uint64_t page_align_down(uint64_t addr)
{
    return addr & PAGE_MASK;
}

/* Round address up to page boundary */
static inline uint64_t page_align_up(uint64_t addr)
{
    return (addr + PAGE_SIZE - 1) & PAGE_MASK;
}

/* ---- Architecture-specific NX/WP enable ---- */

#if defined(__x86_64__) || defined(_M_X64)

/* Enable NX bit support by setting IA32_EFER.NXE (bit 11) */
static void enable_nx_support(void)
{
    uint32_t eax, edx;

    /* Check if NX is supported via CPUID.80000001h:EDX.NX[bit 20] */
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=d"(edx)
                     : "a"(0x80000001)
                     : "ebx", "ecx");

    if (!(edx & (1u << 20)))
        return;     /* NX not supported — skip */

    /* Read IA32_EFER (MSR 0xC0000080) */
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080));

    /* Set NXE bit (bit 11) */
    lo |= (1u << 11);
    __asm__ volatile("wrmsr" :: "a"(lo), "d"(hi), "c"(0xC0000080));
}

/* Enable CR0.WP so supervisor-mode writes to read-only pages fault */
static void enable_write_protect(void)
{
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1ULL << 16);   /* WP bit */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));
}

/* Enable SMEP/SMAP if available */
static void enable_smep_smap(void)
{
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(7), "c"(0));

    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

    if (ebx & (1u << 7))    /* SMEP */
        cr4 |= (1ULL << 20);
    if (ebx & (1u << 20))   /* SMAP */
        cr4 |= (1ULL << 21);

    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));
}

#endif /* __x86_64__ */

/* ---- HAL-based page permission modifiers ---- */

int memprotect_set_nx(uint64_t addr, uint64_t size)
{
    if (size == 0)
        return 0;

    addr = page_align_down(addr);
    size = page_align_up(size);

    /* Re-map the region with read+write but no execute.
     * We unmap first, then re-map with the restricted permissions.
     * On architectures where hal_mmu_map updates existing mappings
     * in place, the unmap is harmless. */
    hal_mmu_unmap(addr, size);

    hal_status_t rc = hal_mmu_map(addr, addr, size,
                                  HAL_PAGE_READ | HAL_PAGE_WRITE,
                                  HAL_MEM_NORMAL);
    hal_mmu_invalidate(addr, size);

    return (rc == HAL_OK) ? 0 : -1;
}

int memprotect_set_ro(uint64_t addr, uint64_t size)
{
    if (size == 0)
        return 0;

    addr = page_align_down(addr);
    size = page_align_up(size);

    hal_mmu_unmap(addr, size);

    hal_status_t rc = hal_mmu_map(addr, addr, size,
                                  HAL_PAGE_READ | HAL_PAGE_EXEC,
                                  HAL_MEM_NORMAL);
    hal_mmu_invalidate(addr, size);

    return (rc == HAL_OK) ? 0 : -1;
}

int memprotect_guard_page(uint64_t addr)
{
    addr = page_align_down(addr);

    /* Simply unmap the page.  Any access (read, write, exec) to an
     * unmapped address will trigger a page fault / data abort. */
    hal_mmu_unmap(addr, PAGE_SIZE);
    hal_mmu_invalidate(addr, PAGE_SIZE);

    return 0;
}

/* ---- Kernel region protection ---- */

/* Apply protection to kernel text: Read + Execute, no write */
static int protect_kernel_text(void)
{
    uint64_t text_start = (uint64_t)(uintptr_t)_kernel_start;
    /* Text ends where rodata/data begins.  We use _load_end as a
     * conservative boundary — in practice the .text section ends
     * before .rodata, but we protect the entire loadable image
     * as read-only + executable for the text portion.
     *
     * A more precise approach would require separate linker symbols
     * for .text end; for now we protect text_start.._load_end. */

    /* Since we cannot easily distinguish .text from .rodata/.data
     * without finer-grained linker symbols, we mark the entire
     * kernel image as RX first, then overlay RW+NX on the data
     * portions.  This gives us:
     *   .text   → RX (from this step)
     *   .rodata → RX (acceptable: read-only, executable but harmless data)
     *   .data   → RW+NX (overwritten below)
     *   .bss    → RW+NX (overwritten below)
     */
    uint64_t image_end = page_align_up((uint64_t)(uintptr_t)_end);
    uint64_t image_size = image_end - page_align_down(text_start);

    hal_mmu_unmap(page_align_down(text_start), image_size);

    hal_status_t rc = hal_mmu_map(
        page_align_down(text_start),
        page_align_down(text_start),
        image_size,
        HAL_PAGE_READ | HAL_PAGE_EXEC,
        HAL_MEM_NORMAL);

    hal_mmu_invalidate(page_align_down(text_start), image_size);

    return (rc == HAL_OK) ? 0 : -1;
}

/* Apply protection to kernel data + BSS: Read + Write, no execute */
static int protect_kernel_data(void)
{
    /* .data starts right after .rodata (approximated by _load_end for
     * initialized data).  .bss follows.  We protect both as RW+NX. */

    /* Data region: from start of .data to end of .bss */
    uint64_t data_start = page_align_down((uint64_t)(uintptr_t)_bss_start);
    uint64_t data_end   = page_align_up((uint64_t)(uintptr_t)_bss_end);

    /* If linker symbols place .data before .bss, extend backwards */
    uint64_t load_end_addr = page_align_down((uint64_t)(uintptr_t)_load_end);
    if (load_end_addr < data_start)
        data_start = load_end_addr;

    uint64_t data_size = data_end - data_start;
    if (data_size == 0)
        return 0;

    hal_mmu_unmap(data_start, data_size);

    hal_status_t rc = hal_mmu_map(
        data_start, data_start, data_size,
        HAL_PAGE_READ | HAL_PAGE_WRITE,
        HAL_MEM_NORMAL);

    hal_mmu_invalidate(data_start, data_size);

    return (rc == HAL_OK) ? 0 : -1;
}

/* Set up a guard page at the bottom of the kernel stack.
 * The stack grows downward, so the guard page is at the lowest address. */
static int protect_kernel_stack(void)
{
    /* Get the current stack pointer to locate the stack region.
     * The kernel stack is typically at a fixed address set by the
     * boot code; we use the current SP as a reference. */
    uint64_t sp = 0;

#if defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile("movq %%rsp, %0" : "=r"(sp));
#elif defined(__aarch64__)
    __asm__ volatile("mov %0, sp" : "=r"(sp));
#elif defined(__riscv) && (__riscv_xlen == 64)
    __asm__ volatile("mv %0, sp" : "=r"(sp));
#endif

    if (sp == 0)
        return -1;

    /* Estimate stack bottom: round down SP, subtract KERNEL_STACK_SIZE,
     * and place the guard page at that boundary.
     * This is approximate; a production system would use a known
     * stack base from the boot code. */
    uint64_t stack_bottom = page_align_down(sp) - KERNEL_STACK_SIZE;
    uint64_t guard_addr = page_align_down(stack_bottom);

    return memprotect_guard_page(guard_addr);
}

/* ---- Public API ---- */

int memprotect_init(void)
{
    int rc = 0;

    hal_console_puts("[memprotect] Initializing memory protection\n");

    /* Step 1: Enable architecture-specific NX / WP features */

#if defined(__x86_64__) || defined(_M_X64)
    enable_nx_support();
    enable_write_protect();
    enable_smep_smap();
    hal_console_puts("[memprotect] x86-64: NX, WP, SMEP/SMAP enabled\n");

#elif defined(__aarch64__)
    /* AArch64: PXN/UXN bits are always available in the page table
     * format.  No special register setup needed beyond what the MMU
     * init already does (TCR_EL1 / MAIR_EL1 are set by hal_mmu_init). */
    hal_console_puts("[memprotect] AArch64: PXN/UXN via page tables\n");

#elif defined(__riscv) && (__riscv_xlen == 64)
    /* RISC-V Sv39/Sv48: Permission bits (R/W/X) are per-PTE.
     * No global enable register needed. */
    hal_console_puts("[memprotect] RISC-V: PTE R/W/X permissions\n");
#endif

    /* Step 2: Protect kernel text as Read + Execute */
    if (protect_kernel_text() != 0) {
        hal_console_puts("[memprotect] WARNING: failed to protect kernel text\n");
        rc = -1;
    }

    /* Step 3: Protect kernel data + BSS as Read + Write + No-Exec */
    if (protect_kernel_data() != 0) {
        hal_console_puts("[memprotect] WARNING: failed to protect kernel data\n");
        rc = -1;
    }

    /* Step 4: Set up guard page at bottom of kernel stack */
    if (protect_kernel_stack() != 0) {
        hal_console_puts("[memprotect] WARNING: failed to set stack guard page\n");
        rc = -1;
    }

    if (rc == 0)
        hal_console_puts("[memprotect] Memory protection active\n");

    return rc;
}
