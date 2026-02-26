/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- RISC-V 64-bit Linker Script
 *
 * Memory layout for QEMU virt machine with OpenSBI:
 *   OpenSBI (M-mode firmware): 0x80000000
 *   Kernel (S-mode):           0x80200000 (loaded by -kernel option)
 *   RAM ends:                  0x80000000 + RAM_SIZE
 *
 * Sections:
 *   .text.boot  - Boot code (must be first)
 *   .text       - Kernel code
 *   .rodata     - Read-only data
 *   .data       - Initialized data (includes .sdata for GP-relative)
 *   .bss        - Zero-initialized data (includes .sbss)
 *   Stack       - 128KB BSP stack
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    /* QEMU virt: kernel at 0x80200000 (after OpenSBI) */
    RAM (rwx) : ORIGIN = 0x80200000, LENGTH = 254M
}

PHDRS
{
    text PT_LOAD FLAGS(5);   /* Readable + Executable */
    data PT_LOAD FLAGS(6);   /* Readable + Writable */
}

SECTIONS
{
    /* Boot code must come first */
    .text.boot ORIGIN(RAM) : {
        KEEP(*(.text.boot))
    } > RAM :text

    /* Main code */
    .text : ALIGN(4096) {
        *(.text .text.*)
    } > RAM :text

    /* Read-only data */
    .rodata : ALIGN(4096) {
        *(.rodata .rodata.*)
    } > RAM :text

    /* Initialized data (including small data for GP-relative addressing) */
    .data : ALIGN(4096) {
        *(.data .data.*)
        *(.sdata .sdata.*)

        /* Global pointer anchor: GP-relative addressing covers
         * +/- 2KB around __global_pointer$ for efficient small data access. */
        PROVIDE(__global_pointer$ = . + 0x800);

        *(.srodata .srodata.*)
    } > RAM :data

    /* Zero-initialized data */
    .bss : ALIGN(4096) {
        __bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > RAM :data

    /* BSP Stack: 128KB, 16-byte aligned (RISC-V ABI requires 16-byte SP) */
    .stack : ALIGN(16) {
        _stack_bottom = .;
        . += 128K;
        _stack_top = .;
    } > RAM :data

    /* Secondary hart stacks */
    .sec_stacks : ALIGN(4096) {
        _sec_stack_base = .;
        /* Space for up to 8 secondary harts, 64KB each = 512KB */
        . += 512K;
        _sec_stack_top = .;
    } > RAM :data

    /* Heap starts after all static allocations */
    _heap_start = .;
    _heap_end = ORIGIN(RAM) + LENGTH(RAM);

    /* Discard unwanted sections */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
        *(.riscv.attributes)
    }
}

/* Provide symbols (these are defined inline above, but PROVIDE ensures
 * they exist even if the .bss section is empty) */
PROVIDE(__bss_start = ADDR(.bss));
PROVIDE(__bss_end = ADDR(.bss) + SIZEOF(.bss));
PROVIDE(_sec_stack_base = ADDR(.sec_stacks));
