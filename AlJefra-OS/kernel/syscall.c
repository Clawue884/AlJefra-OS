/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Syscall Dispatch
 *
 * Architecture-independent syscall router.
 * The arch-specific trap/interrupt handler extracts arguments and calls
 * syscall_dispatch().  On x86-64, this is via the existing call-table
 * mechanism at 0x100010+.
 */

#include "syscall.h"
#include "sched.h"
#include "driver_loader.h"
#include "../hal/hal.h"

/* ── Syscall handler table ── */
typedef uint64_t (*syscall_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t);
static syscall_fn_t g_syscall_table[SYS_MAX];

/* ── Individual handlers ── */

static uint64_t sys_input(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4)
{
    (void)a1; (void)a2; (void)a3; (void)a4;
    return (uint64_t)hal_console_getc();
}

static uint64_t sys_output(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4)
{
    (void)a3; (void)a4;
    hal_console_write((const char *)a1, a2);
    return 0;
}

static uint64_t sys_system(uint64_t func, uint64_t var1, uint64_t var2, uint64_t a4)
{
    (void)a4;

    switch (func) {
    case 0x00: /* TIMECOUNTER */
        return hal_timer_ns();
    case 0x01: /* FREE_MEMORY */
        return hal_mmu_free_ram();
    case 0x10: /* SMP_ID */
        return hal_smp_core_id();
    case 0x11: /* SMP_NUMCORES */
        return hal_smp_core_count();
    case 0x72: /* DELAY */
        hal_timer_delay_us(var1);
        return 0;
    case 0x7F: /* SHUTDOWN */
        hal_console_puts("[kernel] Shutdown requested\n");
        hal_cpu_halt();
        return 0;
    default:
        return 0;
    }
}

static uint64_t sys_sched_yield(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4)
{
    (void)a1; (void)a2; (void)a3; (void)a4;
    sched_yield();
    return 0;
}

static uint64_t sys_sched_create(uint64_t entry, uint64_t arg, uint64_t stack_size, uint64_t a4)
{
    (void)a4;
    return (uint64_t)sched_create((void (*)(void *))entry, (void *)arg, stack_size);
}

/* ── Init ── */

void syscall_init(void)
{
    for (int i = 0; i < SYS_MAX; i++)
        g_syscall_table[i] = NULL;

    g_syscall_table[SYS_INPUT]       = sys_input;
    g_syscall_table[SYS_OUTPUT]      = sys_output;
    g_syscall_table[SYS_SYSTEM]      = sys_system;
    g_syscall_table[SYS_SCHED_YIELD] = sys_sched_yield;
    g_syscall_table[SYS_SCHED_CREATE]= sys_sched_create;

    /* Network and storage syscalls are registered by their drivers */
}

/* ── Dispatch ── */

uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2,
                          uint64_t a3, uint64_t a4)
{
    if (num >= SYS_MAX || g_syscall_table[num] == NULL) {
        hal_console_printf("[syscall] Unknown syscall %u\n", (uint32_t)num);
        return (uint64_t)-1;
    }
    return g_syscall_table[num](a1, a2, a3, a4);
}

/* ── Main loop ── */

void syscall_loop(void)
{
    /* In the exokernel model, we return to the application.
     * The kernel runs in response to syscalls and interrupts.
     * This loop just halts until something happens. */
    for (;;) {
        hal_cpu_halt();  /* Wait for interrupt */
        sched_tick();    /* Run scheduler on wakeup */
    }
}
