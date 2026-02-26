/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- AArch64 SMP Implementation
 * Implements hal/smp.h using ARM PSCI (Power State Coordination Interface).
 *
 * PSCI functions (SMCCC compliant):
 *   PSCI_VERSION      = 0x84000000
 *   CPU_ON            = 0xC4000003 (64-bit)
 *   CPU_OFF           = 0x84000002
 *   AFFINITY_INFO     = 0xC4000004 (64-bit)
 *   SYSTEM_RESET      = 0x84000009
 *
 * On QEMU virt, PSCI is handled by the firmware (EL3/EL2).
 * We use HVC (hypervisor call) or SMC (secure monitor call).
 */

#include "../../hal/hal.h"

/* ------------------------------------------------------------------ */
/* PSCI Function IDs                                                   */
/* ------------------------------------------------------------------ */

#define PSCI_VERSION        0x84000000
#define PSCI_CPU_ON_64      0xC4000003
#define PSCI_CPU_OFF        0x84000002
#define PSCI_AFFINITY_INFO  0xC4000004
#define PSCI_SYSTEM_RESET   0x84000009

/* PSCI return codes */
#define PSCI_SUCCESS            0
#define PSCI_NOT_SUPPORTED      (-1)
#define PSCI_INVALID_PARAMS     (-2)
#define PSCI_DENIED             (-3)
#define PSCI_ALREADY_ON         (-4)
#define PSCI_ON_PENDING         (-5)
#define PSCI_INTERNAL_FAILURE   (-6)

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

#define MAX_CORES   256
#define STACK_SIZE  (64 * 1024)   /* 64KB per core */

/* Use HVC by default (QEMU virt). Set to 1 for SMC (real hardware). */
#define USE_SMC     0

/* ------------------------------------------------------------------ */
/* Per-core state                                                      */
/* ------------------------------------------------------------------ */

static hal_core_info_t  cores[MAX_CORES];
static uint32_t         core_count = 1;  /* BSP always counted */
static hal_smp_work_fn  core_work_fn[MAX_CORES];
static void            *core_work_arg[MAX_CORES];

/* Per-core stacks (statically allocated) */
static uint8_t core_stacks[MAX_CORES][STACK_SIZE] __attribute__((aligned(16)));

/* ------------------------------------------------------------------ */
/* PSCI call wrapper                                                   */
/* ------------------------------------------------------------------ */

static int64_t psci_call(uint64_t fid, uint64_t a1, uint64_t a2, uint64_t a3)
{
    register uint64_t x0 __asm__("x0") = fid;
    register uint64_t x1 __asm__("x1") = a1;
    register uint64_t x2 __asm__("x2") = a2;
    register uint64_t x3 __asm__("x3") = a3;

#if USE_SMC
    __asm__ volatile("smc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x3)
        : "x4", "x5", "x6", "x7",
          "x8", "x9", "x10", "x11",
          "x12", "x13", "x14", "x15",
          "x16", "x17", "memory");
#else
    __asm__ volatile("hvc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x3)
        : "x4", "x5", "x6", "x7",
          "x8", "x9", "x10", "x11",
          "x12", "x13", "x14", "x15",
          "x16", "x17", "memory");
#endif

    return (int64_t)x0;
}

/* ------------------------------------------------------------------ */
/* Secondary core entry point (called from boot.S trampoline)          */
/* ------------------------------------------------------------------ */

void aarch64_secondary_entry(uint64_t core_id);

void aarch64_secondary_entry(uint64_t core_id)
{
    /* Mark core as online */
    if (core_id < MAX_CORES) {
        cores[core_id].state = HAL_CORE_ONLINE;

        /* Execute the work function if one was assigned */
        if (core_work_fn[core_id]) {
            core_work_fn[core_id](core_work_arg[core_id]);
        }

        cores[core_id].state = HAL_CORE_HALTED;
    }

    /* Core done: enter low-power wait */
    for (;;)
        __asm__ volatile("wfi");
}

/* ------------------------------------------------------------------ */
/* HAL Interface Implementation                                        */
/* ------------------------------------------------------------------ */

hal_status_t hal_smp_init(void)
{
    /* Query PSCI version to verify it's available */
    int64_t version = psci_call(PSCI_VERSION, 0, 0, 0);
    if (version < 0)
        return HAL_NOT_SUPPORTED;

    /* Initialize BSP (core 0) */
    uint64_t mpidr;
    __asm__ volatile("mrs %0, MPIDR_EL1" : "=r"(mpidr));
    uint32_t bsp_id = mpidr & 0xFF;

    cores[0].core_id   = bsp_id;
    cores[0].state     = HAL_CORE_ONLINE;
    cores[0].stack_top = 0;  /* BSP uses boot stack */

    /* Probe for additional cores using AFFINITY_INFO.
     * QEMU virt uses linear MPIDR Aff0 = 0, 1, 2, ...
     * We probe up to 8 cores. */
    core_count = 1;
    for (uint32_t i = 1; i < 8; i++) {
        int64_t status = psci_call(PSCI_AFFINITY_INFO, (uint64_t)i, 0, 0);
        /* status: 0=ON, 1=OFF, 2=ON_PENDING */
        if (status >= 0) {
            cores[core_count].core_id   = i;
            cores[core_count].state     = HAL_CORE_OFFLINE;
            cores[core_count].stack_top = (uint64_t)&core_stacks[core_count][STACK_SIZE];
            core_count++;
        }
    }

    return HAL_OK;
}

uint32_t hal_smp_core_count(void)
{
    return core_count;
}

uint32_t hal_smp_core_id(void)
{
    uint64_t mpidr;
    __asm__ volatile("mrs %0, MPIDR_EL1" : "=r"(mpidr));
    return (uint32_t)(mpidr & 0xFF);
}

hal_status_t hal_smp_start_core(uint32_t core_id, hal_smp_work_fn fn, void *arg)
{
    /* Find the core in our table */
    int idx = -1;
    for (uint32_t i = 0; i < core_count; i++) {
        if (cores[i].core_id == core_id) {
            idx = (int)i;
            break;
        }
    }
    if (idx < 0)
        return HAL_ERROR;

    if (cores[idx].state == HAL_CORE_ONLINE)
        return HAL_BUSY;

    /* Store work function for the core to pick up */
    core_work_fn[idx]  = fn;
    core_work_arg[idx] = arg;

    /* PSCI CPU_ON:
     * x1 = target_cpu MPIDR (Aff0 = core_id)
     * x2 = entry_point (physical address)
     * x3 = context_id (passed as x0 to entry point) */
    extern void _aarch64_secondary_start(void);  /* Defined in boot.S */

    int64_t ret = psci_call(PSCI_CPU_ON_64,
                            (uint64_t)core_id,
                            (uint64_t)&_aarch64_secondary_start,
                            (uint64_t)core_id);  /* context = core_id */

    if (ret == PSCI_SUCCESS || ret == PSCI_ALREADY_ON) {
        cores[idx].state = HAL_CORE_ONLINE;
        return HAL_OK;
    }

    return HAL_ERROR;
}

hal_status_t hal_smp_send_work(uint32_t core_id, hal_smp_work_fn fn, void *arg)
{
    /* TODO: Implement IPI-based work queue for online cores.
     * For now, use a simple polling mechanism. */
    int idx = -1;
    for (uint32_t i = 0; i < core_count; i++) {
        if (cores[i].core_id == core_id) {
            idx = (int)i;
            break;
        }
    }
    if (idx < 0)
        return HAL_ERROR;

    if (cores[idx].state != HAL_CORE_ONLINE)
        return HAL_ERROR;

    core_work_fn[idx]  = fn;
    core_work_arg[idx] = arg;

    /* Send SGI (Software Generated Interrupt) to wake the core */
    /* TODO: Implement GIC SGI send */

    return HAL_OK;
}

hal_status_t hal_smp_wait_core(uint32_t core_id)
{
    int idx = -1;
    for (uint32_t i = 0; i < core_count; i++) {
        if (cores[i].core_id == core_id) {
            idx = (int)i;
            break;
        }
    }
    if (idx < 0)
        return HAL_ERROR;

    /* Busy-wait until the core finishes its work (state = HALTED) */
    while (cores[idx].state == HAL_CORE_ONLINE) {
        __asm__ volatile("yield");
    }

    return HAL_OK;
}

/* ------------------------------------------------------------------ */
/* Spinlock: AArch64 exclusive monitor (LDAXR / STLXR)                */
/* ------------------------------------------------------------------ */

void hal_spin_lock(hal_spinlock_t *lock)
{
    uint64_t tmp, val;
    __asm__ volatile(
        "   sevl\n"
        "1: wfe\n"                           /* Wait for event (power-saving spin) */
        "2: ldaxr %0, [%2]\n"               /* Load-acquire exclusive */
        "   cbnz  %0, 1b\n"                 /* If locked (!=0), wait again */
        "   stxr  %w1, %3, [%2]\n"          /* Try to store 1 (acquire lock) */
        "   cbnz  %w1, 2b\n"                /* If store failed, retry */
        : "=&r"(val), "=&r"(tmp)
        : "r"(lock), "r"((uint64_t)1)
        : "memory"
    );
}

void hal_spin_unlock(hal_spinlock_t *lock)
{
    __asm__ volatile(
        "stlr %1, [%0]\n"                   /* Store-release 0 (release lock) */
        :
        : "r"(lock), "r"((uint64_t)0)
        : "memory"
    );
}

int hal_spin_trylock(hal_spinlock_t *lock)
{
    uint64_t val;
    uint32_t status;
    __asm__ volatile(
        "ldaxr %0, [%2]\n"                  /* Load-acquire exclusive */
        "cbnz  %0, 1f\n"                    /* If locked, fail */
        "stxr  %w1, %3, [%2]\n"             /* Try to acquire */
        "b     2f\n"
        "1: mov %w1, #1\n"                  /* Failed: status = 1 */
        "2:\n"
        : "=&r"(val), "=&r"(status)
        : "r"(lock), "r"((uint64_t)1)
        : "memory"
    );
    return (status == 0) ? 1 : 0;  /* 1 = acquired, 0 = failed */
}
