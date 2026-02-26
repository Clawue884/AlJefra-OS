/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- AArch64 CPU HAL Implementation
 * Implements hal/cpu.h for ARMv8-A (AArch64).
 *
 * Key system registers:
 *   MIDR_EL1         - Main ID Register (implementer, variant, part)
 *   MPIDR_EL1        - Multiprocessor Affinity Register
 *   CPACR_EL1        - Coprocessor Access Control (FPU/NEON enable)
 *   ID_AA64ISAR0_EL1 - Instruction Set Attribute Register 0 (AES, CRC32, ...)
 *   ID_AA64PFR0_EL1  - Processor Feature Register 0 (FP, AdvSIMD, SVE, ...)
 *   CNTVCT_EL0       - Virtual Counter (cycle counter)
 *   DAIF             - Interrupt mask bits (D, A, I, F)
 *   RNDR             - Random Number (ARMv8.5-RNG)
 */

#include "../../hal/hal.h"

/* ------------------------------------------------------------------ */
/* Private state                                                       */
/* ------------------------------------------------------------------ */

static hal_cpu_info_t cpu_info;
static uint32_t       online_cores = 1;  /* BSP always online */

/* ------------------------------------------------------------------ */
/* AArch64 system register helpers                                     */
/* ------------------------------------------------------------------ */

static inline uint64_t read_midr_el1(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, MIDR_EL1" : "=r"(v));
    return v;
}

static inline uint64_t read_mpidr_el1(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, MPIDR_EL1" : "=r"(v));
    return v;
}

static inline uint64_t read_id_aa64isar0_el1(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, ID_AA64ISAR0_EL1" : "=r"(v));
    return v;
}

static inline uint64_t read_id_aa64pfr0_el1(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, ID_AA64PFR0_EL1" : "=r"(v));
    return v;
}

static inline uint64_t read_cntvct_el0(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, CNTVCT_EL0" : "=r"(v));
    return v;
}

static inline uint64_t read_cpacr_el1(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, CPACR_EL1" : "=r"(v));
    return v;
}

static inline void write_cpacr_el1(uint64_t v)
{
    __asm__ volatile("msr CPACR_EL1, %0" : : "r"(v));
    __asm__ volatile("isb");
}

/* ------------------------------------------------------------------ */
/* Helper: decode MIDR_EL1 into vendor / model strings                 */
/* ------------------------------------------------------------------ */

static void decode_midr(uint64_t midr)
{
    uint8_t implementer = (midr >> 24) & 0xFF;
    uint16_t part       = (midr >> 4)  & 0xFFF;
    uint8_t variant     = (midr >> 20) & 0xF;
    uint8_t revision    = midr & 0xF;

    /* Vendor */
    switch (implementer) {
    case 0x41: /* 'A' */
        cpu_info.vendor[0] = 'A'; cpu_info.vendor[1] = 'R';
        cpu_info.vendor[2] = 'M'; cpu_info.vendor[3] = '\0';
        break;
    case 0x42:
        cpu_info.vendor[0] = 'B'; cpu_info.vendor[1] = 'r';
        cpu_info.vendor[2] = 'o'; cpu_info.vendor[3] = 'a';
        cpu_info.vendor[4] = 'd'; cpu_info.vendor[5] = 'c';
        cpu_info.vendor[6] = 'o'; cpu_info.vendor[7] = 'm';
        cpu_info.vendor[8] = '\0';
        break;
    case 0x51:
        cpu_info.vendor[0] = 'Q'; cpu_info.vendor[1] = 'u';
        cpu_info.vendor[2] = 'a'; cpu_info.vendor[3] = 'l';
        cpu_info.vendor[4] = 'c'; cpu_info.vendor[5] = 'o';
        cpu_info.vendor[6] = 'm'; cpu_info.vendor[7] = 'm';
        cpu_info.vendor[8] = '\0';
        break;
    case 0x61:
        cpu_info.vendor[0] = 'A'; cpu_info.vendor[1] = 'p';
        cpu_info.vendor[2] = 'p'; cpu_info.vendor[3] = 'l';
        cpu_info.vendor[4] = 'e'; cpu_info.vendor[5] = '\0';
        break;
    default:
        cpu_info.vendor[0] = 'U'; cpu_info.vendor[1] = 'n';
        cpu_info.vendor[2] = 'k'; cpu_info.vendor[3] = '\0';
        break;
    }

    /* Model string: "Cortex-A<part> r<variant>p<revision>" (simplified) */
    const char *part_name = "Unknown";
    switch (part) {
    case 0xD03: part_name = "Cortex-A53"; break;
    case 0xD04: part_name = "Cortex-A35"; break;
    case 0xD05: part_name = "Cortex-A55"; break;
    case 0xD07: part_name = "Cortex-A57"; break;
    case 0xD08: part_name = "Cortex-A72"; break;
    case 0xD09: part_name = "Cortex-A73"; break;
    case 0xD0A: part_name = "Cortex-A75"; break;
    case 0xD0B: part_name = "Cortex-A76"; break;
    case 0xD0D: part_name = "Cortex-A77"; break;
    case 0xD40: part_name = "Neoverse-N1"; break;
    case 0xD49: part_name = "Neoverse-N2"; break;
    case 0xD4F: part_name = "Neoverse-V2"; break;
    }

    /* Manually copy part name + revision into model[] */
    int i = 0;
    for (const char *p = part_name; *p && i < 40; p++, i++)
        cpu_info.model[i] = *p;
    cpu_info.model[i++] = ' ';
    cpu_info.model[i++] = 'r';
    cpu_info.model[i++] = '0' + variant;
    cpu_info.model[i++] = 'p';
    cpu_info.model[i++] = '0' + revision;
    cpu_info.model[i]   = '\0';
}

/* ------------------------------------------------------------------ */
/* Feature detection                                                   */
/* ------------------------------------------------------------------ */

static uint32_t detect_features(void)
{
    uint32_t feat = 0;
    uint64_t isar0 = read_id_aa64isar0_el1();
    uint64_t pfr0  = read_id_aa64pfr0_el1();

    /* ID_AA64PFR0_EL1: FP field [19:16], AdvSIMD field [23:20], SVE [35:32] */
    uint8_t fp_field   = (pfr0 >> 16) & 0xF;
    uint8_t simd_field = (pfr0 >> 20) & 0xF;
    uint8_t sve_field  = (pfr0 >> 32) & 0xF;

    /* FP == 0 means FP implemented, 0xF means not implemented */
    if (fp_field != 0xF)
        feat |= HAL_CPU_FEAT_FPU;

    /* AdvSIMD == 0 means NEON implemented */
    if (simd_field != 0xF)
        feat |= HAL_CPU_FEAT_NEON;

    /* SVE field: 1 = SVE implemented */
    if (sve_field >= 1)
        feat |= HAL_CPU_FEAT_SVE;

    /* ID_AA64ISAR0_EL1: AES [7:4], CRC32 [19:16], RNDR [63:60] */
    uint8_t aes_field  = (isar0 >> 4)  & 0xF;
    uint8_t crc_field  = (isar0 >> 16) & 0xF;
    uint8_t rndr_field = (isar0 >> 60) & 0xF;

    if (aes_field >= 1)
        feat |= HAL_CPU_FEAT_AES;

    if (crc_field >= 1)
        feat |= HAL_CPU_FEAT_CRC32;

    if (rndr_field >= 1)
        feat |= HAL_CPU_FEAT_RDRAND;  /* RNDR = ARM's equivalent of RDRAND */

    /* Atomics: ID_AA64ISAR0_EL1 Atomic field [23:20] >= 2 means LSE atomics */
    uint8_t atomic_field = (isar0 >> 20) & 0xF;
    if (atomic_field >= 2)
        feat |= HAL_CPU_FEAT_ATOMICS;

    return feat;
}

/* ------------------------------------------------------------------ */
/* HAL Interface Implementation                                        */
/* ------------------------------------------------------------------ */

hal_status_t hal_cpu_init(void)
{
    uint64_t midr = read_midr_el1();

    /* Decode processor identification */
    decode_midr(midr);

    /* Enable FPU / NEON / SVE access at EL1:
     * CPACR_EL1.FPEN [21:20] = 0b11 (no trapping of FP/NEON at EL0/EL1)
     * CPACR_EL1.ZEN  [17:16] = 0b11 (no trapping of SVE at EL0/EL1, if SVE) */
    uint64_t cpacr = read_cpacr_el1();
    cpacr |= (3ULL << 20);  /* FPEN = 0b11 */
    cpacr |= (3ULL << 16);  /* ZEN  = 0b11 (harmless if SVE not present) */
    write_cpacr_el1(cpacr);

    /* Detect features */
    cpu_info.features = detect_features();

    /* Cache line size: read CTR_EL0 DminLine [19:16] */
    uint64_t ctr;
    __asm__ volatile("mrs %0, CTR_EL0" : "=r"(ctr));
    uint32_t dmin_line = (ctr >> 16) & 0xF;
    cpu_info.cache_line_bytes = 4 << dmin_line;  /* encoded as log2 of words */

    /* Default: single core until SMP init discovers more */
    cpu_info.cores_physical = 1;
    cpu_info.cores_logical  = 1;

    return HAL_OK;
}

void hal_cpu_get_info(hal_cpu_info_t *info)
{
    if (info) {
        /* Manual struct copy (no memcpy in freestanding) */
        const char *src = (const char *)&cpu_info;
        char *dst = (char *)info;
        for (unsigned i = 0; i < sizeof(hal_cpu_info_t); i++)
            dst[i] = src[i];
    }
}

uint64_t hal_cpu_id(void)
{
    uint64_t mpidr = read_mpidr_el1();
    /* Extract Aff0 [7:0] as core ID for typical single-cluster setups.
     * For multi-cluster: combine Aff0 + (Aff1 << 8) + (Aff2 << 16). */
    return mpidr & 0xFF;
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
    /* DAIFClr: clear I and F bits (bits 1 and 0 of the immediate)
     * Immediate encodes: bit 3=D, bit 2=A, bit 1=I, bit 0=F
     * Clear IRQ (I) and FIQ (F): immediate = 0x3 */
    __asm__ volatile("msr DAIFClr, #0x3");
}

void hal_cpu_disable_interrupts(void)
{
    /* DAIFSet: set I and F bits to mask interrupts */
    __asm__ volatile("msr DAIFSet, #0x3");
}

void hal_cpu_memory_barrier(void)
{
    /* DMB ISH: data memory barrier, inner shareable domain */
    __asm__ volatile("dmb ish" ::: "memory");
}

uint64_t hal_cpu_cycles(void)
{
    return read_cntvct_el0();
}

uint64_t hal_cpu_random(void)
{
    /* ARMv8.5-RNG: RNDR system register.
     * If FEAT_RNG is not present, we return 0.
     * RNDR sets NZCV.Z on failure (entropy exhausted), so we check. */
    uint64_t val = 0;

    if (cpu_info.features & HAL_CPU_FEAT_RDRAND) {
        /* The RNDR register is s3_3_c2_c4_0.
         * GCC inline asm uses the numeric encoding since older assemblers
         * may not know the mnemonic. */
        uint64_t tmp;
        uint64_t success;
        __asm__ volatile(
            "mrs %0, s3_3_c2_c4_0\n\t"   /* mrs tmp, RNDR */
            "cset %1, ne\n\t"             /* success = (NZCV.Z == 0) */
            : "=r"(tmp), "=r"(success)
            :
            : "cc"
        );
        if (success)
            val = tmp;
    }

    return val;
}
