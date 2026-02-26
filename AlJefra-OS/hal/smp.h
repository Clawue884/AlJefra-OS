/* SPDX-License-Identifier: MIT */
/* AlJefra OS — HAL SMP (Symmetric Multi-Processing) Interface
 * Architecture-independent multi-core management.
 *
 * x86-64:  APIC-based core startup (INIT/SIPI)
 * AArch64: PSCI (Power State Coordination Interface)
 * RISC-V:  SBI HSM (Hart State Management)
 */

#ifndef ALJEFRA_HAL_SMP_H
#define ALJEFRA_HAL_SMP_H

#include <stdint.h>

/* Core state */
typedef enum {
    HAL_CORE_OFFLINE = 0,
    HAL_CORE_ONLINE  = 1,
    HAL_CORE_HALTED  = 2,
} hal_core_state_t;

/* Per-core info */
typedef struct {
    uint32_t         core_id;
    hal_core_state_t state;
    uint64_t         stack_top;   /* Stack pointer for this core */
} hal_core_info_t;

/* Work function signature for SMP dispatch */
typedef void (*hal_smp_work_fn)(void *arg);

/* Initialize SMP subsystem (discovers all cores, BSP only) */
hal_status_t hal_smp_init(void);

/* Get number of cores detected */
uint32_t hal_smp_core_count(void);

/* Get current core ID */
uint32_t hal_smp_core_id(void);

/* Start an AP (application processor) running a function.
 * The AP will execute fn(arg) and then halt.
 * Returns HAL_OK if the core was successfully started. */
hal_status_t hal_smp_start_core(uint32_t core_id, hal_smp_work_fn fn, void *arg);

/* Send work to a specific core (core must already be online) */
hal_status_t hal_smp_send_work(uint32_t core_id, hal_smp_work_fn fn, void *arg);

/* Wait for a core to become idle */
hal_status_t hal_smp_wait_core(uint32_t core_id);

/* Simple spinlock (architecture-optimized) */
typedef volatile uint64_t hal_spinlock_t;

void hal_spin_lock(hal_spinlock_t *lock);
void hal_spin_unlock(hal_spinlock_t *lock);
int  hal_spin_trylock(hal_spinlock_t *lock);

#define HAL_SPINLOCK_INIT  0

#endif /* ALJEFRA_HAL_SMP_H */
