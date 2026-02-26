/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- RISC-V 64-bit Timer Implementation
 * Implements hal/timer.h using the RISC-V timer (rdtime).
 *
 * The RISC-V timer is memory-mapped via CLINT (Core-Local Interruptor):
 *   MTIME:    0x0200BFF8 (64-bit counter, increments at timebase-frequency)
 *   MTIMECMP: 0x02004000 + 8*hartid (comparator, fires STIP when MTIME >= MTIMECMP)
 *
 * In S-mode, timer comparisons are managed via SBI timer extension:
 *   SBI_EXT_TIMER (0x54494D45) function 0: sbi_set_timer(stime_value)
 *
 * The timebase-frequency is obtained from the Device Tree (typically 10MHz
 * for QEMU virt, 1MHz for some boards).
 */

#include "../../hal/hal.h"

/* ------------------------------------------------------------------ */
/* SBI Timer extension                                                 */
/* ------------------------------------------------------------------ */

#define SBI_EXT_TIMER   0x54494D45  /* "TIME" */
#define SBI_SET_TIMER   0

/* SBI call (duplicated from cpu.c for self-containment) */
static void sbi_set_timer(uint64_t stime_value)
{
    register uint64_t a0 __asm__("a0") = stime_value;
    register uint64_t a6 __asm__("a6") = SBI_SET_TIMER;
    register uint64_t a7 __asm__("a7") = SBI_EXT_TIMER;

    __asm__ volatile("ecall"
        : "+r"(a0)
        : "r"(a6), "r"(a7)
        : "a1", "memory");
}

/* ------------------------------------------------------------------ */
/* Private state                                                       */
/* ------------------------------------------------------------------ */

static uint64_t timer_freq = 10000000ULL;  /* Default 10MHz (QEMU virt) */
static hal_timer_callback_t armed_callback;
static void *armed_ctx;

/* ------------------------------------------------------------------ */
/* Counter access                                                      */
/* ------------------------------------------------------------------ */

static inline uint64_t read_time(void)
{
    uint64_t v;
    __asm__ volatile("rdtime %0" : "=r"(v));
    return v;
}

/* ------------------------------------------------------------------ */
/* HAL Interface Implementation                                        */
/* ------------------------------------------------------------------ */

hal_status_t hal_timer_init(void)
{
    /* TODO: Parse timebase-frequency from DTB.
     * The DTB is typically passed by firmware in a0.
     * For now, use QEMU virt default: 10 MHz. */
    timer_freq = 10000000ULL;

    armed_callback = 0;
    armed_ctx      = 0;

    /* Disable timer interrupt by setting comparator to max */
    sbi_set_timer(0xFFFFFFFFFFFFFFFFULL);

    return HAL_OK;
}

uint64_t hal_timer_ns(void)
{
    uint64_t t = read_time();

    /* ns = t * 1e9 / freq, split to avoid overflow */
    uint64_t sec  = t / timer_freq;
    uint64_t frac = t % timer_freq;
    return sec * 1000000000ULL + (frac * 1000000000ULL) / timer_freq;
}

void hal_timer_delay_us(uint64_t us)
{
    /* ticks = us * freq / 1e6 */
    uint64_t ticks = (us * timer_freq) / 1000000ULL;
    uint64_t start = read_time();

    while ((read_time() - start) < ticks) {
        /* Spin -- no yield instruction on RISC-V base ISA,
         * but we can use a NOP or the WFI hint if interrupts are enabled. */
        __asm__ volatile("nop");
    }
}

hal_status_t hal_timer_arm(uint64_t ns, hal_timer_callback_t cb, void *ctx)
{
    if (ns == 0) {
        /* Disarm */
        sbi_set_timer(0xFFFFFFFFFFFFFFFFULL);
        armed_callback = 0;
        armed_ctx      = 0;
        return HAL_OK;
    }

    armed_callback = cb;
    armed_ctx      = ctx;

    /* Convert ns to timer ticks */
    uint64_t ticks = (ns * timer_freq) / 1000000000ULL;
    uint64_t target = read_time() + ticks;

    /* Set timer comparator via SBI */
    sbi_set_timer(target);

    return HAL_OK;
}

uint64_t hal_timer_freq_hz(void)
{
    return timer_freq;
}

/* ------------------------------------------------------------------ */
/* Timer IRQ handler (called from interrupt.c trap dispatch)           */
/* ------------------------------------------------------------------ */

void riscv64_timer_irq_handler(uint32_t irq, void *ctx)
{
    (void)irq;
    (void)ctx;

    /* Disable timer to acknowledge (set comparator to max) */
    sbi_set_timer(0xFFFFFFFFFFFFFFFFULL);

    if (armed_callback) {
        hal_timer_callback_t cb = armed_callback;
        void *arg = armed_ctx;
        armed_callback = 0;
        armed_ctx      = 0;
        cb(arg);
    }
}
