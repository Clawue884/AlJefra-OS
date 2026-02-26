/* SPDX-License-Identifier: MIT */
/* AlJefra OS — x86-64 Timer HAL Implementation (Standalone)
 *
 * Uses RDTSC for nanosecond timing. TSC frequency is calibrated
 * against the PIT (Programmable Interval Timer) at init time.
 */

#include "../../hal/hal.h"

/* -------------------------------------------------------------------------- */
/* TSC (Time Stamp Counter)                                                   */
/* -------------------------------------------------------------------------- */

static uint64_t tsc_freq_hz;     /* TSC ticks per second */
static uint64_t tsc_boot;        /* TSC value at boot (init time) */
static bool timer_initialized = false;

static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* PIT ports */
#define PIT_CH2_DATA  0x42
#define PIT_CMD       0x43
#define PIT_PORT_B    0x61

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Calibrate TSC against PIT Channel 2.
 * PIT runs at 1.193182 MHz. We time ~10ms (11932 PIT ticks). */
static uint64_t calibrate_tsc(void)
{
    /* PIT Channel 2, Mode 0 (one-shot), binary */
    outb(PIT_CMD, 0xB0);    /* Ch 2, lobyte/hibyte, mode 0, binary */

    /* Count for ~10ms: 11932 ticks at 1.193182 MHz */
    uint16_t count = 11932;
    outb(PIT_CH2_DATA, (uint8_t)(count & 0xFF));
    outb(PIT_CH2_DATA, (uint8_t)((count >> 8) & 0xFF));

    /* Enable PIT Channel 2 gate */
    uint8_t portb = inb(PIT_PORT_B);
    outb(PIT_PORT_B, (portb & 0xFC) | 0x01);  /* Gate high, speaker off */

    /* Restart: toggle gate */
    outb(PIT_PORT_B, (portb & 0xFC) | 0x00);
    outb(PIT_PORT_B, (portb & 0xFC) | 0x01);

    /* Read TSC before waiting */
    uint64_t tsc_start = rdtsc();

    /* Wait for PIT output to go high (bit 5 of port 0x61) */
    while (!(inb(PIT_PORT_B) & 0x20)) {
        __asm__ volatile ("pause");
    }

    uint64_t tsc_end = rdtsc();
    uint64_t tsc_elapsed = tsc_end - tsc_start;

    /* TSC freq = tsc_elapsed / (count / 1193182) = tsc_elapsed * 1193182 / count */
    return (tsc_elapsed * 1193182ULL) / count;
}

/* -------------------------------------------------------------------------- */
/* Timer callback state                                                       */
/* -------------------------------------------------------------------------- */

static hal_timer_callback_t armed_callback = 0;
static void *armed_ctx = 0;
static uint64_t armed_deadline_ns = 0;

/* -------------------------------------------------------------------------- */
/* HAL Timer API                                                              */
/* -------------------------------------------------------------------------- */

hal_status_t hal_timer_init(void)
{
    tsc_freq_hz = calibrate_tsc();
    tsc_boot = rdtsc();

    /* Sanity check: TSC should be at least 100 MHz */
    if (tsc_freq_hz < 100000000ULL) {
        /* Fallback: assume 1 GHz */
        tsc_freq_hz = 1000000000ULL;
    }

    timer_initialized = true;
    return HAL_OK;
}

uint64_t hal_timer_ns(void)
{
    uint64_t elapsed_ticks = rdtsc() - tsc_boot;
    /* ns = elapsed_ticks * 1,000,000,000 / tsc_freq_hz
     * To avoid overflow, use: (elapsed_ticks / tsc_freq_hz) * 1e9 + remainder
     * But for better precision: split into seconds and fraction */
    uint64_t secs = elapsed_ticks / tsc_freq_hz;
    uint64_t frac = elapsed_ticks % tsc_freq_hz;
    return secs * 1000000000ULL + (frac * 1000000000ULL) / tsc_freq_hz;
}

void hal_timer_delay_us(uint64_t us)
{
    if (us == 0) return;

    uint64_t target_ticks = (us * tsc_freq_hz) / 1000000ULL;
    uint64_t start = rdtsc();

    while ((rdtsc() - start) < target_ticks) {
        __asm__ volatile ("pause");
    }
}

hal_status_t hal_timer_arm(uint64_t ns, hal_timer_callback_t cb, void *ctx)
{
    if (ns == 0) {
        armed_callback = 0;
        armed_ctx = 0;
        armed_deadline_ns = 0;
        return HAL_OK;
    }

    armed_deadline_ns = hal_timer_ns() + ns;
    armed_ctx = ctx;
    armed_callback = cb;
    return HAL_OK;
}

uint64_t hal_timer_freq_hz(void)
{
    return tsc_freq_hz;
}
