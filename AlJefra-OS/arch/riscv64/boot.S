/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- RISC-V 64-bit Boot Stub
 *
 * Entry point for RISC-V freestanding boot (S-mode).
 *
 * OpenSBI convention:
 *   a0 = hartid (hardware thread ID)
 *   a1 = DTB pointer (physical address of device tree blob)
 *
 * QEMU virt machine:
 *   Kernel loaded at 0x80200000 (after OpenSBI at 0x80000000)
 *   RAM starts at 0x80000000
 *
 * The boot hart is whichever hart OpenSBI selects.
 * Non-boot harts are parked by OpenSBI until SBI HSM HART_START.
 */

.section .text.boot
.global _start
.global _riscv64_secondary_start

/* ================================================================== */
/* Primary (boot hart) entry point                                     */
/* ================================================================== */

_start:
    /* a0 = hartid, a1 = DTB pointer */

    /* Save hartid in tp (thread pointer -- used by hal_cpu_id) */
    mv      tp, a0

    /* Save DTB pointer in s1 (callee-saved, survives BSS zeroing) */
    mv      s1, a1

    /* Set up the global pointer (for linker relaxation) */
.option push
.option norelax
    la      gp, __global_pointer$
.option pop

    /* Set up boot stack */
    la      sp, _stack_top

    /* Zero BSS section */
    la      t0, __bss_start
    la      t1, __bss_end
    bgeu    t0, t1, .bss_done
.bss_loop:
    sd      zero, 0(t0)
    addi    t0, t0, 8
    bltu    t0, t1, .bss_loop
.bss_done:

    /* Store DTB pointer to global variable (riscv64_dtb_addr is in BSS,
     * now safely zeroed, so we can write the real value). */
    la      t0, riscv64_dtb_addr
    sd      s1, 0(t0)

    /* Set up trap vector (stvec): direct mode (bit 0 = 0) */
    la      t0, _trap_vector
    csrw    stvec, t0

    /* Enable FPU: set sstatus.FS = 01 (Initial) */
    li      t0, (1 << 13)
    csrs    sstatus, t0

    /* Call hal_init() -- no arguments (hartid in tp, DTB in global) */
    call    hal_init

    /* Call kernel_main() */
    call    kernel_main

    /* If kernel_main returns, halt */
.halt:
    wfi
    j       .halt

/* ================================================================== */
/* Secondary hart entry point (for SBI HSM HART_START)                 */
/* ================================================================== */

/* OpenSBI enters secondary harts here in S-mode with:
 *   a0 = hartid
 *   a1 = opaque (we pass hartid) */

_riscv64_secondary_start:
    /* Set tp = hartid */
    mv      tp, a0

    /* Set up global pointer */
.option push
.option norelax
    la      gp, __global_pointer$
.option pop

    /* Set up per-hart stack: base + (hartid + 1) * 64KB */
    la      t0, _sec_stack_base
    li      t1, (64 * 1024)
    addi    t2, a0, 1
    mul     t1, t1, t2
    add     sp, t0, t1

    /* Set trap vector */
    la      t0, _trap_vector
    csrw    stvec, t0

    /* Enable FPU */
    li      t0, (1 << 13)
    csrs    sstatus, t0

    /* Call C entry: riscv64_secondary_entry(hartid) */
    mv      a0, tp
    call    riscv64_secondary_entry

    /* Should not return */
.sec_halt:
    wfi
    j       .sec_halt

/* ================================================================== */
/* Trap Vector                                                         */
/* ================================================================== */

/* Direct mode: all traps come here.
 * We save context and dispatch to C handler. */

.balign 4
_trap_vector:
    /* Save all registers to stack */
    addi    sp, sp, -(32 * 8)

    sd      x1,  (0  * 8)(sp)    /* ra */
    sd      x3,  (1  * 8)(sp)    /* gp */
    sd      x4,  (2  * 8)(sp)    /* tp */
    sd      x5,  (3  * 8)(sp)    /* t0 */
    sd      x6,  (4  * 8)(sp)    /* t1 */
    sd      x7,  (5  * 8)(sp)    /* t2 */
    sd      x8,  (6  * 8)(sp)    /* s0/fp */
    sd      x9,  (7  * 8)(sp)    /* s1 */
    sd      x10, (8  * 8)(sp)    /* a0 */
    sd      x11, (9  * 8)(sp)    /* a1 */
    sd      x12, (10 * 8)(sp)    /* a2 */
    sd      x13, (11 * 8)(sp)    /* a3 */
    sd      x14, (12 * 8)(sp)    /* a4 */
    sd      x15, (13 * 8)(sp)    /* a5 */
    sd      x16, (14 * 8)(sp)    /* a6 */
    sd      x17, (15 * 8)(sp)    /* a7 */
    sd      x18, (16 * 8)(sp)    /* s2 */
    sd      x19, (17 * 8)(sp)    /* s3 */
    sd      x20, (18 * 8)(sp)    /* s4 */
    sd      x21, (19 * 8)(sp)    /* s5 */
    sd      x22, (20 * 8)(sp)    /* s6 */
    sd      x23, (21 * 8)(sp)    /* s7 */
    sd      x24, (22 * 8)(sp)    /* s8 */
    sd      x25, (23 * 8)(sp)    /* s9 */
    sd      x26, (24 * 8)(sp)    /* s10 */
    sd      x27, (25 * 8)(sp)    /* s11 */
    sd      x28, (26 * 8)(sp)    /* t3 */
    sd      x29, (27 * 8)(sp)    /* t4 */
    sd      x30, (28 * 8)(sp)    /* t5 */
    sd      x31, (29 * 8)(sp)    /* t6 */

    /* Save sepc and sstatus */
    csrr    t0, sepc
    csrr    t1, sstatus
    sd      t0, (30 * 8)(sp)
    sd      t1, (31 * 8)(sp)

    /* Call C trap handler */
    call    riscv64_trap_dispatch

    /* Restore sepc and sstatus */
    ld      t0, (30 * 8)(sp)
    ld      t1, (31 * 8)(sp)
    csrw    sepc, t0
    csrw    sstatus, t1

    /* Restore all registers */
    ld      x1,  (0  * 8)(sp)
    ld      x3,  (1  * 8)(sp)
    ld      x4,  (2  * 8)(sp)
    ld      x5,  (3  * 8)(sp)
    ld      x6,  (4  * 8)(sp)
    ld      x7,  (5  * 8)(sp)
    ld      x8,  (6  * 8)(sp)
    ld      x9,  (7  * 8)(sp)
    ld      x10, (8  * 8)(sp)
    ld      x11, (9  * 8)(sp)
    ld      x12, (10 * 8)(sp)
    ld      x13, (11 * 8)(sp)
    ld      x14, (12 * 8)(sp)
    ld      x15, (13 * 8)(sp)
    ld      x16, (14 * 8)(sp)
    ld      x17, (15 * 8)(sp)
    ld      x18, (16 * 8)(sp)
    ld      x19, (17 * 8)(sp)
    ld      x20, (18 * 8)(sp)
    ld      x21, (19 * 8)(sp)
    ld      x22, (20 * 8)(sp)
    ld      x23, (21 * 8)(sp)
    ld      x24, (22 * 8)(sp)
    ld      x25, (23 * 8)(sp)
    ld      x26, (24 * 8)(sp)
    ld      x27, (25 * 8)(sp)
    ld      x28, (26 * 8)(sp)
    ld      x29, (27 * 8)(sp)
    ld      x30, (28 * 8)(sp)
    ld      x31, (29 * 8)(sp)

    addi    sp, sp, (32 * 8)

    sret
