/* SPDX-License-Identifier: MIT */
/* AlJefra OS — HAL Timer Interface
 * Architecture-independent timer / clock services.
 *
 * x86-64:  HPET or KVM clock
 * AArch64: Generic Timer (CNTPCT_EL0)
 * RISC-V:  mtime / rdtime CSR
 */

#ifndef ALJEFRA_HAL_TIMER_H
#define ALJEFRA_HAL_TIMER_H

#include <stdint.h>

/* Initialize the platform timer.  Must be called after hal_cpu_init(). */
hal_status_t hal_timer_init(void);

/* Nanoseconds elapsed since boot (monotonic, never wraps in practice) */
uint64_t hal_timer_ns(void);

/* Milliseconds since boot (convenience) */
static inline uint64_t hal_timer_ms(void) { return hal_timer_ns() / 1000000ULL; }

/* Busy-wait for the given number of microseconds */
void hal_timer_delay_us(uint64_t us);

/* Busy-wait for the given number of milliseconds */
static inline void hal_timer_delay_ms(uint64_t ms)
{
    hal_timer_delay_us(ms * 1000);
}

/* Timer callback — called from interrupt context */
typedef void (*hal_timer_callback_t)(void *ctx);

/* Arm a one-shot timer to fire after `ns` nanoseconds.
 * Only one callback may be armed at a time; arming a new one replaces the old.
 * Pass ns=0 to disarm.
 */
hal_status_t hal_timer_arm(uint64_t ns, hal_timer_callback_t cb, void *ctx);

/* Get timer frequency in Hz (for raw counter interpretation) */
uint64_t hal_timer_freq_hz(void);

#endif /* ALJEFRA_HAL_TIMER_H */
