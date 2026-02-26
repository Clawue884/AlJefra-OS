/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- AArch64 Generic Timer Implementation
 * Implements hal/timer.h using the ARM Generic Timer.
 *
 * Key system registers:
 *   CNTFRQ_EL0  - Counter frequency (Hz), set by firmware
 *   CNTVCT_EL0  - Virtual counter value (monotonic)
 *   CNTV_CTL_EL0 - Virtual timer control (ENABLE, IMASK, ISTATUS)
 *   CNTV_CVAL_EL0 - Virtual timer compare value
 *   CNTP_CTL_EL0 - Physical timer control
 *   CNTP_CVAL_EL0 - Physical timer compare value
 */

#include "../../hal/hal.h"

/* ------------------------------------------------------------------ */
/* Private state                                                       */
/* ------------------------------------------------------------------ */

static uint64_t timer_freq_hz;   /* Counter frequency in Hz */
static hal_timer_callback_t armed_callback;
static void *armed_ctx;

/* ------------------------------------------------------------------ */
/* System register accessors                                           */
/* ------------------------------------------------------------------ */

static inline uint64_t read_cntfrq_el0(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(v));
    return v;
}

static inline uint64_t read_cntvct_el0(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, CNTVCT_EL0" : "=r"(v));
    return v;
}

static inline void write_cntv_ctl_el0(uint64_t v)
{
    __asm__ volatile("msr CNTV_CTL_EL0, %0" : : "r"(v));
    __asm__ volatile("isb");
}

static inline uint64_t read_cntv_ctl_el0(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, CNTV_CTL_EL0" : "=r"(v));
    return v;
}

static inline void write_cntv_cval_el0(uint64_t v)
{
    __asm__ volatile("msr CNTV_CVAL_EL0, %0" : : "r"(v));
    __asm__ volatile("isb");
}

/* ------------------------------------------------------------------ */
/* HAL Interface Implementation                                        */
/* ------------------------------------------------------------------ */

hal_status_t hal_timer_init(void)
{
    timer_freq_hz = read_cntfrq_el0();

    /* If firmware didn't set CNTFRQ, use a sane default (62.5 MHz QEMU) */
    if (timer_freq_hz == 0)
        timer_freq_hz = 62500000ULL;

    armed_callback = 0;
    armed_ctx      = 0;

    /* Disable virtual timer interrupt initially */
    write_cntv_ctl_el0(0);

    return HAL_OK;
}

uint64_t hal_timer_ns(void)
{
    uint64_t cnt = read_cntvct_el0();

    /* ns = cnt * 1,000,000,000 / freq
     * To avoid overflow for large cnt values, split the calculation:
     * ns = (cnt / freq) * 1e9 + ((cnt % freq) * 1e9) / freq */
    uint64_t sec  = cnt / timer_freq_hz;
    uint64_t frac = cnt % timer_freq_hz;
    return sec * 1000000000ULL + (frac * 1000000000ULL) / timer_freq_hz;
}

void hal_timer_delay_us(uint64_t us)
{
    /* target_ticks = us * freq / 1,000,000 */
    uint64_t ticks = (us * timer_freq_hz) / 1000000ULL;
    uint64_t start = read_cntvct_el0();

    while ((read_cntvct_el0() - start) < ticks) {
        /* Yield to allow interrupts during wait */
        __asm__ volatile("yield");
    }
}

hal_status_t hal_timer_arm(uint64_t ns, hal_timer_callback_t cb, void *ctx)
{
    if (ns == 0) {
        /* Disarm: disable virtual timer */
        write_cntv_ctl_el0(0);
        armed_callback = 0;
        armed_ctx      = 0;
        return HAL_OK;
    }

    armed_callback = cb;
    armed_ctx      = ctx;

    /* Convert ns to ticks: ticks = ns * freq / 1e9 */
    uint64_t ticks = (ns * timer_freq_hz) / 1000000000ULL;
    uint64_t now   = read_cntvct_el0();

    /* Set compare value to fire after `ticks` */
    write_cntv_cval_el0(now + ticks);

    /* Enable virtual timer, unmask interrupt:
     * Bit 0 = ENABLE, Bit 1 = IMASK (0=unmasked) */
    write_cntv_ctl_el0(1);

    return HAL_OK;
}

uint64_t hal_timer_freq_hz(void)
{
    return timer_freq_hz;
}

/* ------------------------------------------------------------------ */
/* Timer IRQ handler (called from interrupt.c dispatch)                */
/* ------------------------------------------------------------------ */

void aarch64_timer_irq_handler(uint32_t irq, void *ctx)
{
    (void)irq;
    (void)ctx;

    /* Disable timer to acknowledge and prevent re-entry */
    write_cntv_ctl_el0(0);

    if (armed_callback) {
        hal_timer_callback_t cb = armed_callback;
        void *arg = armed_ctx;
        armed_callback = 0;
        armed_ctx      = 0;
        cb(arg);
    }
}
