# AlJefra OS — Boot Protocol

## Overview

Each architecture has a different boot path, but they all converge at the same point: `hal_init()` followed by `kernel_main()`.

## x86-64 Boot

```
BIOS/UEFI
  └→ Pure64 bootloader
       ├→ Enables long mode (64-bit)
       ├→ Sets up identity-mapped page tables (2MB pages)
       ├→ Detects memory via E820
       ├→ Initializes all APs via INIT/SIPI
       └→ Jumps to kernel at 0x100000

Kernel (kernel.asm)
  └→ Sets up function dispatch table at 0x100010
  └→ Initializes drivers, interrupts, timer
  └→ Loads payload from BMFS at 0x1E0000
  └→ Executes payload

Payload (our new C kernel)
  └→ Entry at 0x1E0000
  └→ hal_init() → kernel_main()
```

### x86-64 Memory Map
| Address | Size | Contents |
|---------|------|----------|
| 0x0000 - 0x7FFF | 32 KB | Real-mode IVT + BIOS data |
| 0x8000 - 0xFFFF | 32 KB | Pure64 bootloader |
| 0x100000 | 64 KB | AlJefra kernel |
| 0x1E0000 | ~1 MB | Payload (C kernel + drivers) |
| 0x800000 | 8 MB | Task stacks |
| 0x1000000 | varies | DMA buffers, page tables |

## AArch64 Boot

```
UEFI / U-Boot / Device Tree
  └→ arch/aarch64/boot.S
       ├→ Check exception level (EL2 or EL1)
       ├→ If EL2: configure HCR_EL2, drop to EL1
       ├→ Set up initial stack (64 KB)
       ├→ Zero BSS section
       └→ Call hal_init()

hal_init()
  └→ Console (PL011 UART at 0x09000000 on QEMU virt)
  └→ CPU (read MIDR_EL1, enable FP/NEON)
  └→ MMU (4KB granule, 48-bit VA identity map)
  └→ Interrupts (GICv2/v3 at device-tree addresses)
  └→ Timer (Generic Timer, CNTFRQ_EL0)
  └→ Bus (Device Tree + PCIe ECAM)
  └→ SMP (PSCI CPU_ON)

kernel_main()
  └→ Standard boot sequence
```

### AArch64 Memory Map (QEMU virt)
| Address | Contents |
|---------|----------|
| 0x00000000 | Flash (U-Boot/UEFI) |
| 0x08000000 | GICv2 Distributor |
| 0x08010000 | GICv2 CPU Interface |
| 0x09000000 | PL011 UART |
| 0x0A000000 | VirtIO devices |
| 0x10000000 | PCIe ECAM |
| 0x40000000 | Kernel load address |
| 0x40000000 + size | RAM |

## RISC-V 64 Boot

```
SBI (OpenSBI) / UEFI
  └→ arch/riscv64/boot.S
       ├→ Save hart ID (a0) and DTB pointer (a1)
       ├→ Set up stack
       ├→ Zero BSS
       └→ Call hal_init(hart_id, dtb_ptr)

hal_init()
  └→ Console (16550 UART or SBI ecall)
  └→ CPU (read misa for extensions)
  └→ MMU (Sv48 page tables)
  └→ Interrupts (PLIC at 0x0C000000, CLINT at 0x02000000)
  └→ Timer (rdtime CSR, timebase from DTB)
  └→ Bus (Device Tree)
  └→ SMP (SBI HSM hart_start)

kernel_main()
  └→ Standard boot sequence
```

### RISC-V Memory Map (QEMU virt)
| Address | Contents |
|---------|----------|
| 0x02000000 | CLINT (timer, IPI) |
| 0x0C000000 | PLIC |
| 0x10000000 | UART (16550) |
| 0x10001000 | VirtIO devices |
| 0x30000000 | PCIe ECAM |
| 0x80000000 | Kernel load address |
| 0x80000000 + size | RAM |

## Convergence Point

All architectures converge at `kernel_main()` in `kernel/main.c`. At this point:
- Console is working (can print messages)
- CPU is initialized (features detected)
- MMU is set up (identity mapping)
- Interrupts are configured
- Timer is running (nanosecond precision)
- Bus scan is ready
- SMP cores are discoverable

The rest of the boot (driver loading, networking, AI bootstrap) is entirely architecture-independent.

## QEMU Test Commands

### x86-64
```bash
qemu-system-x86_64 -machine q35 -smp 1 -cpu Westmere -m 256 \
  -drive file=aljefra_os.img,format=raw,if=none,id=disk0 \
  -device virtio-blk,drive=disk0 \
  -netdev tap,id=net0,ifname=tap0,script=no \
  -device virtio-net-pci,netdev=net0 \
  -serial stdio
```

### AArch64
```bash
qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 256 \
  -kernel kernel_aarch64.bin \
  -drive file=disk.img,format=raw,if=none,id=disk0 \
  -device virtio-blk-device,drive=disk0 \
  -netdev user,id=net0 \
  -device virtio-net-device,netdev=net0 \
  -serial stdio -nographic
```

### RISC-V 64
```bash
qemu-system-riscv64 -machine virt -m 256 \
  -kernel kernel_riscv64.bin \
  -drive file=disk.img,format=raw,if=none,id=disk0 \
  -device virtio-blk-device,drive=disk0 \
  -netdev user,id=net0 \
  -device virtio-net-device,netdev=net0 \
  -serial stdio -nographic
```
