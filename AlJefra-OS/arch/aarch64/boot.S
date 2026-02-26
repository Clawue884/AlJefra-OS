/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- AArch64 Boot Stub
 *
 * Entry point for AArch64 freestanding boot.
 * Handles EL2 -> EL1 transition, stack setup, BSS clearing,
 * and calling into the C HAL init.
 *
 * On QEMU virt machine:
 *   - Entry at EL1 (with -machine virt) or EL2 (with -machine virt,virtualization=on)
 *   - x0 = DTB pointer (physical address of device tree blob)
 *   - Load address: 0x40000000 (default for -kernel)
 *
 * Memory layout (defined in linker.ld):
 *   0x40000000  .text   (code)
 *   ...         .rodata (read-only data)
 *   ...         .data   (initialized data)
 *   ...         .bss    (zero-initialized data)
 *   ...         stack   (grows downward from _stack_top)
 */

.section .text.boot
.global _start
.global _aarch64_secondary_start

/* ================================================================== */
/* Primary (BSP) entry point                                           */
/* ================================================================== */

_start:
    /* x0 = DTB pointer from firmware, save it */
    mov     x19, x0

    /* Check current Exception Level */
    mrs     x1, CurrentEL
    and     x1, x1, #0xC       /* Extract EL field [3:2] */
    cmp     x1, #0x8           /* EL2 = 0b1000 */
    b.eq    .from_el2
    cmp     x1, #0xC           /* EL3 = 0b1100 */
    b.eq    .from_el3
    b       .at_el1            /* Already at EL1 */

/* ------------------------------------------------------------------ */
/* Drop from EL3 to EL2                                                */
/* ------------------------------------------------------------------ */
.from_el3:
    /* Configure EL3 -> EL2 transition */
    mov     x0, #0x531         /* SCR_EL3: RW (AArch64 at EL2), NS, HCE, SMD */
    msr     SCR_EL3, x0

    mov     x0, #0x3C9         /* SPSR_EL3: EL2h, all exceptions masked */
    msr     SPSR_EL3, x0

    adr     x0, .from_el2
    msr     ELR_EL3, x0
    eret

/* ------------------------------------------------------------------ */
/* Drop from EL2 to EL1                                                */
/* ------------------------------------------------------------------ */
.from_el2:
    /* HCR_EL2: RW bit (EL1 is AArch64) */
    mov     x0, #(1 << 31)     /* HCR_EL2.RW = 1 */
    msr     HCR_EL2, x0

    /* Disable EL2 trapping of FP/NEON/SVE.
     * CPTR_EL2: TFP(bit10)=0 (don't trap FP), TCPAC(bit12)=0 (allow CPACR_EL1),
     * TTA(bit13)=0 (don't trap trace), bits[9:0] are RES1 on AArch64. */
    mov     x0, #0x03FF
    msr     CPTR_EL2, x0
    msr     HSTR_EL2, xzr      /* No hypervisor trap of system registers */

    /* Enable virtual counter access at EL1 */
    mrs     x0, CNTHCTL_EL2
    orr     x0, x0, #3         /* EL1PCEN | EL1PCTEN */
    msr     CNTHCTL_EL2, x0
    msr     CNTVOFF_EL2, xzr   /* Zero virtual offset */

    /* SPSR_EL2: return to EL1h with all exceptions masked */
    mov     x0, #0x3C5         /* M[3:0]=0101 (EL1h), DAIF=1111 */
    msr     SPSR_EL2, x0

    /* Set EL1 entry point */
    adr     x0, .at_el1
    msr     ELR_EL2, x0

    /* Enable PSCI conduit (QEMU requires this for CPU_ON) */
    /* VPIDR_EL2 / VMPIDR_EL2: copy physical values */
    mrs     x0, MIDR_EL1
    msr     VPIDR_EL2, x0
    mrs     x0, MPIDR_EL1
    msr     VMPIDR_EL2, x0

    eret

/* ------------------------------------------------------------------ */
/* At EL1: common setup for BSP                                        */
/* ------------------------------------------------------------------ */
.at_el1:
    /* Disable MMU, caches, and alignment checking initially.
     * We'll enable MMU and caches in hal_mmu_init.
     * Alignment checking (A bit) must be OFF because the compiler
     * may emit unaligned multi-byte stores (e.g. 8-byte store to
     * a 4-byte-aligned struct field). */
    mrs     x0, SCTLR_EL1
    bic     x0, x0, #(1 << 0)  /* M: MMU off */
    bic     x0, x0, #(1 << 1)  /* A: Alignment check off */
    bic     x0, x0, #(1 << 2)  /* C: D-cache off */
    bic     x0, x0, #(1 << 12) /* I: I-cache off */
    msr     SCTLR_EL1, x0
    isb

    /* Set up the BSP stack */
    ldr     x0, =_stack_top
    mov     sp, x0

    /* Zero BSS section */
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
    cmp     x0, x1
    b.ge    .bss_done
.bss_loop:
    str     xzr, [x0], #8
    cmp     x0, x1
    b.lt    .bss_loop
.bss_done:

    /* Set up exception vector table */
    ldr     x0, =_vector_table
    msr     VBAR_EL1, x0
    isb

    /* Restore DTB pointer in x0 (first argument to hal_init) */
    mov     x0, x19

    /* Call hal_init() -> kernel_main() */
    bl      hal_init
    bl      kernel_main

    /* If kernel_main returns, halt */
.halt:
    wfi
    b       .halt

/* ================================================================== */
/* Secondary core entry point (for PSCI CPU_ON)                        */
/* ================================================================== */

_aarch64_secondary_start:
    /* x0 = context_id (core_id) passed from PSCI CPU_ON */

    /* Drop to EL1 if at EL2 (PSCI may enter at EL2) */
    mrs     x1, CurrentEL
    and     x1, x1, #0xC
    cmp     x1, #0x8
    b.ne    .sec_at_el1

    /* Quick EL2 -> EL1 for secondary */
    mov     x2, #(1 << 31)
    msr     HCR_EL2, x2
    mov     x2, #0x3C5
    msr     SPSR_EL2, x2
    adr     x2, .sec_at_el1
    msr     ELR_EL2, x2
    eret

.sec_at_el1:
    /* x0 still = core_id */
    /* Set up per-core stack: base + (core_id + 1) * 64KB */
    ldr     x1, =_sec_stack_base
    mov     x2, #(64 * 1024)
    add     x3, x0, #1
    mul     x2, x2, x3
    add     sp, x1, x2

    /* Set exception vector table */
    ldr     x1, =_vector_table
    msr     VBAR_EL1, x1
    isb

    /* Enable FPU for this core */
    mrs     x1, CPACR_EL1
    orr     x1, x1, #(3 << 20)
    msr     CPACR_EL1, x1
    isb

    /* Call C entry point: aarch64_secondary_entry(core_id) */
    bl      aarch64_secondary_entry

    /* Should not return, but just in case */
.sec_halt:
    wfi
    b       .sec_halt

/* ================================================================== */
/* Exception Vector Table (EL1)                                        */
/* ================================================================== */

/* AArch64 vector table: 16 entries, 128 bytes each (32 instructions).
 * Layout:
 *   [0]  Current EL with SP_EL0 - Synchronous
 *   [1]  Current EL with SP_EL0 - IRQ
 *   [2]  Current EL with SP_EL0 - FIQ
 *   [3]  Current EL with SP_EL0 - SError
 *   [4]  Current EL with SP_ELx - Synchronous
 *   [5]  Current EL with SP_ELx - IRQ
 *   [6]  Current EL with SP_ELx - FIQ
 *   [7]  Current EL with SP_ELx - SError
 *   [8]  Lower EL AArch64       - Synchronous
 *   [9]  Lower EL AArch64       - IRQ
 *   [10] Lower EL AArch64       - FIQ
 *   [11] Lower EL AArch64       - SError
 *   [12] Lower EL AArch32       - Synchronous
 *   [13] Lower EL AArch32       - IRQ
 *   [14] Lower EL AArch32       - FIQ
 *   [15] Lower EL AArch32       - SError
 */

.balign 2048
_vector_table:

/* ---- Current EL with SP_EL0 ---- */
.balign 128
    b       _exc_sync_sp0       /* Synchronous */
.balign 128
    b       _exc_irq_sp0        /* IRQ */
.balign 128
    b       _exc_fiq_sp0        /* FIQ */
.balign 128
    b       _exc_serror_sp0     /* SError */

/* ---- Current EL with SP_ELx ---- */
.balign 128
    b       _exc_sync_spx       /* Synchronous */
.balign 128
    b       _exc_irq_spx        /* IRQ -- this is the normal IRQ path */
.balign 128
    b       _exc_fiq_spx        /* FIQ */
.balign 128
    b       _exc_serror_spx     /* SError */

/* ---- Lower EL AArch64 ---- */
.balign 128
    b       _exc_sync_lower64   /* Synchronous */
.balign 128
    b       _exc_irq_lower64    /* IRQ */
.balign 128
    b       _exc_fiq_lower64    /* FIQ */
.balign 128
    b       _exc_serror_lower64 /* SError */

/* ---- Lower EL AArch32 ---- */
.balign 128
    b       _exc_sync_lower32   /* Synchronous */
.balign 128
    b       _exc_irq_lower32    /* IRQ */
.balign 128
    b       _exc_fiq_lower32    /* FIQ */
.balign 128
    b       _exc_serror_lower32 /* SError */

/* ================================================================== */
/* Exception handlers                                                  */
/* ================================================================== */

/* Macro to save all general-purpose registers */
.macro SAVE_REGS
    sub     sp, sp, #(34 * 8)
    stp     x0,  x1,  [sp, #(0  * 8)]
    stp     x2,  x3,  [sp, #(2  * 8)]
    stp     x4,  x5,  [sp, #(4  * 8)]
    stp     x6,  x7,  [sp, #(6  * 8)]
    stp     x8,  x9,  [sp, #(8  * 8)]
    stp     x10, x11, [sp, #(10 * 8)]
    stp     x12, x13, [sp, #(12 * 8)]
    stp     x14, x15, [sp, #(14 * 8)]
    stp     x16, x17, [sp, #(16 * 8)]
    stp     x18, x19, [sp, #(18 * 8)]
    stp     x20, x21, [sp, #(20 * 8)]
    stp     x22, x23, [sp, #(22 * 8)]
    stp     x24, x25, [sp, #(24 * 8)]
    stp     x26, x27, [sp, #(26 * 8)]
    stp     x28, x29, [sp, #(28 * 8)]
    mrs     x0, ELR_EL1
    mrs     x1, SPSR_EL1
    stp     x30, x0,  [sp, #(30 * 8)]
    str     x1,       [sp, #(32 * 8)]
.endm

.macro RESTORE_REGS
    ldr     x1,       [sp, #(32 * 8)]
    ldp     x30, x0,  [sp, #(30 * 8)]
    msr     SPSR_EL1, x1
    msr     ELR_EL1, x0
    ldp     x28, x29, [sp, #(28 * 8)]
    ldp     x26, x27, [sp, #(26 * 8)]
    ldp     x24, x25, [sp, #(24 * 8)]
    ldp     x22, x23, [sp, #(22 * 8)]
    ldp     x20, x21, [sp, #(20 * 8)]
    ldp     x18, x19, [sp, #(18 * 8)]
    ldp     x16, x17, [sp, #(16 * 8)]
    ldp     x14, x15, [sp, #(14 * 8)]
    ldp     x12, x13, [sp, #(12 * 8)]
    ldp     x10, x11, [sp, #(10 * 8)]
    ldp     x8,  x9,  [sp, #(8  * 8)]
    ldp     x6,  x7,  [sp, #(6  * 8)]
    ldp     x4,  x5,  [sp, #(4  * 8)]
    ldp     x2,  x3,  [sp, #(2  * 8)]
    ldp     x0,  x1,  [sp, #(0  * 8)]
    add     sp, sp, #(34 * 8)
.endm

/* IRQ handler (main path -- Current EL with SP_ELx) */
_exc_irq_spx:
    SAVE_REGS
    bl      aarch64_irq_dispatch
    RESTORE_REGS
    eret

/* Synchronous exception handler */
_exc_sync_spx:
    SAVE_REGS
    mrs     x0, ESR_EL1         /* x0 = exception syndrome (vector) */
    lsr     x0, x0, #26         /* EC field [31:26] */
    mrs     x1, ESR_EL1         /* x1 = full ESR (error code) */
    mrs     x2, FAR_EL1         /* x2 = fault address */
    bl      aarch64_exception_dispatch
    RESTORE_REGS
    eret

/* Default handlers for other vectors (just hang for now) */
_exc_sync_sp0:
_exc_irq_sp0:
_exc_fiq_sp0:
_exc_serror_sp0:
_exc_fiq_spx:
_exc_serror_spx:
_exc_sync_lower64:
_exc_irq_lower64:
_exc_fiq_lower64:
_exc_serror_lower64:
_exc_sync_lower32:
_exc_irq_lower32:
_exc_fiq_lower32:
_exc_serror_lower32:
    SAVE_REGS
    mrs     x0, ESR_EL1
    lsr     x0, x0, #26
    mrs     x1, ESR_EL1
    mrs     x2, FAR_EL1
    bl      aarch64_exception_dispatch
    RESTORE_REGS
    eret

/* _sec_stack_base is defined by the linker script (linker.ld) */
