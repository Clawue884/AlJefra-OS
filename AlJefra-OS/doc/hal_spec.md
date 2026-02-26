# AlJefra OS — HAL Interface Specification

## Overview

The Hardware Abstraction Layer (HAL) provides architecture-independent C interfaces for all hardware operations. Code above the HAL never uses inline assembly or architecture-specific features.

## Headers

| Header | Purpose |
|--------|---------|
| `hal/hal.h` | Master include, common types, `hal_init()` |
| `hal/cpu.h` | CPU detection, control, barriers |
| `hal/interrupt.h` | IRQ registration, enable/disable, EOI |
| `hal/timer.h` | Monotonic clock, delays, one-shot timer |
| `hal/bus.h` | Device discovery (PCIe/DT/ACPI) |
| `hal/io.h` | MMIO read/write, port I/O, DMA allocation |
| `hal/mmu.h` | Page tables, memory mapping, physical allocator |
| `hal/smp.h` | Multi-core management, spinlocks |
| `hal/console.h` | Early text output (UART/VGA/framebuffer) |

## Common Types

```c
typedef enum {
    HAL_OK, HAL_ERROR, HAL_TIMEOUT, HAL_BUSY,
    HAL_NO_DEVICE, HAL_NO_MEMORY, HAL_NOT_SUPPORTED
} hal_status_t;

typedef enum {
    HAL_ARCH_X86_64, HAL_ARCH_AARCH64, HAL_ARCH_RISCV64
} hal_arch_t;
```

## CPU Interface (`hal/cpu.h`)

### `hal_cpu_init(void) → hal_status_t`
Initialize the boot processor. Detect features, enable FPU/SIMD.

### `hal_cpu_get_info(hal_cpu_info_t *info)`
Fill structure with vendor string, model name, feature flags, core counts, cache line size.

### `hal_cpu_id() → uint64_t`
Return the current core's ID (APIC ID on x86, MPIDR on ARM, mhartid on RISC-V).

### `hal_cpu_halt()`
Low-power wait until next interrupt (HLT/WFI/WFI).

### `hal_cpu_enable_interrupts()` / `hal_cpu_disable_interrupts()`
Enable/disable interrupts on the current core (STI/CLI, DAIFClr/DAIFSet, sstatus.SIE).

### `hal_cpu_cycles() → uint64_t`
Read the cycle counter (TSC/CNTVCT/rdcycle).

### `hal_cpu_random() → uint64_t`
Hardware random number (RDRAND/RNDR). Returns 0 if unsupported.

## Interrupt Interface (`hal/interrupt.h`)

### `hal_irq_init() → hal_status_t`
Initialize the interrupt controller (APIC/GIC/PLIC).

### `hal_irq_register(irq, handler, ctx) → hal_status_t`
Register a handler for IRQ number `irq`. Handler signature: `void handler(uint32_t irq, void *ctx)`.

### `hal_irq_eoi(irq)`
Signal end-of-interrupt to the controller.

### `hal_exception_register(vector, handler) → hal_status_t`
Register an exception/fault handler. Handler receives vector number, error code, and faulting address.

## Timer Interface (`hal/timer.h`)

### `hal_timer_ns() → uint64_t`
Nanoseconds since boot. Monotonic, never wraps.

### `hal_timer_delay_us(us)`
Busy-wait for the specified microseconds. Uses `hal_timer_ns()` internally.

### `hal_timer_arm(ns, callback, ctx) → hal_status_t`
Arm a one-shot timer. Only one may be active at a time.

## Bus Interface (`hal/bus.h`)

### `hal_bus_scan(devs, max) → uint32_t`
Scan all buses and fill an array of `hal_device_t` structs. Returns device count.

```c
typedef struct {
    hal_bus_type_t bus_type;    // PCIE, DT, ACPI, MMIO
    uint16_t vendor_id, device_id;
    uint8_t  class_code, subclass, prog_if;
    uint8_t  irq;
    uint64_t bar[6], bar_size[6];
    uint16_t bus; uint8_t dev, func;
    char     compatible[64];    // Device Tree compatible string
} hal_device_t;
```

### `hal_bus_pci_read32(bdf, reg) → uint32_t`
Read PCI configuration space. `bdf = bus<<8 | dev<<3 | func`.

### `hal_bus_pci_enable(dev)`
Set bus-master and memory-space bits in PCI command register.

## I/O Interface (`hal/io.h`)

### MMIO (inline functions)
```c
hal_mmio_read8/16/32/64(addr)
hal_mmio_write8/16/32/64(addr, val)
hal_mmio_barrier()
```
All include compiler barriers. `hal_mmio_barrier()` issues a full memory fence.

### Port I/O (x86-64 only)
```c
hal_port_in8/16/32(port)
hal_port_out8/16/32(port, val)
```
No-ops on ARM and RISC-V (these architectures don't have port I/O).

### DMA
```c
void *hal_dma_alloc(size, &phys)  // Allocate physically-contiguous buffer
void  hal_dma_free(virt, size)    // Free DMA buffer
```

## MMU Interface (`hal/mmu.h`)

### `hal_mmu_map(virt, phys, size, perms, type) → hal_status_t`
Map physical pages. Permissions: `HAL_PAGE_READ|WRITE|EXEC|USER`. Types: `NORMAL`, `DEVICE`, `WC`.

### `hal_mmu_alloc_pages(count) → uint64_t`
Allocate contiguous physical pages. Returns physical address (0 on failure).

### `hal_mmu_total_ram() → uint64_t` / `hal_mmu_free_ram() → uint64_t`
Query total and free RAM in bytes.

## SMP Interface (`hal/smp.h`)

### `hal_smp_start_core(core_id, fn, arg) → hal_status_t`
Start an application processor running `fn(arg)`.

### Spinlocks
```c
hal_spin_lock(&lock)    // Acquire (blocking)
hal_spin_unlock(&lock)  // Release
hal_spin_trylock(&lock) // Non-blocking attempt, returns 1 on success
```

## Console Interface (`hal/console.h`)

### `hal_console_puts(str)` / `hal_console_printf(fmt, ...)`
Early text output. Supports `%s %d %u %x %p %%` format specifiers.

### `hal_console_getc() → char`
Blocking character read.

## Implementation Requirements

Each architecture must provide implementations for all HAL functions in `arch/<arch>/`:

| File | Implements |
|------|-----------|
| `cpu.c` | `hal/cpu.h` functions |
| `interrupt.c` | `hal/interrupt.h` functions |
| `timer.c` | `hal/timer.h` functions |
| `bus.c` | `hal/bus.h` functions |
| `io.c` | `hal/io.h` port I/O + DMA |
| `mmu.c` | `hal/mmu.h` functions |
| `smp.c` | `hal/smp.h` functions |
| `console.c` | `hal/console.h` functions |
| `hal_init.c` | `hal_init()` master init |
| `boot.S` | Assembly entry point |
| `linker.ld` | Memory layout |
