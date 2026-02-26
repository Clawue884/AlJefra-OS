/* Linker script for runtime .ajdrv drivers.
 * Produces a flat binary with code+rodata+data+bss.
 * Entry point at offset 0 (first function in .text). */

SECTIONS
{
    . = 0;
    .text   : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    .bss    : { *(.bss .bss.* COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
