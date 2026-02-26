/* SPDX-License-Identifier: MIT */
/* AlJefra OS — HAL Interrupt Interface
 * Architecture-independent interrupt management.
 *
 * x86-64: APIC/IO-APIC + IDT
 * AArch64: GICv2/v3
 * RISC-V: PLIC + CLINT
 */

#ifndef ALJEFRA_HAL_INTERRUPT_H
#define ALJEFRA_HAL_INTERRUPT_H

#include <stdint.h>

/* Interrupt handler function type */
typedef void (*hal_irq_handler_t)(uint32_t irq, void *ctx);

/* Initialize the interrupt controller for this architecture */
hal_status_t hal_irq_init(void);

/* Register an IRQ handler.  Returns HAL_OK or HAL_ERROR. */
hal_status_t hal_irq_register(uint32_t irq, hal_irq_handler_t handler, void *ctx);

/* Unregister an IRQ handler */
hal_status_t hal_irq_unregister(uint32_t irq);

/* Enable / disable a specific IRQ line */
void hal_irq_enable(uint32_t irq);
void hal_irq_disable(uint32_t irq);

/* Signal end-of-interrupt to the controller */
void hal_irq_eoi(uint32_t irq);

/* Install exception / fault handlers (arch-specific vector numbers)
 * On x86-64: vectors 0-31 (divide error, page fault, GP fault, …)
 * On ARM64:  sync/irq/fiq/serror at EL1
 * On RISC-V: mcause / scause exceptions
 */
typedef void (*hal_exception_handler_t)(uint64_t vector, uint64_t error_code,
                                        uint64_t fault_addr);
hal_status_t hal_exception_register(uint32_t vector, hal_exception_handler_t handler);

/* Get the maximum IRQ number supported by this platform */
uint32_t hal_irq_max(void);

#endif /* ALJEFRA_HAL_INTERRUPT_H */
