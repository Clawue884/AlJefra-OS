/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- RISC-V 64-bit Interrupt Controller (PLIC + CLINT)
 * Implements hal/interrupt.h for RISC-V platforms.
 *
 * PLIC (Platform-Level Interrupt Controller):
 *   Standard base: 0x0C000000
 *   - Priority registers:    base + 4 * irq
 *   - Pending registers:     base + 0x001000
 *   - Enable registers:      base + 0x002000 + 0x80 * context
 *   - Threshold register:    base + 0x200000 + 0x1000 * context
 *   - Claim/Complete:        base + 0x200004 + 0x1000 * context
 *
 * CLINT (Core-Local Interruptor):
 *   Standard base: 0x02000000
 *   - MSIP:    base + 4 * hartid
 *   - MTIMECMP: base + 0x4000 + 8 * hartid
 *   - MTIME:   base + 0xBFF8
 *
 * In S-mode, we handle:
 *   - SEIP (Supervisor External Interrupt Pending) from PLIC
 *   - STIP (Supervisor Timer Interrupt Pending) from CLINT/SBI
 *   - SSIP (Supervisor Software Interrupt Pending) for IPI
 */

#include "../../hal/hal.h"

/* ------------------------------------------------------------------ */
/* PLIC Configuration                                                  */
/* ------------------------------------------------------------------ */

#define PLIC_BASE               0x0C000000ULL
#define PLIC_PRIORITY(irq)      (PLIC_BASE + 4 * (irq))
#define PLIC_PENDING(word)      (PLIC_BASE + 0x1000 + 4 * (word))
#define PLIC_ENABLE(ctx, word)  (PLIC_BASE + 0x2000 + 0x80 * (ctx) + 4 * (word))
#define PLIC_THRESHOLD(ctx)     (PLIC_BASE + 0x200000 + 0x1000 * (ctx))
#define PLIC_CLAIM(ctx)         (PLIC_BASE + 0x200004 + 0x1000 * (ctx))

/* S-mode context for hart 0 is typically context 1 (context 0 = M-mode) */
#define PLIC_S_CONTEXT          1

#define MAX_IRQS                1024

/* ------------------------------------------------------------------ */
/* CLINT Configuration                                                 */
/* ------------------------------------------------------------------ */

#define CLINT_BASE              0x02000000ULL
#define CLINT_MSIP(hart)        (CLINT_BASE + 4 * (hart))
#define CLINT_MTIMECMP(hart)    (CLINT_BASE + 0x4000 + 8 * (hart))
#define CLINT_MTIME             (CLINT_BASE + 0xBFF8)

/* ------------------------------------------------------------------ */
/* IRQ handler table                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    hal_irq_handler_t       handler;
    void                   *ctx;
} irq_entry_t;

static irq_entry_t irq_table[MAX_IRQS];
static hal_exception_handler_t exception_handlers[32]; /* scause exception codes */
static uint32_t max_irq = 0;

/* ------------------------------------------------------------------ */
/* MMIO helpers                                                        */
/* ------------------------------------------------------------------ */

static inline uint32_t plic_read32(uint64_t addr)
{
    uint32_t v = *(volatile uint32_t *)addr;
    __asm__ volatile("fence i, i" ::: "memory");
    return v;
}

static inline void plic_write32(uint64_t addr, uint32_t val)
{
    __asm__ volatile("fence o, o" ::: "memory");
    *(volatile uint32_t *)addr = val;
}

/* ------------------------------------------------------------------ */
/* CSR helpers                                                         */
/* ------------------------------------------------------------------ */

static inline uint64_t read_sie(void)
{
    uint64_t v;
    __asm__ volatile("csrr %0, sie" : "=r"(v));
    return v;
}

static inline void write_sie(uint64_t v)
{
    __asm__ volatile("csrw sie, %0" : : "r"(v));
}

static inline uint64_t read_scause(void)
{
    uint64_t v;
    __asm__ volatile("csrr %0, scause" : "=r"(v));
    return v;
}

static inline uint64_t read_stval(void)
{
    uint64_t v;
    __asm__ volatile("csrr %0, stval" : "=r"(v));
    return v;
}

/* SIE bits */
#define SIE_SSIE    (1ULL << 1)   /* Supervisor Software Interrupt Enable */
#define SIE_STIE    (1ULL << 5)   /* Supervisor Timer Interrupt Enable */
#define SIE_SEIE    (1ULL << 9)   /* Supervisor External Interrupt Enable */

/* ------------------------------------------------------------------ */
/* HAL Interface Implementation                                        */
/* ------------------------------------------------------------------ */

hal_status_t hal_irq_init(void)
{
    /* Zero handler tables */
    for (uint32_t i = 0; i < MAX_IRQS; i++) {
        irq_table[i].handler = 0;
        irq_table[i].ctx     = 0;
    }
    for (uint32_t i = 0; i < 32; i++) {
        exception_handlers[i] = 0;
    }

    /* Determine max IRQ by reading PLIC priority registers.
     * Standard PLIC supports up to 1023 interrupt sources (IRQ 1-1023). */
    max_irq = 1024;

    /* Set all priorities to 0 (disabled) initially */
    for (uint32_t i = 1; i < max_irq; i++) {
        plic_write32(PLIC_PRIORITY(i), 0);
    }

    /* Disable all interrupts for S-mode context */
    for (uint32_t word = 0; word < max_irq / 32; word++) {
        plic_write32(PLIC_ENABLE(PLIC_S_CONTEXT, word), 0);
    }

    /* Set threshold to 0 (accept all priorities > 0) */
    plic_write32(PLIC_THRESHOLD(PLIC_S_CONTEXT), 0);

    /* Enable supervisor external, timer, and software interrupts */
    uint64_t sie = read_sie();
    sie |= SIE_SEIE | SIE_STIE | SIE_SSIE;
    write_sie(sie);

    return HAL_OK;
}

hal_status_t hal_irq_register(uint32_t irq, hal_irq_handler_t handler, void *ctx)
{
    if (irq == 0 || irq >= max_irq)
        return HAL_ERROR;

    irq_table[irq].handler = handler;
    irq_table[irq].ctx     = ctx;

    /* Set priority to 1 (lowest non-zero = enabled) */
    plic_write32(PLIC_PRIORITY(irq), 1);

    /* Enable in the S-mode context enable bitmap */
    hal_irq_enable(irq);

    return HAL_OK;
}

hal_status_t hal_irq_unregister(uint32_t irq)
{
    if (irq == 0 || irq >= max_irq)
        return HAL_ERROR;

    hal_irq_disable(irq);
    plic_write32(PLIC_PRIORITY(irq), 0);
    irq_table[irq].handler = 0;
    irq_table[irq].ctx     = 0;

    return HAL_OK;
}

void hal_irq_enable(uint32_t irq)
{
    if (irq == 0 || irq >= max_irq) return;
    uint32_t word = irq / 32;
    uint32_t bit  = irq % 32;
    uint32_t val = plic_read32(PLIC_ENABLE(PLIC_S_CONTEXT, word));
    val |= (1U << bit);
    plic_write32(PLIC_ENABLE(PLIC_S_CONTEXT, word), val);
}

void hal_irq_disable(uint32_t irq)
{
    if (irq == 0 || irq >= max_irq) return;
    uint32_t word = irq / 32;
    uint32_t bit  = irq % 32;
    uint32_t val = plic_read32(PLIC_ENABLE(PLIC_S_CONTEXT, word));
    val &= ~(1U << bit);
    plic_write32(PLIC_ENABLE(PLIC_S_CONTEXT, word), val);
}

void hal_irq_eoi(uint32_t irq)
{
    /* Write the IRQ number to the claim/complete register to signal EOI */
    plic_write32(PLIC_CLAIM(PLIC_S_CONTEXT), irq);
}

hal_status_t hal_exception_register(uint32_t vector, hal_exception_handler_t handler)
{
    if (vector >= 32)
        return HAL_ERROR;
    exception_handlers[vector] = handler;
    return HAL_OK;
}

uint32_t hal_irq_max(void)
{
    return max_irq;
}

/* ------------------------------------------------------------------ */
/* Direct timer IRQ registration (pseudo-IRQ 0, bypasses hal_irq_register) */
/* ------------------------------------------------------------------ */

void riscv64_timer_register_direct(hal_irq_handler_t handler, void *ctx)
{
    irq_table[0].handler = handler;
    irq_table[0].ctx     = ctx;
}

/* ------------------------------------------------------------------ */
/* Trap dispatch (called from boot.S trap handler)                     */
/* ------------------------------------------------------------------ */

void riscv64_trap_dispatch(void)
{
    uint64_t scause = read_scause();
    uint64_t stval  = read_stval();

    int is_interrupt = (scause >> 63) & 1;
    uint64_t code    = scause & 0x7FFFFFFFFFFFFFFFULL;

    if (is_interrupt) {
        switch (code) {
        case 1:  /* Supervisor software interrupt */
            /* Clear SSIP */
            __asm__ volatile("csrc sip, %0" : : "r"(SIE_SSIE));
            /* TODO: Handle IPI for SMP */
            break;

        case 5:  /* Supervisor timer interrupt */
            /* Timer handler registered via irq_table with a special pseudo-IRQ */
            if (irq_table[0].handler) {
                irq_table[0].handler(0, irq_table[0].ctx);
            }
            break;

        case 9: {  /* Supervisor external interrupt (from PLIC) */
            /* Claim the interrupt */
            uint32_t irq = plic_read32(PLIC_CLAIM(PLIC_S_CONTEXT));
            if (irq == 0)
                break;  /* Spurious */

            if (irq < MAX_IRQS && irq_table[irq].handler) {
                irq_table[irq].handler(irq, irq_table[irq].ctx);
            }

            /* Complete the interrupt */
            hal_irq_eoi(irq);
            break;
        }

        default:
            /* Unknown interrupt */
            break;
        }
    } else {
        /* Synchronous exception */
        if (code < 32 && exception_handlers[code]) {
            exception_handlers[code](code, scause, stval);
        } else {
            /* Unhandled exception: halt */
            hal_cpu_disable_interrupts();
            for (;;)
                __asm__ volatile("wfi");
        }
    }
}
