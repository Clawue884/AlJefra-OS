/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- AArch64 Interrupt Controller (GICv2 / GICv3)
 * Implements hal/interrupt.h for ARM Generic Interrupt Controller.
 *
 * GICv2 MMIO layout (from device tree or hardcoded):
 *   GICD base + 0x0000: Distributor
 *   GICC base + 0x0000: CPU Interface
 *
 * GICv3 uses system register interface (ICC_*_EL1).
 *
 * Standard offsets for QEMU virt machine:
 *   GICD: 0x08000000
 *   GICC: 0x08010000 (GICv2) or redistributor at 0x080A0000 (GICv3)
 */

#include "../../hal/hal.h"

/* ------------------------------------------------------------------ */
/* GIC Register Offsets                                                */
/* ------------------------------------------------------------------ */

/* Distributor (GICD) registers */
#define GICD_CTLR           0x000   /* Distributor Control */
#define GICD_TYPER          0x004   /* Interrupt Controller Type */
#define GICD_IIDR           0x008   /* Distributor Implementer ID */
#define GICD_IGROUPR(n)     (0x080 + 4 * (n))  /* Interrupt Group */
#define GICD_ISENABLER(n)   (0x100 + 4 * (n))  /* Set-Enable */
#define GICD_ICENABLER(n)   (0x180 + 4 * (n))  /* Clear-Enable */
#define GICD_ISPENDR(n)     (0x200 + 4 * (n))  /* Set-Pending */
#define GICD_ICPENDR(n)     (0x280 + 4 * (n))  /* Clear-Pending */
#define GICD_IPRIORITYR(n)  (0x400 + 4 * (n))  /* Priority */
#define GICD_ITARGETSR(n)   (0x800 + 4 * (n))  /* Target (GICv2) */
#define GICD_ICFGR(n)       (0xC00 + 4 * (n))  /* Configuration */
#define GICD_PIDR2          0xFE8   /* Peripheral ID 2 (GIC version) */

/* CPU Interface (GICC) registers -- GICv2 only */
#define GICC_CTLR           0x000   /* CPU Interface Control */
#define GICC_PMR            0x004   /* Priority Mask */
#define GICC_BPR            0x008   /* Binary Point */
#define GICC_IAR            0x00C   /* Interrupt Acknowledge */
#define GICC_EOIR           0x010   /* End of Interrupt */
#define GICC_RPR            0x014   /* Running Priority */

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

#define MAX_IRQS            1024
#define MAX_SPI_IRQS        988     /* IRQs 32..1019 are SPIs */

/* Default QEMU virt machine GIC addresses */
static volatile uint8_t *gicd_base = (volatile uint8_t *)0x08000000ULL;
static volatile uint8_t *gicc_base = (volatile uint8_t *)0x08010000ULL;

/* ------------------------------------------------------------------ */
/* IRQ handler table                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    hal_irq_handler_t       handler;
    void                   *ctx;
} irq_entry_t;

static irq_entry_t         irq_table[MAX_IRQS];
static hal_exception_handler_t exception_handlers[16];  /* EL1 sync/irq/fiq/serror per EL */

static uint32_t            max_irq = 0;
static int                 gic_version = 0;  /* 2 or 3 */

/* ------------------------------------------------------------------ */
/* MMIO helpers                                                        */
/* ------------------------------------------------------------------ */

static inline uint32_t gicd_read(uint32_t off)
{
    uint32_t v = *(volatile uint32_t *)(gicd_base + off);
    __asm__ volatile("dmb ish" ::: "memory");
    return v;
}

static inline void gicd_write(uint32_t off, uint32_t val)
{
    __asm__ volatile("dmb ish" ::: "memory");
    *(volatile uint32_t *)(gicd_base + off) = val;
}

static inline uint32_t gicc_read(uint32_t off)
{
    uint32_t v = *(volatile uint32_t *)(gicc_base + off);
    __asm__ volatile("dmb ish" ::: "memory");
    return v;
}

static inline void gicc_write(uint32_t off, uint32_t val)
{
    __asm__ volatile("dmb ish" ::: "memory");
    *(volatile uint32_t *)(gicc_base + off) = val;
}

/* ------------------------------------------------------------------ */
/* GICv3 system register helpers                                       */
/* ------------------------------------------------------------------ */

static inline void gicv3_write_icc_sre_el1(uint64_t val)
{
    __asm__ volatile("msr S3_0_C12_C12_5, %0" : : "r"(val)); /* ICC_SRE_EL1 */
    __asm__ volatile("isb");
}

static inline void gicv3_write_icc_ctlr_el1(uint64_t val)
{
    __asm__ volatile("msr S3_0_C12_C12_4, %0" : : "r"(val)); /* ICC_CTLR_EL1 */
    __asm__ volatile("isb");
}

static inline void gicv3_write_icc_pmr_el1(uint64_t val)
{
    __asm__ volatile("msr S3_0_C4_C6_0, %0" : : "r"(val));   /* ICC_PMR_EL1 */
    __asm__ volatile("isb");
}

static inline void gicv3_write_icc_igrpen1_el1(uint64_t val)
{
    __asm__ volatile("msr S3_0_C12_C12_7, %0" : : "r"(val)); /* ICC_IGRPEN1_EL1 */
    __asm__ volatile("isb");
}

static inline uint64_t gicv3_read_icc_iar1_el1(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, S3_0_C12_C12_0" : "=r"(v));    /* ICC_IAR1_EL1 */
    return v;
}

static inline void gicv3_write_icc_eoir1_el1(uint64_t val)
{
    __asm__ volatile("msr S3_0_C12_C12_1, %0" : : "r"(val)); /* ICC_EOIR1_EL1 */
    __asm__ volatile("isb");
}

/* ------------------------------------------------------------------ */
/* Detect GIC version from GICD_PIDR2                                  */
/* ------------------------------------------------------------------ */

static int detect_gic_version(void)
{
    uint32_t pidr2 = gicd_read(GICD_PIDR2);
    /* PIDR2[7:4] = ArchRev: 1=GICv1, 2=GICv2, 3=GICv3, 4=GICv4 */
    int rev = (pidr2 >> 4) & 0xF;
    if (rev >= 3)
        return 3;
    if (rev >= 2)
        return 2;
    return 2;  /* assume GICv2 if unknown */
}

/* ------------------------------------------------------------------ */
/* GICv2 initialization                                                */
/* ------------------------------------------------------------------ */

static void gicv2_init(void)
{
    /* Disable distributor while configuring */
    gicd_write(GICD_CTLR, 0);

    /* Find max IRQ: GICD_TYPER.ITLinesNumber [4:0] */
    uint32_t typer = gicd_read(GICD_TYPER);
    max_irq = ((typer & 0x1F) + 1) * 32;
    if (max_irq > MAX_IRQS)
        max_irq = MAX_IRQS;

    /* Default all SPIs: priority 0xA0, target CPU0, level-sensitive */
    for (uint32_t i = 32; i < max_irq; i += 4) {
        gicd_write(GICD_IPRIORITYR(i / 4), 0xA0A0A0A0);
        gicd_write(GICD_ITARGETSR(i / 4), 0x01010101);  /* CPU 0 */
    }

    /* Clear all pending and disable all interrupts */
    for (uint32_t i = 0; i < max_irq / 32; i++) {
        gicd_write(GICD_ICENABLER(i), 0xFFFFFFFF);
        gicd_write(GICD_ICPENDR(i),   0xFFFFFFFF);
    }

    /* All interrupts in Group 1 (non-secure) */
    for (uint32_t i = 0; i < max_irq / 32; i++) {
        gicd_write(GICD_IGROUPR(i), 0xFFFFFFFF);
    }

    /* Enable distributor: EnableGrp0 + EnableGrp1 */
    gicd_write(GICD_CTLR, 0x3);

    /* CPU interface: enable, priority mask = 0xFF (accept all) */
    gicc_write(GICC_PMR,  0xFF);
    gicc_write(GICC_CTLR, 0x1);
}

/* ------------------------------------------------------------------ */
/* GICv3 initialization                                                */
/* ------------------------------------------------------------------ */

static void gicv3_init(void)
{
    /* Enable system register interface */
    gicv3_write_icc_sre_el1(0x7);  /* SRE | DFB | DIB */

    /* Disable distributor */
    gicd_write(GICD_CTLR, 0);

    /* Find max IRQ */
    uint32_t typer = gicd_read(GICD_TYPER);
    max_irq = ((typer & 0x1F) + 1) * 32;
    if (max_irq > MAX_IRQS)
        max_irq = MAX_IRQS;

    /* Default all SPIs: priority 0xA0 */
    for (uint32_t i = 32; i < max_irq; i += 4) {
        gicd_write(GICD_IPRIORITYR(i / 4), 0xA0A0A0A0);
    }

    /* Clear all pending and disable all */
    for (uint32_t i = 0; i < max_irq / 32; i++) {
        gicd_write(GICD_ICENABLER(i), 0xFFFFFFFF);
        gicd_write(GICD_ICPENDR(i),   0xFFFFFFFF);
    }

    /* All SPIs Group 1 NS */
    for (uint32_t i = 1; i < max_irq / 32; i++) {
        gicd_write(GICD_IGROUPR(i), 0xFFFFFFFF);
    }

    /* Enable distributor: EnableGrp1NS (bit 1) + ARE_NS (bit 4) */
    gicd_write(GICD_CTLR, (1 << 4) | (1 << 1));

    /* TODO: Configure GICv3 Redistributor for SGIs/PPIs */

    /* CPU interface via system registers */
    gicv3_write_icc_pmr_el1(0xFF);       /* Accept all priorities */
    gicv3_write_icc_ctlr_el1(0);         /* EOImode=0 */
    gicv3_write_icc_igrpen1_el1(1);      /* Enable Group 1 */
}

/* ------------------------------------------------------------------ */
/* HAL Interface Implementation                                        */
/* ------------------------------------------------------------------ */

hal_status_t hal_irq_init(void)
{
    /* Zero handler table */
    for (uint32_t i = 0; i < MAX_IRQS; i++) {
        irq_table[i].handler = 0;
        irq_table[i].ctx     = 0;
    }
    for (uint32_t i = 0; i < 16; i++) {
        exception_handlers[i] = 0;
    }

    /* Detect GIC version and initialize */
    gic_version = detect_gic_version();

    if (gic_version >= 3)
        gicv3_init();
    else
        gicv2_init();

    return HAL_OK;
}

hal_status_t hal_irq_register(uint32_t irq, hal_irq_handler_t handler, void *ctx)
{
    if (irq >= max_irq)
        return HAL_ERROR;

    irq_table[irq].handler = handler;
    irq_table[irq].ctx     = ctx;

    /* Enable this IRQ in the distributor */
    hal_irq_enable(irq);

    return HAL_OK;
}

hal_status_t hal_irq_unregister(uint32_t irq)
{
    if (irq >= max_irq)
        return HAL_ERROR;

    hal_irq_disable(irq);
    irq_table[irq].handler = 0;
    irq_table[irq].ctx     = 0;

    return HAL_OK;
}

void hal_irq_enable(uint32_t irq)
{
    if (irq >= max_irq) return;
    gicd_write(GICD_ISENABLER(irq / 32), 1U << (irq % 32));
}

void hal_irq_disable(uint32_t irq)
{
    if (irq >= max_irq) return;
    gicd_write(GICD_ICENABLER(irq / 32), 1U << (irq % 32));
}

void hal_irq_eoi(uint32_t irq)
{
    if (gic_version >= 3) {
        gicv3_write_icc_eoir1_el1((uint64_t)irq);
    } else {
        gicc_write(GICC_EOIR, irq);
    }
}

hal_status_t hal_exception_register(uint32_t vector, hal_exception_handler_t handler)
{
    if (vector >= 16)
        return HAL_ERROR;
    exception_handlers[vector] = handler;
    return HAL_OK;
}

uint32_t hal_irq_max(void)
{
    return max_irq;
}

/* ------------------------------------------------------------------ */
/* IRQ dispatch (called from vector table in boot.S)                   */
/* ------------------------------------------------------------------ */

void aarch64_irq_dispatch(void)
{
    uint32_t irq;

    if (gic_version >= 3) {
        irq = (uint32_t)gicv3_read_icc_iar1_el1();
    } else {
        irq = gicc_read(GICC_IAR) & 0x3FF;
    }

    /* Spurious interrupt check (ID 1023) */
    if (irq == 1023)
        return;

    if (irq < MAX_IRQS && irq_table[irq].handler) {
        irq_table[irq].handler(irq, irq_table[irq].ctx);
    }

    hal_irq_eoi(irq);
}

/* Exception dispatch (called from vector table for synchronous exceptions) */
void aarch64_exception_dispatch(uint64_t vector, uint64_t esr, uint64_t far_el1)
{
    if (vector < 16 && exception_handlers[vector]) {
        exception_handlers[vector](vector, esr, far_el1);
    } else {
        /* Unhandled exception: halt */
        hal_cpu_disable_interrupts();
        for (;;)
            __asm__ volatile("wfi");
    }
}
