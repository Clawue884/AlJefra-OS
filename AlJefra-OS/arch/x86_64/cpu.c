/* SPDX-License-Identifier: MIT */
/* AlJefra OS — x86-64 CPU HAL Implementation (Standalone)
 * CPUID, FPU/SSE, RDTSC, RDRAND.  No AlJefra b_system() dependency.
 * Core count and APIC ID read directly from CPUID.
 */

#include "../../hal/hal.h"
#include "../../lib/string.h"

/* -------------------------------------------------------------------------- */
/* Internal helpers                                                           */
/* -------------------------------------------------------------------------- */

static hal_cpu_info_t cached_info;
static bool info_cached = false;

/* Execute CPUID instruction */
static inline void cpuid(uint32_t leaf, uint32_t subleaf,
                          uint32_t *eax, uint32_t *ebx,
                          uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile (
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf)
    );
}

/* Read CR0 */
static inline uint64_t read_cr0(void)
{
    uint64_t val;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(val));
    return val;
}

/* Write CR0 */
static inline void write_cr0(uint64_t val)
{
    __asm__ volatile ("mov %0, %%cr0" : : "r"(val));
}

/* Read CR4 */
static inline uint64_t read_cr4(void)
{
    uint64_t val;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(val));
    return val;
}

/* Write CR4 */
static inline void write_cr4(uint64_t val)
{
    __asm__ volatile ("mov %0, %%cr4" : : "r"(val));
}

/* -------------------------------------------------------------------------- */
/* HAL CPU API                                                                */
/* -------------------------------------------------------------------------- */

hal_status_t hal_cpu_init(void)
{
    uint32_t eax, ebx, ecx, edx;

    /* ---- Enable FPU ---- */
    uint64_t cr0 = read_cr0();
    cr0 &= ~(1ULL << 2);   /* Clear CR0.EM (emulation) */
    cr0 |=  (1ULL << 1);   /* Set CR0.MP (monitor coprocessor) */
    write_cr0(cr0);

    /* ---- Enable SSE (OSFXSR + OSXMMEXCPT) ---- */
    uint64_t cr4 = read_cr4();
    cr4 |= (1ULL << 9);    /* CR4.OSFXSR */
    cr4 |= (1ULL << 10);   /* CR4.OSXMMEXCPT */
    write_cr4(cr4);

    /* ---- Populate cached CPU info ---- */
    memset(&cached_info, 0, sizeof(cached_info));

    /* Vendor string: CPUID leaf 0 */
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    /* Vendor string is EBX:EDX:ECX (12 chars) */
    memcpy(&cached_info.vendor[0], &ebx, 4);
    memcpy(&cached_info.vendor[4], &edx, 4);
    memcpy(&cached_info.vendor[8], &ecx, 4);
    cached_info.vendor[12] = '\0';

    /* Feature flags: CPUID leaf 1 */
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);

    if (edx & (1u << 0))  cached_info.features |= HAL_CPU_FEAT_FPU;
    if (edx & (1u << 25)) cached_info.features |= HAL_CPU_FEAT_SSE;
    if (ecx & (1u << 28)) cached_info.features |= HAL_CPU_FEAT_AVX;
    if (ecx & (1u << 25)) cached_info.features |= HAL_CPU_FEAT_AES;
    if (ecx & (1u << 30)) cached_info.features |= HAL_CPU_FEAT_RDRAND;

    /* Cache line size from CLFLUSH line size field (bits 15:8 of EBX * 8) */
    cached_info.cache_line_bytes = ((ebx >> 8) & 0xFF) * 8;
    if (cached_info.cache_line_bytes == 0)
        cached_info.cache_line_bytes = 64; /* Sensible default */

    /* Extended feature flags: leaf 7 sub 0 */
    cpuid(7, 0, &eax, &ebx, &ecx, &edx);
    if (ebx & (1u << 0))  cached_info.features |= HAL_CPU_FEAT_CRC32; /* Actually BMI1, but CRC32 is SSE4.2 */

    /* Re-check CPUID leaf 1 ECX for SSE4.2 CRC32 */
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    if (ecx & (1u << 20)) cached_info.features |= HAL_CPU_FEAT_CRC32;

    /* Atomics always supported on x86-64 (LOCK prefix) */
    cached_info.features |= HAL_CPU_FEAT_ATOMICS;

    /* Model name string: CPUID leaves 0x80000002-0x80000004 */
    cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x80000004) {
        uint32_t *model = (uint32_t *)cached_info.model;
        cpuid(0x80000002, 0, &model[0], &model[1], &model[2], &model[3]);
        cpuid(0x80000003, 0, &model[4], &model[5], &model[6], &model[7]);
        cpuid(0x80000004, 0, &model[8], &model[9], &model[10], &model[11]);
    }

    /* Core count from CPUID leaf 1, EBX[23:16] = max logical processors per package.
     * This gives the number the CPU *supports*, not how many are actually online.
     * For SMP-aware count, use hal_smp_core_count() instead. */
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    cached_info.cores_logical = (ebx >> 16) & 0xFF;
    if (cached_info.cores_logical == 0)
        cached_info.cores_logical = 1;
    cached_info.cores_physical = cached_info.cores_logical;

    info_cached = true;
    return HAL_OK;
}

void hal_cpu_get_info(hal_cpu_info_t *info)
{
    if (!info_cached) {
        hal_cpu_init();
    }
    memcpy(info, &cached_info, sizeof(hal_cpu_info_t));
}

uint64_t hal_cpu_id(void)
{
    /* Read initial APIC ID from CPUID leaf 1, EBX[31:24] */
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    return (ebx >> 24) & 0xFF;
}

uint32_t hal_cpu_count(void)
{
    /* CPUID leaf 1, EBX[23:16] = max logical processors per package */
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    uint32_t count = (ebx >> 16) & 0xFF;
    return count > 0 ? count : 1;
}

void hal_cpu_halt(void)
{
    __asm__ volatile ("hlt");
}

void hal_cpu_enable_interrupts(void)
{
    __asm__ volatile ("sti");
}

void hal_cpu_disable_interrupts(void)
{
    __asm__ volatile ("cli");
}

void hal_cpu_memory_barrier(void)
{
    __asm__ volatile ("mfence" ::: "memory");
}

uint64_t hal_cpu_cycles(void)
{
    uint32_t lo, hi;
    __asm__ volatile (
        "rdtscp"
        : "=a"(lo), "=d"(hi)
        :
        : "rcx"   /* rdtscp clobbers ECX with IA32_TSC_AUX */
    );
    return ((uint64_t)hi << 32) | lo;
}

uint64_t hal_cpu_random(void)
{
    uint64_t val;
    unsigned char ok;

    /* Check if RDRAND is supported */
    if (!(cached_info.features & HAL_CPU_FEAT_RDRAND)) {
        return 0;
    }

    /* Try RDRAND up to 10 times (Intel recommendation) */
    for (int i = 0; i < 10; i++) {
        __asm__ volatile (
            "rdrand %0\n\t"
            "setc %1"
            : "=r"(val), "=qm"(ok)
        );
        if (ok) return val;
    }
    return 0;
}
