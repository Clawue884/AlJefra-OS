/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- RISC-V 64-bit Master HAL Initialization
 * Implements hal_init() for RISC-V, calling all subsystem init functions
 * in the correct order.
 *
 * On RISC-V (and AArch64), boot.S jumps directly to hal_init() after
 * setting up the stack and zeroing BSS — there is no separate start.c
 * shim (unlike x86-64 which needs one for Multiboot1 entry).
 *
 * Init order: Console → CPU → IRQ → Timer → MMU → Bus → SMP
 * (IRQ before MMU, unlike x86-64 which does MMU before IRQ because
 * the x86-64 identity map is already set up by Pure64/bootloader.)
 */

#include "../../hal/hal.h"

/* Forward declaration of the timer IRQ registration */
extern void riscv64_timer_irq_handler(uint32_t irq, void *ctx);

/* Kernel main entry point (defined by the kernel) */
extern void kernel_main(void);

/* DTB pointer saved by boot.S (global so other modules can access it) */
uint64_t riscv64_dtb_addr = 0;

/* Timer pseudo-IRQ handler -- registered directly in irq_table[0] */
extern void riscv64_timer_register_direct(hal_irq_handler_t handler, void *ctx);

/* ------------------------------------------------------------------ */
/* hal_init() -- master initialization sequence                        */
/* ------------------------------------------------------------------ */

hal_status_t hal_init(void)
{
    hal_status_t st;

    /* 1. Console first -- enables debug output ASAP.
     * On RISC-V, even before NS16550 init, we can use SBI putchar. */
    st = hal_console_init();
    if (st != HAL_OK) {
        /* SBI putchar should always work as fallback */
    }

    hal_console_puts("AlJefra OS -- RISC-V 64-bit HAL Init\n");
    hal_console_printf("[HAL] DTB at 0x%x\n", (unsigned)(riscv64_dtb_addr & 0xFFFFFFFF));

    /* 2. CPU: detect ISA extensions, enable FPU */
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
        hal_console_printf("[HAL] Boot hartid: %u\n", (uint32_t)hal_cpu_id());
    }

    /* 3. Interrupt controller (PLIC) */
    st = hal_irq_init();
    if (st != HAL_OK) {
        hal_console_puts("[HAL] IRQ (PLIC) init failed\n");
        return st;
    }
    hal_console_printf("[HAL] PLIC initialized, max IRQ: %u\n", hal_irq_max());

    /* 4. Timer */
    st = hal_timer_init();
    if (st != HAL_OK) {
        hal_console_puts("[HAL] Timer init failed\n");
        return st;
    }
    hal_console_printf("[HAL] Timer initialized, freq: %u Hz\n",
                       (uint32_t)hal_timer_freq_hz());

    /* Register timer interrupt handler directly into irq_table[0].
     * Note: The supervisor timer interrupt (STIP) is not routed through PLIC.
     * It is handled directly in riscv64_trap_dispatch() (interrupt.c) when
     * scause indicates a supervisor timer interrupt (code 5). The timer
     * handler is registered on pseudo-IRQ 0 in the irq_table by a direct
     * registration function (hal_irq_register rejects IRQ 0). */
    riscv64_timer_register_direct(riscv64_timer_irq_handler, 0);

    /* 5. MMU: set up Sv39 page tables */
    st = hal_mmu_init();
    if (st != HAL_OK) {
        hal_console_puts("[HAL] MMU init failed\n");
        return st;
    }
    hal_console_printf("[HAL] MMU (Sv39) enabled, RAM: %u MB\n",
                       (uint32_t)(hal_mmu_total_ram() / (1024 * 1024)));

    /* 6. Bus enumeration */
    st = hal_bus_init();
    if (st != HAL_OK) {
        hal_console_puts("[HAL] Bus init failed\n");
    } else {
        hal_device_t devs[16];
        uint32_t n = hal_bus_scan(devs, 16);
        hal_console_printf("[HAL] Bus: %u devices found\n", n);
    }

    /* 7. SMP: discover harts via SBI HSM */
    st = hal_smp_init();
    if (st != HAL_OK) {
        hal_console_puts("[HAL] SMP init failed (single-hart mode)\n");
    } else {
        hal_console_printf("[HAL] SMP: %u harts detected\n", hal_smp_core_count());
    }

    /* 8. Enable interrupts */
    hal_cpu_enable_interrupts();
    hal_console_puts("[HAL] Interrupts enabled\n");

    hal_console_puts("[HAL] RISC-V 64-bit initialization complete\n\n");

    return HAL_OK;
}
