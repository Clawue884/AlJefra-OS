/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Hardware Abstraction Layer
 * Master include for all HAL interfaces.
 * Architecture-independent C API consumed by kernel, drivers, and apps.
 */

#ifndef ALJEFRA_HAL_H
#define ALJEFRA_HAL_H

#include <stdint.h>
#include <stddef.h>

/* Boolean for bare-metal (no stdbool.h) */
#ifndef __cplusplus
#ifndef bool
typedef _Bool bool;
#define true  1
#define false 0
#endif
#endif

/* Common status codes */
typedef enum {
    HAL_OK          = 0,
    HAL_ERROR       = -1,
    HAL_TIMEOUT     = -2,
    HAL_BUSY        = -3,
    HAL_NO_DEVICE   = -4,
    HAL_NO_MEMORY   = -5,
    HAL_NOT_SUPPORTED = -6,
} hal_status_t;

/* Architecture identifier */
typedef enum {
    HAL_ARCH_X86_64  = 0,
    HAL_ARCH_AARCH64 = 1,
    HAL_ARCH_RISCV64 = 2,
} hal_arch_t;

/* Return current architecture (compile-time constant) */
static inline hal_arch_t hal_arch(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    return HAL_ARCH_X86_64;
#elif defined(__aarch64__)
    return HAL_ARCH_AARCH64;
#elif defined(__riscv) && (__riscv_xlen == 64)
    return HAL_ARCH_RISCV64;
#else
#error "Unsupported architecture"
#endif
}

/* Master HAL init — called once at boot, dispatches to arch-specific init */
hal_status_t hal_init(void);

/* Sub-module includes */
#include "cpu.h"
#include "interrupt.h"
#include "timer.h"
#include "bus.h"
#include "io.h"
#include "mmu.h"
#include "smp.h"
#include "console.h"

#endif /* ALJEFRA_HAL_H */
