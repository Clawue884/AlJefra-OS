/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- RISC-V 64-bit SMP Implementation
 * Implements hal/smp.h using SBI HSM (Hart State Management) extension.
 *
 * SBI HSM Extension ID: 0x48534D ("HSM")
 *   Function 0: HART_START  (hartid, start_addr, opaque)
 *   Function 1: HART_STOP
 *   Function 2: HART_GET_STATUS (hartid) -> 0=STARTED, 1=STOPPED, 2=START_PENDING, ...
 *   Function 3: HART_SUSPEND
 *
 * On QEMU virt, OpenSBI handles HSM.
 * All harts start in M-mode; OpenSBI parks non-boot harts until HSM HART_START.
 */

#include "../../hal/hal.h"

/* ------------------------------------------------------------------ */
/* SBI HSM constants                                                   */
/* ------------------------------------------------------------------ */

#define SBI_EXT_HSM             0x48534D

#define SBI_HSM_HART_START      0
#define SBI_HSM_HART_STOP       1
#define SBI_HSM_HART_STATUS     2
#define SBI_HSM_HART_SUSPEND    3

/* HSM hart status codes */
#define SBI_HSM_STARTED         0
#define SBI_HSM_STOPPED         1
#define SBI_HSM_START_PENDING   2
#define SBI_HSM_STOP_PENDING    3
#define SBI_HSM_SUSPENDED       4
#define SBI_HSM_SUSPEND_PENDING 5
#define SBI_HSM_RESUME_PENDING  6

/* SBI return codes */
#define SBI_SUCCESS             0
#define SBI_ERR_FAILED          (-1)
#define SBI_ERR_NOT_SUPPORTED   (-2)
#define SBI_ERR_INVALID_PARAM   (-3)
#define SBI_ERR_DENIED          (-4)
#define SBI_ERR_INVALID_ADDRESS (-5)
#define SBI_ERR_ALREADY_AVAILABLE (-6)

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

#define MAX_HARTS   256
#define STACK_SIZE  (64 * 1024)   /* 64KB per hart */

/* ------------------------------------------------------------------ */
/* SBI call                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    int64_t  error;
    uint64_t value;
} sbi_ret_t;

static sbi_ret_t sbi_call(uint64_t eid, uint64_t fid,
                            uint64_t a0, uint64_t a1, uint64_t a2)
{
    register uint64_t r_a0 __asm__("a0") = a0;
    register uint64_t r_a1 __asm__("a1") = a1;
    register uint64_t r_a2 __asm__("a2") = a2;
    register uint64_t r_a6 __asm__("a6") = fid;
    register uint64_t r_a7 __asm__("a7") = eid;

    __asm__ volatile("ecall"
        : "+r"(r_a0), "+r"(r_a1)
        : "r"(r_a2), "r"(r_a6), "r"(r_a7)
        : "memory");

    sbi_ret_t ret;
    ret.error = (int64_t)r_a0;
    ret.value = r_a1;
    return ret;
}

/* ------------------------------------------------------------------ */
/* Per-hart state                                                      */
/* ------------------------------------------------------------------ */

static hal_core_info_t   harts[MAX_HARTS];
static uint32_t          hart_count = 1;
static hal_smp_work_fn   hart_work_fn[MAX_HARTS];
static void             *hart_work_arg[MAX_HARTS];

/* Per-hart stacks */
static uint8_t hart_stacks[MAX_HARTS][STACK_SIZE] __attribute__((aligned(16)));

/* ------------------------------------------------------------------ */
/* Secondary hart entry point (called from boot.S trampoline)          */
/* ------------------------------------------------------------------ */

void riscv64_secondary_entry(uint64_t hartid);

void riscv64_secondary_entry(uint64_t hartid)
{
    if (hartid < MAX_HARTS) {
        /* Find this hart in our table */
        for (uint32_t i = 0; i < hart_count; i++) {
            if (harts[i].core_id == (uint32_t)hartid) {
                harts[i].state = HAL_CORE_ONLINE;

                if (hart_work_fn[i]) {
                    hart_work_fn[i](hart_work_arg[i]);
                }

                harts[i].state = HAL_CORE_HALTED;
                break;
            }
        }
    }

    /* Hart done: enter low-power wait */
    for (;;)
        __asm__ volatile("wfi");
}

/* ------------------------------------------------------------------ */
/* HAL Interface Implementation                                        */
/* ------------------------------------------------------------------ */

hal_status_t hal_smp_init(void)
{
    /* Read boot hart ID from tp register */
    uint64_t boot_hart;
    __asm__ volatile("mv %0, tp" : "=r"(boot_hart));

    /* Initialize BSP */
    harts[0].core_id   = (uint32_t)boot_hart;
    harts[0].state     = HAL_CORE_ONLINE;
    harts[0].stack_top = 0;

    /* Probe for additional harts using SBI HSM HART_GET_STATUS.
     * QEMU virt uses sequential hartids: 0, 1, 2, ... */
    hart_count = 1;
    for (uint32_t hartid = 0; hartid < 8; hartid++) {
        if (hartid == (uint32_t)boot_hart)
            continue;

        sbi_ret_t ret = sbi_call(SBI_EXT_HSM, SBI_HSM_HART_STATUS, hartid, 0, 0);
        if (ret.error == SBI_SUCCESS) {
            uint32_t idx = hart_count;
            harts[idx].core_id   = hartid;
            harts[idx].state     = HAL_CORE_OFFLINE;
            harts[idx].stack_top = (uint64_t)&hart_stacks[idx][STACK_SIZE];
            hart_count++;
        }
    }

    return HAL_OK;
}

uint32_t hal_smp_core_count(void)
{
    return hart_count;
}

uint32_t hal_smp_core_id(void)
{
    uint64_t hartid;
    __asm__ volatile("mv %0, tp" : "=r"(hartid));
    return (uint32_t)hartid;
}

hal_status_t hal_smp_start_core(uint32_t core_id, hal_smp_work_fn fn, void *arg)
{
    int idx = -1;
    for (uint32_t i = 0; i < hart_count; i++) {
        if (harts[i].core_id == core_id) {
            idx = (int)i;
            break;
        }
    }
    if (idx < 0)
        return HAL_ERROR;

    if (harts[idx].state == HAL_CORE_ONLINE)
        return HAL_BUSY;

    /* Store work function */
    hart_work_fn[idx]  = fn;
    hart_work_arg[idx] = arg;

    /* SBI HSM HART_START:
     *   a0 = hartid
     *   a1 = start_addr (physical)
     *   a2 = opaque (passed as a1 to the hart) */
    extern void _riscv64_secondary_start(void);

    sbi_ret_t ret = sbi_call(SBI_EXT_HSM, SBI_HSM_HART_START,
                              (uint64_t)core_id,
                              (uint64_t)&_riscv64_secondary_start,
                              (uint64_t)core_id);

    if (ret.error == SBI_SUCCESS || ret.error == SBI_ERR_ALREADY_AVAILABLE) {
        harts[idx].state = HAL_CORE_ONLINE;
        return HAL_OK;
    }

    return HAL_ERROR;
}

hal_status_t hal_smp_send_work(uint32_t core_id, hal_smp_work_fn fn, void *arg)
{
    int idx = -1;
    for (uint32_t i = 0; i < hart_count; i++) {
        if (harts[i].core_id == core_id) {
            idx = (int)i;
            break;
        }
    }
    if (idx < 0 || harts[idx].state != HAL_CORE_ONLINE)
        return HAL_ERROR;

    hart_work_fn[idx]  = fn;
    hart_work_arg[idx] = arg;

    /* TODO: Send IPI via SBI sbi_send_ipi to wake the hart */

    return HAL_OK;
}

hal_status_t hal_smp_wait_core(uint32_t core_id)
{
    int idx = -1;
    for (uint32_t i = 0; i < hart_count; i++) {
        if (harts[i].core_id == core_id) {
            idx = (int)i;
            break;
        }
    }
    if (idx < 0)
        return HAL_ERROR;

    while (harts[idx].state == HAL_CORE_ONLINE) {
        __asm__ volatile("nop");
    }

    return HAL_OK;
}

/* ------------------------------------------------------------------ */
/* Spinlock: RISC-V AMO (Atomic Memory Operations)                     */
/* ------------------------------------------------------------------ */

void hal_spin_lock(hal_spinlock_t *lock)
{
    uint64_t tmp;
    __asm__ volatile(
        "1:\n"
        "   lr.d.aq  %0, (%1)\n"       /* Load-reserved, acquire */
        "   bnez     %0, 1b\n"         /* If locked (!=0), retry */
        "   li       %0, 1\n"
        "   sc.d.rl  %0, %0, (%1)\n"   /* Store-conditional, release */
        "   bnez     %0, 1b\n"         /* If SC failed, retry */
        : "=&r"(tmp)
        : "r"(lock)
        : "memory"
    );
}

void hal_spin_unlock(hal_spinlock_t *lock)
{
    __asm__ volatile(
        "amoswap.d.rl zero, zero, (%0)\n"  /* Atomically store 0, release */
        :
        : "r"(lock)
        : "memory"
    );
}

int hal_spin_trylock(hal_spinlock_t *lock)
{
    uint64_t tmp, result;
    __asm__ volatile(
        "lr.d.aq  %0, (%2)\n"          /* Load-reserved */
        "bnez     %0, 1f\n"            /* Already locked? */
        "li       %0, 1\n"
        "sc.d.rl  %0, %0, (%2)\n"      /* Try store */
        "1:\n"
        "seqz     %1, %0\n"            /* result = (tmp == 0) ? 1 : 0 */
        : "=&r"(tmp), "=r"(result)
        : "r"(lock)
        : "memory"
    );
    return (int)result;
}
