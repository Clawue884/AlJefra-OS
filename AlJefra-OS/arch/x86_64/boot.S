/* SPDX-License-Identifier: MIT */
/* AlJefra OS — x86-64 Multiboot1 Boot Stub
 *
 * Starts in 32-bit protected mode (multiboot), transitions to 64-bit
 * long mode, sets up identity mapping (first 4GB with 2MB pages),
 * then calls the C entry point _start.
 *
 * Uses multiboot1 with a.out kludge (flags bit 16) so QEMU loads the
 * raw binary at the specified addresses without requiring a 32-bit ELF.
 *
 * Memory layout:
 *   0x00000000 - 0x00010000  Reserved (IVT, BDA, etc.)
 *   0x00010000 - 0x00016000  Boot page tables (PML4, PDPT, PD×4)
 *   0x00070000 - 0x00080000  Boot stack (64KB, grows down from 0x80000)
 *   0x00100000 - ...         Kernel text+rodata+data+BSS (loaded by bootloader)
 *   0x00800000 - 0x01000000  DMA region (used by drivers)
 *   0x01000000 - 0x02000000  Page pool (used by MMU HAL)
 */

/* Multiboot1 constants */
#define MULTIBOOT_MAGIC    0x1BADB002
/* Flags: bit 0 = page-aligned modules, bit 1 = memory info,
 *        bit 16 = use header address fields (a.out kludge) */
#define MULTIBOOT_FLAGS    0x00010003
#define MULTIBOOT_CHECKSUM (-(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS))

/* Multiboot1 bootloader magic (passed in EAX by bootloader) */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

.section .multiboot, "ax"
.align 4

/* ── Multiboot1 Header with a.out kludge ── */
multiboot_header:
    .long MULTIBOOT_MAGIC       /* magic */
    .long MULTIBOOT_FLAGS       /* flags */
    .long MULTIBOOT_CHECKSUM    /* checksum */
    /* a.out kludge address fields (used because flags bit 16 is set) */
    .long multiboot_header      /* header_addr: physical address of this header */
    .long _kernel_start         /* load_addr: start of text+data to load */
    .long _load_end             /* load_end_addr: end of data to load from file */
    .long _bss_end              /* bss_end_addr: end of BSS (zeroed by loader) */
    .long _boot32               /* entry_addr: entry point */

/* ── 32-bit entry point ── */
.section .text.boot32, "ax"
.code32

.global _boot32
_boot32:
    /* Disable interrupts */
    cli

    /* Save multiboot info pointer (EBX) and magic (EAX) */
    movl %ebx, (boot_mb_info_ptr)
    movl $0,   (boot_mb_info_ptr + 4)
    movl %eax, (boot_mb_magic)

    /* ── Set up page tables ── */
    /* PML4 at 0x10000, PDPT at 0x11000, PD[0-3] at 0x12000-0x15000
     * Placed in low conventional memory to avoid overlap with kernel BSS. */

    /* Zero 6 pages of page tables */
    movl $0x10000, %edi
    xorl %eax, %eax
    movl $(6 * 4096 / 4), %ecx
    rep stosl

    /* PML4[0] -> PDPT at 0x11000 (present + writable) */
    movl $0x11000, %eax
    orl  $0x03, %eax        /* P | RW */
    movl %eax, 0x10000

    /* PDPT[0] -> PD[0] at 0x12000 */
    movl $0x12000, %eax
    orl  $0x03, %eax
    movl %eax, 0x11000

    /* PDPT[1] -> PD[1] at 0x13000 */
    movl $0x13000, %eax
    orl  $0x03, %eax
    movl %eax, 0x11008

    /* PDPT[2] -> PD[2] at 0x14000 */
    movl $0x14000, %eax
    orl  $0x03, %eax
    movl %eax, 0x11010

    /* PDPT[3] -> PD[3] at 0x15000 */
    movl $0x15000, %eax
    orl  $0x03, %eax
    movl %eax, 0x11018

    /* Fill PD[0-3] with 2MB pages: identity map 0 - 4GB */
    movl $0x12000, %edi
    movl $0x00000083, %eax  /* 0MB, Present | RW | PS (2MB page) */
    movl $0, %edx
    movl $2048, %ecx

1:
    movl %eax, (%edi)
    movl %edx, 4(%edi)
    addl $0x200000, %eax
    adcl $0, %edx
    addl $8, %edi
    decl %ecx
    jnz  1b

    /* ── Enable PAE ── */
    movl %cr4, %eax
    orl  $(1 << 5), %eax    /* CR4.PAE */
    movl %eax, %cr4

    /* Load PML4 into CR3 */
    movl $0x10000, %eax
    movl %eax, %cr3

    /* ── Enable Long Mode (EFER.LME) ── */
    movl $0xC0000080, %ecx  /* IA32_EFER MSR */
    rdmsr
    orl  $(1 << 8), %eax    /* LME */
    orl  $(1 << 11), %eax   /* NXE */
    wrmsr

    /* ── Enable Paging ── */
    movl %cr0, %eax
    orl  $(1 << 31), %eax   /* CR0.PG */
    orl  $(1 << 0), %eax    /* CR0.PE */
    movl %eax, %cr0

    /* ── Load 64-bit GDT and far jump to 64-bit code ── */
    lgdt (gdt64_ptr)
    ljmp $0x08, $_boot64

/* ── 64-bit entry point ── */
.code64
.global _boot64
_boot64:
    /* Reload data segments */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* Set up stack at 0x80000 (64KB stack area, grows down to 0x70000) */
    movq $0x80000, %rsp
    xorq %rbp, %rbp

    /* Enable SSE (needed by compiler-generated code) */
    movq %cr0, %rax
    andq $~(1 << 2), %rax   /* Clear CR0.EM */
    orq  $(1 << 1), %rax    /* Set CR0.MP */
    movq %rax, %cr0

    movq %cr4, %rax
    orq  $(1 << 9), %rax    /* CR4.OSFXSR */
    orq  $(1 << 10), %rax   /* CR4.OSXMMEXCPT */
    movq %rax, %cr4

    /* Call C entry point */
    call _start

    /* Halt if _start returns */
2:
    cli
    hlt
    jmp 2b

/* ── Data ── */
.section .data.boot

.global boot_mb_info_ptr
.align 8
boot_mb_info_ptr:
    .quad 0     /* Multiboot info pointer (saved by _boot32) */

.global boot_mb_magic
.align 4
boot_mb_magic:
    .long 0     /* Multiboot bootloader magic */

/* ── GDT for 64-bit mode ── */
.align 16
gdt64:
    .quad 0                         /* Null descriptor */
    .quad 0x00AF9A000000FFFF        /* Code: L=1, D=0, P=1, DPL=0, Exec/Read */
    .quad 0x00CF92000000FFFF        /* Data: P=1, DPL=0, Read/Write */
gdt64_end:

gdt64_ptr:
    .short gdt64_end - gdt64 - 1   /* Limit */
    .long  gdt64                     /* Base (32-bit for initial lgdt) */
    .long  0                         /* Padding */
