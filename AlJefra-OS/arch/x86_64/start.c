/* SPDX-License-Identifier: MIT */
/* AlJefra OS — x86-64 C entry point
 * Called by the AlJefra kernel at 0x1E0000.
 * Zeroes BSS, calls hal_init(), then kernel_main().
 *
 * Only x86-64 has this separate start.c shim because the Multiboot1
 * entry point must be in a specific section (.text.entry) and needs
 * to zero BSS before calling any C code.  On AArch64 and RISC-V the
 * boot.S assembly jumps directly to hal_init(), which serves as the
 * C entry point (BSS is zeroed in the assembly stub).
 */

#include "../../hal/hal.h"

/* Linker-defined symbols */
extern char _bss_start[];
extern char _bss_end[];

/* Kernel entry (architecture-independent) */
extern void kernel_main(void);

/* _start must be placed in .text.entry so the linker puts it first */
__attribute__((section(".text.entry"), noreturn))
void _start(void)
{
    /* Zero the BSS segment */
    char *p = _bss_start;
    while (p < _bss_end)
        *p++ = 0;

    /* Initialize hardware abstraction layer */
    hal_init();

    /* Enter architecture-independent kernel */
    kernel_main();

    /* Should never return — halt if it does */
    for (;;)
        hal_cpu_halt();
}
