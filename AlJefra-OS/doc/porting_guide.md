# AlJefra OS — Porting Guide

## Adding a New Architecture

This guide explains how to add support for a new CPU architecture to AlJefra OS.

## Prerequisites

You need:
- Cross-compiler for the target architecture (GCC or Clang)
- QEMU system emulator for the target
- Understanding of the target's: boot process, MMU, interrupt controller, timer, UART

## Steps

### 1. Create Architecture Directory

```bash
mkdir -p arch/<arch_name>/
```

### 2. Implement Required Files

Create these files, implementing all HAL functions:

| File | Purpose | Key Functions |
|------|---------|---------------|
| `boot.S` | Assembly entry point | CPU mode setup, stack init, BSS clear, call hal_init |
| `cpu.c` | CPU operations | hal_cpu_init, hal_cpu_halt, hal_cpu_cycles |
| `interrupt.c` | Interrupt controller | hal_irq_init, hal_irq_register, hal_irq_eoi |
| `timer.c` | System timer | hal_timer_init, hal_timer_ns, hal_timer_delay_us |
| `io.c` | Port I/O + DMA | hal_port_in/out (can be no-ops), hal_dma_alloc |
| `bus.c` | Device discovery | hal_bus_scan, hal_bus_pci_read32 |
| `mmu.c` | Page tables | hal_mmu_init, hal_mmu_map, hal_mmu_alloc_pages |
| `smp.c` | Multi-core | hal_smp_init, hal_smp_start_core, hal_spin_lock |
| `console.c` | UART output | hal_console_init, hal_console_putc, hal_console_printf |
| `hal_init.c` | Master init | hal_init() calls all sub-inits in order |
| `linker.ld` | Memory layout | .text, .rodata, .data, .bss sections |

### 3. Implement boot.S

The boot stub must:
1. Be the entry point (firmware jumps here)
2. Set up the CPU for C execution (stack, mode, FPU)
3. Zero the BSS section
4. Call `hal_init()`
5. Call `kernel_main()`

Example structure:
```asm
.section .text.entry
.global _start
_start:
    /* Architecture-specific CPU setup */
    /* Set stack pointer */
    /* Zero BSS */
    /* Call hal_init */
    /* Call kernel_main */
    /* Infinite halt loop */
```

### 4. Add to Build System

Edit `Makefile`:
```makefile
ifeq ($(ARCH),new_arch)
    CC = new_arch-elf-gcc
    AS = new_arch-elf-as
    LD = new_arch-elf-ld
    ARCH_DIR = arch/new_arch
    CFLAGS += -march=...
endif
```

### 5. Test in QEMU

```bash
make ARCH=new_arch
qemu-system-new_arch -machine virt -kernel kernel.bin -serial stdio -nographic
```

Expected output:
```
==============================================
  AlJefra OS — Universal Boot
  Architecture: NewArch
  CPU: ...
  Cores: 1
  RAM: 256 MB
==============================================

[kernel] Scanning buses...
[kernel] Found N devices
...
```

### 6. Verify All HAL Functions

Test each HAL subsystem:
- [ ] Console output works (you can see boot messages)
- [ ] CPU info is detected correctly
- [ ] Timer returns monotonically increasing nanoseconds
- [ ] Memory allocation works (hal_mmu_alloc_pages)
- [ ] Bus scan finds devices (at minimum VirtIO on QEMU)
- [ ] Interrupts fire and are handled
- [ ] SMP cores can be started
- [ ] A driver initializes successfully (e.g., VirtIO-Blk)
- [ ] Network works (VirtIO-Net TX/RX)
- [ ] DHCP obtains an IP address
- [ ] TLS handshake succeeds
- [ ] AI bootstrap completes

## Common Pitfalls

1. **Memory barriers**: Each architecture has different memory ordering. Make sure `hal_mmio_barrier()` uses the correct fence instruction.

2. **Cache coherency**: DMA buffers may need cache invalidation/flush on non-x86 architectures. Use appropriate barriers.

3. **Endianness**: All HAL structures use little-endian. ARM and RISC-V can be big-endian; ensure your port runs in LE mode.

4. **Alignment**: Some architectures fault on unaligned access. Packed structs in drivers may need alignment wrappers.

5. **Address space**: Identity mapping simplifies things but may not be possible with all firmware. Document any address translation.

## Architecture Reference

### AArch64

| Feature | Implementation |
|---------|---------------|
| Entry | UEFI or flat binary at 0x40000000 |
| CPU mode | EL1, AArch64, LE |
| MMU | TTBR0_EL1, 4KB granule, 48-bit VA |
| Interrupts | GICv2 (GICD + GICC) or GICv3 |
| Timer | CNTVCT_EL0, CNTFRQ_EL0 |
| SMP | PSCI CPU_ON via SMC/HVC |
| UART | PL011 (QEMU virt: 0x09000000) |
| Barrier | DMB ISH / DSB SY |

### RISC-V 64

| Feature | Implementation |
|---------|---------------|
| Entry | SBI hands off at 0x80000000, DTB in a1 |
| CPU mode | S-mode (supervisor) |
| MMU | Sv48 page tables |
| Interrupts | PLIC (0x0C000000) + CLINT (0x02000000) |
| Timer | rdtime CSR, timebase from DTB |
| SMP | SBI HSM hart_start |
| UART | 16550 (QEMU virt: 0x10000000) or SBI console |
| Barrier | FENCE instruction |
