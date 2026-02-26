/* SPDX-License-Identifier: MIT */
/* AlJefra OS — HAL CPU Interface
 * Architecture-independent CPU operations.
 */

#ifndef ALJEFRA_HAL_CPU_H
#define ALJEFRA_HAL_CPU_H

#include <stdint.h>

/* CPU feature flags (arch-independent superset) */
#define HAL_CPU_FEAT_FPU        (1u << 0)
#define HAL_CPU_FEAT_SSE        (1u << 1)
#define HAL_CPU_FEAT_AVX        (1u << 2)
#define HAL_CPU_FEAT_NEON       (1u << 3)   /* ARM SIMD */
#define HAL_CPU_FEAT_SVE        (1u << 4)   /* ARM SVE */
#define HAL_CPU_FEAT_RVVEC      (1u << 5)   /* RISC-V vector */
#define HAL_CPU_FEAT_RDRAND     (1u << 6)
#define HAL_CPU_FEAT_CRC32      (1u << 7)
#define HAL_CPU_FEAT_AES        (1u << 8)
#define HAL_CPU_FEAT_ATOMICS    (1u << 9)

/* CPU info structure */
typedef struct {
    char     vendor[16];        /* "GenuineIntel", "ARM", "RISC-V" */
    char     model[48];         /* Model name string */
    uint32_t features;          /* HAL_CPU_FEAT_* bitmask */
    uint32_t cores_physical;    /* Physical core count */
    uint32_t cores_logical;     /* Logical (HT) core count */
    uint32_t cache_line_bytes;  /* L1 cache line size */
} hal_cpu_info_t;

/* Initialize BSP (boot processor).  Must be called first. */
hal_status_t hal_cpu_init(void);

/* Query CPU information */
void hal_cpu_get_info(hal_cpu_info_t *info);

/* Get current core ID (0-based) */
uint64_t hal_cpu_id(void);

/* Total number of online cores */
uint32_t hal_cpu_count(void);

/* Halt / low-power wait until next interrupt */
void hal_cpu_halt(void);

/* Enable/disable interrupts on current core */
void hal_cpu_enable_interrupts(void);
void hal_cpu_disable_interrupts(void);

/* Full memory barrier */
void hal_cpu_memory_barrier(void);

/* Read cycle counter (TSC / CNTVCT / rdcycle) */
uint64_t hal_cpu_cycles(void);

/* Hardware random number (0 on failure / not supported) */
uint64_t hal_cpu_random(void);

#endif /* ALJEFRA_HAL_CPU_H */
