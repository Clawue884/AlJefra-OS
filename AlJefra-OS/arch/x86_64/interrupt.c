/* SPDX-License-Identifier: MIT */
/* AlJefra OS — x86-64 Interrupt HAL Implementation (Standalone)
 *
 * Maintains a handler table for IRQ dispatch.  In standalone mode the
 * kernel uses polling (drivers call hal_irq_eoi after APIC EOI).
 * A full IDT + IO-APIC setup can be added later for interrupt-driven I/O.
 *
 * HAL IRQ 0  = Timer
 * HAL IRQ 1  = Keyboard
 * HAL IRQ 11 = Network (common PCI IRQ line)
 */

#include "../../hal/hal.h"

#define MAX_IRQ_HANDLERS  256
#define MAX_EXCEPTION_HANDLERS 32

/* Handler table */
typedef struct {
    hal_irq_handler_t handler;
    void             *ctx;
} irq_slot_t;

static irq_slot_t irq_handlers[MAX_IRQ_HANDLERS];
static hal_exception_handler_t exception_handlers[MAX_EXCEPTION_HANDLERS];
static bool irq_initialized = false;

/* -------------------------------------------------------------------------- */
/* HAL Interrupt API                                                          */
/* -------------------------------------------------------------------------- */

hal_status_t hal_irq_init(void)
{
    /* Zero all handler slots */
    for (int i = 0; i < MAX_IRQ_HANDLERS; i++) {
        irq_handlers[i].handler = 0;
        irq_handlers[i].ctx = 0;
    }
    for (int i = 0; i < MAX_EXCEPTION_HANDLERS; i++) {
        exception_handlers[i] = 0;
    }

    irq_initialized = true;
    return HAL_OK;
}

hal_status_t hal_irq_register(uint32_t irq, hal_irq_handler_t handler, void *ctx)
{
    if (irq >= MAX_IRQ_HANDLERS)
        return HAL_ERROR;

    irq_handlers[irq].handler = handler;
    irq_handlers[irq].ctx = ctx;

    /* In standalone mode, handlers are stored but not wired to the
     * interrupt controller.  Drivers use polling.  A future IDT + IO-APIC
     * setup would route hardware IRQs to these handlers via an ISR
     * dispatcher that calls irq_handlers[irq].handler(). */

    return HAL_OK;
}

hal_status_t hal_irq_unregister(uint32_t irq)
{
    if (irq >= MAX_IRQ_HANDLERS)
        return HAL_ERROR;

    irq_handlers[irq].handler = 0;
    irq_handlers[irq].ctx = 0;

    return HAL_OK;
}

void hal_irq_enable(uint32_t irq)
{
    /* On AlJefra, IRQs are enabled by registering callbacks.
     * For direct APIC manipulation, we would need MMIO access to the
     * IO-APIC redirection table.  The kernel handles this internally. */
    (void)irq;
}

void hal_irq_disable(uint32_t irq)
{
    /* Unregister effectively disables the callback path */
    (void)irq;
}

void hal_irq_eoi(uint32_t irq)
{
    (void)irq;
    /* Write EOI to the local APIC.
     * The local APIC EOI register is at APIC_BASE + 0xB0.
     * On AlJefra, APIC base is typically at 0xFEE00000 (default x2APIC). */
    volatile uint32_t *apic_eoi = (volatile uint32_t *)0xFEE000B0ULL;
    *apic_eoi = 0;
}

hal_status_t hal_exception_register(uint32_t vector, hal_exception_handler_t handler)
{
    if (vector >= MAX_EXCEPTION_HANDLERS)
        return HAL_ERROR;

    exception_handlers[vector] = handler;

    /* Note: On AlJefra, the kernel owns the IDT.  User-space programs
     * cannot directly modify IDT entries.  This stores the handler for
     * use if the kernel dispatches exceptions to user callbacks (future). */
    return HAL_OK;
}

uint32_t hal_irq_max(void)
{
    /* IO-APIC typically supports 24 IRQ lines (0-23).
     * We report 255 as the maximum to allow for MSI/MSI-X. */
    return MAX_IRQ_HANDLERS - 1;
}
