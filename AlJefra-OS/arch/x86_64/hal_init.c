/* SPDX-License-Identifier: MIT */
/* AlJefra OS — x86-64 Master HAL Initialization
 * Calls all sub-system init functions in the correct order.
 *
 * Boot sequence:
 *   1. Console (earliest output for diagnostics)
 *   2. CPU (CPUID, FPU/SSE enable)
 *   3. MMU (page allocator init, identity map already set by Pure64)
 *   4. Interrupts (callback registration)
 *   5. Timer (verify kernel timer works)
 *   6. Bus (PCIe config space ready)
 *   7. SMP (detect cores)
 */

#include "../../hal/hal.h"

/* -------------------------------------------------------------------------- */
/* HAL master init                                                            */
/* -------------------------------------------------------------------------- */

hal_status_t hal_init(void)
{
    hal_status_t rc;

    /* 1. Console — must be first so we can print diagnostic messages */
    rc = hal_console_init();
    if (rc != HAL_OK)
        return rc;

    hal_console_puts("[HAL] Console initialized\n");

    /* 2. CPU — detect features, enable FPU/SSE */
    rc = hal_cpu_init();
    if (rc != HAL_OK) {
        hal_console_puts("[HAL] ERROR: CPU init failed\n");
        return rc;
    }

    {
        hal_cpu_info_t info;
        hal_cpu_get_info(&info);
        hal_console_printf("[HAL] CPU: %s (%s)\n", info.model, info.vendor);
        hal_console_printf("[HAL] Features: 0x%x, Cores: %u, Cache line: %u B\n",
                           info.features, info.cores_logical, info.cache_line_bytes);
    }

    /* 3. MMU — initialize page allocator (identity map already set by Pure64) */
    rc = hal_mmu_init();
    if (rc != HAL_OK) {
        hal_console_puts("[HAL] ERROR: MMU init failed\n");
        return rc;
    }

    hal_console_printf("[HAL] MMU: %llu MB total, %llu MB free\n",
                       hal_mmu_total_ram() / (1024 * 1024),
                       hal_mmu_free_ram() / (1024 * 1024));

    /* 4. Interrupts — set up callback tables */
    rc = hal_irq_init();
    if (rc != HAL_OK) {
        hal_console_puts("[HAL] ERROR: IRQ init failed\n");
        return rc;
    }

    hal_console_puts("[HAL] Interrupts initialized\n");

    /* 5. Timer — verify kernel timer */
    rc = hal_timer_init();
    if (rc != HAL_OK) {
        hal_console_puts("[HAL] ERROR: Timer init failed\n");
        return rc;
    }

    hal_console_printf("[HAL] Timer: %llu ns since boot\n", hal_timer_ns());

    /* 6. Bus — PCIe enumeration ready */
    rc = hal_bus_init();
    if (rc != HAL_OK) {
        hal_console_puts("[HAL] ERROR: Bus init failed\n");
        return rc;
    }

    hal_console_puts("[HAL] PCIe bus initialized\n");

    /* 7. SMP — detect and configure cores */
    rc = hal_smp_init();
    if (rc != HAL_OK) {
        hal_console_puts("[HAL] ERROR: SMP init failed\n");
        return rc;
    }

    hal_console_printf("[HAL] SMP: %u cores detected (BSP ID: %u)\n",
                       hal_smp_core_count(), hal_smp_core_id());

    hal_console_puts("[HAL] x86-64 HAL initialization complete\n");

    return HAL_OK;
}
