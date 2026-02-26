/* SPDX-License-Identifier: MIT */
/* AlJefra OS — x86-64 SMP HAL Implementation (Standalone)
 *
 * In standalone mode, we only boot the BSP (core 0).
 * APIC ID is read directly from CPUID leaf 1.
 * Full AP startup (INIT/SIPI sequence) can be added later.
 */

#include "../../hal/hal.h"

/* -------------------------------------------------------------------------- */
/* CPUID helper                                                               */
/* -------------------------------------------------------------------------- */

static inline void cpuid_smp(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                              uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile (
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0)
    );
}

/* -------------------------------------------------------------------------- */
/* Core state tracking                                                        */
/* -------------------------------------------------------------------------- */

#define MAX_CORES 256

static hal_core_info_t core_info[MAX_CORES];
static uint32_t num_cores = 0;
static uint32_t bsp_apic_id = 0;
static bool smp_initialized = false;

/* Per-core work function and argument */
static volatile hal_smp_work_fn core_work_fn[MAX_CORES];
static volatile void *core_work_arg[MAX_CORES];
static volatile uint64_t core_done[MAX_CORES];

/* -------------------------------------------------------------------------- */
/* HAL SMP API                                                                */
/* -------------------------------------------------------------------------- */

hal_status_t hal_smp_init(void)
{
    /* Read BSP APIC ID from CPUID leaf 1, EBX[31:24] */
    uint32_t eax, ebx, ecx, edx;
    cpuid_smp(1, &eax, &ebx, &ecx, &edx);
    bsp_apic_id = (ebx >> 24) & 0xFF;

    /* In standalone mode, we only have the BSP.
     * CPUID leaf 1 EBX[23:16] gives max logical CPUs per package,
     * but without INIT/SIPI we can only use core 0. */
    num_cores = 1;

    core_info[0].core_id = bsp_apic_id;
    core_info[0].state = HAL_CORE_ONLINE;
    core_info[0].stack_top = 0;
    core_work_fn[0] = 0;
    core_work_arg[0] = 0;
    core_done[0] = 0;

    for (uint32_t i = 1; i < MAX_CORES; i++) {
        core_info[i].core_id = i;
        core_info[i].state = HAL_CORE_HALTED;
        core_info[i].stack_top = 0;
        core_work_fn[i] = 0;
        core_work_arg[i] = 0;
        core_done[i] = 0;
    }

    smp_initialized = true;
    return HAL_OK;
}

uint32_t hal_smp_core_count(void)
{
    return num_cores;
}

uint32_t hal_smp_core_id(void)
{
    /* Read APIC ID from CPUID leaf 1, EBX[31:24] */
    uint32_t eax, ebx, ecx, edx;
    cpuid_smp(1, &eax, &ebx, &ecx, &edx);
    return (ebx >> 24) & 0xFF;
}

hal_status_t hal_smp_start_core(uint32_t core_id, hal_smp_work_fn fn, void *arg)
{
    if (core_id >= num_cores)
        return HAL_ERROR;

    /* In standalone mode, only BSP is available.
     * AP startup requires INIT/SIPI sequence + AP trampoline code,
     * which can be added in a future enhancement. */
    if (core_id != 0)
        return HAL_ERROR;

    /* Run on BSP directly */
    core_work_fn[core_id] = fn;
    core_work_arg[core_id] = arg;
    core_done[core_id] = 0;

    if (fn) {
        core_info[core_id].state = HAL_CORE_ONLINE;
        fn(arg);
    }

    core_info[core_id].state = HAL_CORE_HALTED;
    core_done[core_id] = 1;
    __asm__ volatile ("mfence" ::: "memory");

    return HAL_OK;
}

hal_status_t hal_smp_send_work(uint32_t core_id, hal_smp_work_fn fn, void *arg)
{
    return hal_smp_start_core(core_id, fn, arg);
}

hal_status_t hal_smp_wait_core(uint32_t core_id)
{
    if (core_id >= num_cores)
        return HAL_ERROR;

    while (!core_done[core_id]) {
        __asm__ volatile ("pause" ::: "memory");
    }

    return HAL_OK;
}

/* -------------------------------------------------------------------------- */
/* Spinlock implementation — x86-64 LOCK XCHG / MOV                          */
/*                                                                            */
/* We use test-and-test-and-set (TTAS) for better performance:                */
/* first check the lock value without LOCK prefix (cache-local read),         */
/* then attempt the atomic exchange only if the lock appears free.            */
/* -------------------------------------------------------------------------- */

void hal_spin_lock(hal_spinlock_t *lock)
{
    for (;;) {
        /* Test (cache-local read) */
        while (*lock != 0) {
            __asm__ volatile ("pause" ::: "memory");
        }
        /* Test-and-set (atomic) */
        uint64_t old;
        __asm__ volatile (
            "lock xchgq %0, %1"
            : "=r"(old), "+m"(*lock)
            : "0"((uint64_t)1)
            : "memory"
        );
        if (old == 0)
            return; /* Lock acquired */
    }
}

void hal_spin_unlock(hal_spinlock_t *lock)
{
    __asm__ volatile ("" ::: "memory");
    *lock = 0;
}

int hal_spin_trylock(hal_spinlock_t *lock)
{
    uint64_t old;
    __asm__ volatile (
        "lock xchgq %0, %1"
        : "=r"(old), "+m"(*lock)
        : "0"((uint64_t)1)
        : "memory"
    );
    return (old == 0) ? 1 : 0;
}
