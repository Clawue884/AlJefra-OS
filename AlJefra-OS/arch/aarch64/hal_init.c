/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- AArch64 Master HAL Initialization
 * Implements hal_init() for AArch64, calling all subsystem init functions
 * in the correct order.
 */

#include "../../hal/hal.h"

/* Forward declaration of the timer IRQ registration */
extern void aarch64_timer_irq_handler(uint32_t irq, void *ctx);

/* Kernel main entry point (defined by the kernel) */
extern void kernel_main(void);

/* ------------------------------------------------------------------ */
/* AArch64 Virtual Timer IRQ numbers                                   */
/* ------------------------------------------------------------------ */

/* On QEMU virt machine:
 *   PPI 27 = Secure Physical Timer (IRQ 27)
 *   PPI 30 = Non-secure Physical Timer (IRQ 30)
 *   PPI 27 = Virtual Timer (IRQ 27)
 * GIC numbering: PPI base = 16, so:
 *   Virtual Timer = 16 + 11 = 27 */
#define AARCH64_VTIMER_IRQ  27

/* ------------------------------------------------------------------ */
/* hal_init() -- master initialization sequence                        */
/* ------------------------------------------------------------------ */

hal_status_t hal_init(void)
{
    hal_status_t st;

    /* 1. Console first -- enables debug output ASAP */
    st = hal_console_init();
    if (st != HAL_OK) {
        /* Can't even print an error... just try to continue */
    }

    hal_console_puts("AlJefra OS -- AArch64 HAL Init\n");

    /* 2. CPU: detect features, enable FPU/NEON */
    st = hal_cpu_init();
    if (st != HAL_OK) {
        hal_console_puts("[HAL] CPU init failed\n");
        return st;
    }
    hal_console_puts("[HAL] CPU initialized\n");

    /* Print CPU info */
    {
        hal_cpu_info_t info;
        hal_cpu_get_info(&info);
        hal_console_printf("[HAL] CPU: %s %s\n", info.vendor, info.model);
        hal_console_printf("[HAL] Features: 0x%x, Cache line: %u bytes\n",
                           info.features, info.cache_line_bytes);
    }

    /* 3. Interrupt controller (GIC) */
    st = hal_irq_init();
    if (st != HAL_OK) {
        hal_console_puts("[HAL] IRQ init failed\n");
        return st;
    }
    hal_console_printf("[HAL] GIC initialized, max IRQ: %u\n", hal_irq_max());

    /* 4. Timer */
    st = hal_timer_init();
    if (st != HAL_OK) {
        hal_console_puts("[HAL] Timer init failed\n");
        return st;
    }
    hal_console_printf("[HAL] Timer initialized, freq: %u Hz\n",
                       (uint32_t)hal_timer_freq_hz());

    /* Register virtual timer IRQ handler */
    hal_irq_register(AARCH64_VTIMER_IRQ, aarch64_timer_irq_handler, 0);

    /* 5. MMU: set up page tables and enable */
    st = hal_mmu_init();
    if (st != HAL_OK) {
        hal_console_puts("[HAL] MMU init failed\n");
        return st;
    }
    hal_console_printf("[HAL] MMU enabled, RAM: %u MB\n",
                       (uint32_t)(hal_mmu_total_ram() / (1024 * 1024)));

    /* 6. Bus enumeration */
    st = hal_bus_init();
    if (st != HAL_OK) {
        hal_console_puts("[HAL] Bus init failed\n");
        /* Non-fatal: continue without bus enumeration */
    } else {
        hal_device_t devs[16];
        uint32_t n = hal_bus_scan(devs, 16);
        hal_console_printf("[HAL] Bus: %u devices found\n", n);
    }

    /* 7. SMP: discover and optionally start secondary cores */
    st = hal_smp_init();
    if (st != HAL_OK) {
        hal_console_puts("[HAL] SMP init failed (single-core mode)\n");
        /* Non-fatal: continue in single-core mode */
    } else {
        hal_console_printf("[HAL] SMP: %u cores detected\n", hal_smp_core_count());
    }

    /* 8. Enable interrupts */
    hal_cpu_enable_interrupts();
    hal_console_puts("[HAL] Interrupts enabled\n");

    hal_console_puts("[HAL] AArch64 initialization complete\n\n");

    return HAL_OK;
}
