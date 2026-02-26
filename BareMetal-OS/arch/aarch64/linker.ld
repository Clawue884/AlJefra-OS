/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- AArch64 Linker Script
 *
 * Memory layout for QEMU virt machine (-machine virt -kernel):
 *   RAM starts at 0x40000000 (1GB)
 *   Kernel loaded at 0x40080000 by QEMU -kernel (512KB offset)
 *
 * Sections:
 *   .text.boot  - Boot code (must be first)
 *   .text       - Kernel code
 *   .rodata     - Read-only data
 *   .data       - Initialized data
 *   .bss        - Zero-initialized data
 *   Stack       - 128KB BSP stack
 */

ENTRY(_start)

MEMORY
{
    /* QEMU virt RAM: starts at 0x40000000; -kernel loads at +0x80000 */
    RAM (rwx) : ORIGIN = 0x40080000, LENGTH = 255M
}

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    /* Boot code must come first at load address */
    .text.boot ORIGIN(RAM) : {
        KEEP(*(.text.boot))
    } > RAM :text

    /* Main code section */
    .text : ALIGN(4096) {
        *(.text .text.*)
    } > RAM :text

    /* Read-only data */
    .rodata : ALIGN(4096) {
        *(.rodata .rodata.*)
    } > RAM :text

    /* Initialized data */
    .data : ALIGN(4096) {
        *(.data .data.*)
    } > RAM :data

    /* Zero-initialized data */
    .bss : ALIGN(4096) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > RAM :data

    /* BSP Stack: 128KB, 16-byte aligned (AArch64 requires 16-byte SP alignment) */
    .stack : ALIGN(16) {
        _stack_bottom = .;
        . += 128K;
        _stack_top = .;
    } > RAM :data

    /* Secondary core stacks region (referenced by boot.S) */
    .sec_stacks : ALIGN(4096) {
        _sec_stack_base = .;
        /* Space for up to 8 secondary cores, 64KB each = 512KB */
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
        *(.ARM.attributes)
    }
}

/* Provide symbols for C code */
PROVIDE(_start = 0x40080000);
PROVIDE(__bss_start = ADDR(.bss));
PROVIDE(__bss_end = ADDR(.bss) + SIZEOF(.bss));
