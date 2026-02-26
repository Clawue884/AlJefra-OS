/* Linker script for payload at physical address 0x1E0000 */
OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x1E0000;
    .text ALIGN(16) : { *(.text.entry) *(.text .text.*) }
    .rodata ALIGN(16) : { *(.rodata .rodata.*) }
    .data ALIGN(16) : { *(.data .data.*) }
    .bss ALIGN(16) : { *(.bss .bss.*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.gnu.hash) *(.dynsym) *(.dynstr) *(.dynamic) *(.rel*) *(.plt*) *(.interp) }
}
