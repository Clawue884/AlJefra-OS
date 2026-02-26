/* SPDX-License-Identifier: MIT */
/* AlJefra OS — x86-64 Linker Script (Multiboot1 Standalone)
 *
 * Kernel is loaded by QEMU/GRUB multiboot at 1MB physical.
 * boot.S transitions to 64-bit long mode with identity-mapped 4GB.
 * BSS is zeroed by start.c, then hal_init() → kernel_main().
 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_boot32)

KERNEL_BASE = 0x100000;  /* 1MB — standard multiboot load address */

SECTIONS
{
    . = KERNEL_BASE;

    _kernel_start = .;

    /* ---- Multiboot1 header (must be in first 8KB) ---- */
    .multiboot ALIGN(4) :
    {
        *(.multiboot)
    }

    /* ---- 32-bit boot code ---- */
    .text.boot32 ALIGN(16) :
    {
        *(.text.boot32)
    }

    /* ---- Code ---- */
    .text ALIGN(16) :
    {
        *(.text.entry)      /* C _start entry point */
        *(.text .text.*)
    }

    /* ---- Read-only data ---- */
    .rodata ALIGN(16) :
    {
        *(.rodata .rodata.*)
    }

    /* ---- GOT (needed by some code models) ---- */
    .got ALIGN(16) :
    {
        *(.got .got.plt)
    }

    /* ---- Initialized data ---- */
    .data ALIGN(16) :
    {
        *(.data .data.*)
        *(.data.boot)
    }

    _load_end = .;  /* End of file-backed data (for multiboot load_end_addr) */

    /* ---- Zero-initialized data ---- */
    . = ALIGN(16);
    _bss_start = .;
    .bss :
    {
        *(.bss .bss.*)
        *(COMMON)
    }
    _bss_end = .;

    _end = .;

    /* ---- Discarded sections ---- */
    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.gnu.hash)
        *(.dynsym)
        *(.dynstr)
        *(.dynamic)
        *(.rel*)
        *(.plt*)
        *(.interp)
    }
}
