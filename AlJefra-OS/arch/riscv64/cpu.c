/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- RISC-V 64-bit CPU HAL Implementation
 * Implements hal/cpu.h for RV64GC (IMAFDC).
 *
 * Key CSRs:
 *   misa       - ISA and extensions (M-mode)
 *   mhartid    - Hardware Thread ID (M-mode)
 *   sstatus    - Supervisor status (SIE, SPIE, SPP, FS, VS)
 *   satp       - Supervisor Address Translation and Protection
 *   cycle      - Cycle counter (read via rdcycle)
 *   time       - Wall-clock time (read via rdtime)
 *
 * Note: On most RISC-V platforms, the kernel runs in S-mode.
 * M-mode CSRs (misa, mhartid) may not be directly accessible.
 * We use S-mode CSRs where available and SBI for M-mode queries.
 */

#include "../../hal/hal.h"

/* ------------------------------------------------------------------ */
/* Private state                                                       */
/* ------------------------------------------------------------------ */

static hal_cpu_info_t cpu_info;
static uint32_t       online_cores = 1;

/* ------------------------------------------------------------------ */
/* CSR access helpers                                                  */
/* ------------------------------------------------------------------ */

/* Read sstatus (supervisor status) */
static inline uint64_t read_sstatus(void)
{
    uint64_t v;
    __asm__ volatile("csrr %0, sstatus" : "=r"(v));
    return v;
}

/* Write sstatus */
static inline void write_sstatus(uint64_t v)
{
    __asm__ volatile("csrw sstatus, %0" : : "r"(v));
}

/* Read cycle counter */
static inline uint64_t read_cycle(void)
{
    uint64_t v;
    __asm__ volatile("rdcycle %0" : "=r"(v));
    return v;
}

/* Read time counter */
static inline uint64_t read_time(void)
{
    uint64_t v;
    __asm__ volatile("rdtime %0" : "=r"(v));
    return v;
}

/* Read mhartid via SBI or trap (may not be directly accessible in S-mode).
 * On many platforms, tp register holds hartid set by bootloader. */
static inline uint64_t read_hartid(void)
{
    uint64_t v;
    __asm__ volatile("mv %0, tp" : "=r"(v));
    return v;
}

/* ------------------------------------------------------------------ */
/* SBI (Supervisor Binary Interface) call                              */
/* ------------------------------------------------------------------ */

/* SBI v0.2+ call convention:
 *   a7 = extension ID (EID)
 *   a6 = function ID (FID)
 *   a0-a5 = arguments
 *   Returns: a0 = error code, a1 = value */

typedef struct {
    int64_t  error;
    uint64_t value;
} sbi_ret_t;

static sbi_ret_t sbi_call(uint64_t eid, uint64_t fid,
                            uint64_t a0, uint64_t a1, uint64_t a2)
{
    register uint64_t r_a0 __asm__("a0") = a0;
    register uint64_t r_a1 __asm__("a1") = a1;
    register uint64_t r_a2 __asm__("a2") = a2;
    register uint64_t r_a6 __asm__("a6") = fid;
    register uint64_t r_a7 __asm__("a7") = eid;

    __asm__ volatile("ecall"
        : "+r"(r_a0), "+r"(r_a1)
        : "r"(r_a2), "r"(r_a6), "r"(r_a7)
        : "memory");

    sbi_ret_t ret;
    ret.error = (int64_t)r_a0;
    ret.value = r_a1;
    return ret;
}

/* SBI Extension IDs */
#define SBI_EXT_BASE        0x10
#define SBI_EXT_TIMER       0x54494D45  /* "TIME" */
#define SBI_EXT_HSM         0x48534D    /* "HSM" */
#define SBI_EXT_SRST        0x53525354  /* "SRST" */

/* SBI Base extension functions */
#define SBI_BASE_GET_SPEC_VERSION   0
#define SBI_BASE_GET_IMPL_ID        1
#define SBI_BASE_GET_IMPL_VERSION   2
#define SBI_BASE_PROBE_EXTENSION    3
#define SBI_BASE_GET_MVENDORID      4
#define SBI_BASE_GET_MARCHID        5
#define SBI_BASE_GET_MIMPID         6

/* ------------------------------------------------------------------ */
/* Feature detection via SBI + marchid                                 */
/* ------------------------------------------------------------------ */

static uint32_t detect_features(void)
{
    uint32_t feat = 0;

    /* Read sstatus.FS to check if FPU is available.
     * FS [14:13]: 0=Off, 1=Initial, 2=Clean, 3=Dirty */
    uint64_t sstatus = read_sstatus();
    uint32_t fs_field = (sstatus >> 13) & 0x3;
    if (fs_field != 0)
        feat |= HAL_CPU_FEAT_FPU;

    /* Check for Vector extension via sstatus.VS [10:9] */
    uint32_t vs_field = (sstatus >> 9) & 0x3;
    if (vs_field != 0)
        feat |= HAL_CPU_FEAT_RVVEC;

    /* Probe SBI for extensions -- if available, we can infer atomics
     * (A extension is mandatory in RV64G) */
    feat |= HAL_CPU_FEAT_ATOMICS;  /* RV64G always has A extension */

    /* TODO: Parse misa CSR if accessible from S-mode (implementation-defined).
     * Could detect M, A, F, D, C, V extensions individually. */

    return feat;
}

/* ------------------------------------------------------------------ */
/* Decode vendor information via SBI                                   */
/* ------------------------------------------------------------------ */

static void detect_vendor(void)
{
    sbi_ret_t ret;

    /* Get mvendorid */
    ret = sbi_call(SBI_EXT_BASE, SBI_BASE_GET_MVENDORID, 0, 0, 0);
    uint64_t vendorid = (ret.error == 0) ? ret.value : 0;

    /* Get marchid */
    ret = sbi_call(SBI_EXT_BASE, SBI_BASE_GET_MARCHID, 0, 0, 0);
    uint64_t archid = (ret.error == 0) ? ret.value : 0;

    /* Vendor string */
    switch (vendorid) {
    case 0x489:
        cpu_info.vendor[0] = 'S'; cpu_info.vendor[1] = 'i';
        cpu_info.vendor[2] = 'F'; cpu_info.vendor[3] = 'i';
        cpu_info.vendor[4] = 'v'; cpu_info.vendor[5] = 'e';
        cpu_info.vendor[6] = '\0';
        break;
    case 0x5B7:
        cpu_info.vendor[0] = 'T'; cpu_info.vendor[1] = '-';
        cpu_info.vendor[2] = 'H'; cpu_info.vendor[3] = 'e';
        cpu_info.vendor[4] = 'a'; cpu_info.vendor[5] = 'd';
        cpu_info.vendor[6] = '\0';
        break;
    default:
        cpu_info.vendor[0] = 'R'; cpu_info.vendor[1] = 'I';
        cpu_info.vendor[2] = 'S'; cpu_info.vendor[3] = 'C';
        cpu_info.vendor[4] = '-'; cpu_info.vendor[5] = 'V';
        cpu_info.vendor[6] = '\0';
        break;
    }

    /* Model string: "RV64 arch:XXXX" */
    const char *prefix = "RV64GC arch:";
    int i = 0;
    while (prefix[i] && i < 40) { cpu_info.model[i] = prefix[i]; i++; }

    /* Append archid as hex */
    const char hex[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4) {
        int nibble = (archid >> shift) & 0xF;
        if (nibble || i > 12) {  /* Skip leading zeros after prefix */
            cpu_info.model[i++] = hex[nibble];
        }
    }
    if (i == 12) cpu_info.model[i++] = '0';  /* archid was 0 */
    cpu_info.model[i] = '\0';
}

/* ------------------------------------------------------------------ */
/* HAL Interface Implementation                                        */
/* ------------------------------------------------------------------ */

hal_status_t hal_cpu_init(void)
{
    /* Detect vendor and model via SBI */
    detect_vendor();

    /* Enable FPU: set sstatus.FS = 01 (Initial) */
    uint64_t sstatus = read_sstatus();
    sstatus |= (1ULL << 13);  /* FS = 01 */
    write_sstatus(sstatus);

    /* Detect features */
    cpu_info.features = detect_features();

    /* Cache line size: RISC-V doesn't have a standard CSR for this.
     * Use 64 bytes as a common default. */
    cpu_info.cache_line_bytes = 64;

    /* Default single core */
    cpu_info.cores_physical = 1;
    cpu_info.cores_logical  = 1;

    return HAL_OK;
}

void hal_cpu_get_info(hal_cpu_info_t *info)
{
    if (info) {
        const char *src = (const char *)&cpu_info;
        char *dst = (char *)info;
        for (unsigned i = 0; i < sizeof(hal_cpu_info_t); i++)
            dst[i] = src[i];
    }
}

uint64_t hal_cpu_id(void)
{
    return read_hartid();
}

uint32_t hal_cpu_count(void)
{
    return online_cores;
}

void hal_cpu_halt(void)
{
    __asm__ volatile("wfi");
}

void hal_cpu_enable_interrupts(void)
{
    /* Set sstatus.SIE (bit 1) */
    __asm__ volatile("csrsi sstatus, 0x2");
}

void hal_cpu_disable_interrupts(void)
{
    /* Clear sstatus.SIE (bit 1) */
    __asm__ volatile("csrci sstatus, 0x2");
}

void hal_cpu_memory_barrier(void)
{
    /* FENCE: orders all preceding memory operations before all following */
    __asm__ volatile("fence iorw, iorw" ::: "memory");
}

uint64_t hal_cpu_cycles(void)
{
    return read_cycle();
}

uint64_t hal_cpu_random(void)
{
    /* RISC-V Scalar Crypto extension (Zkr): seed CSR.
     * Most platforms don't have this yet. Return 0. */
    /* TODO: If Zkr is detected, use: csrr val, seed */
    return 0;
}
