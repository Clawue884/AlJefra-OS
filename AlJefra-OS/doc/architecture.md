# AlJefra OS — Multi-Architecture Overview

## Vision

AlJefra OS is a self-evolving exokernel that boots on **any device** — desktops, laptops, tablets, SBCs, servers — across x86-64, ARM64, and RISC-V architectures. The core insight: we only need enough built-in drivers to get network connectivity, then AI handles everything else by downloading drivers from the marketplace.

## Architecture Layers

```
┌─────────────────────────────────────────────────┐
│  Applications / AI Agent                         │
│  (C, architecture-independent)                   │
├─────────────────────────────────────────────────┤
│  Kernel Core                                     │
│  main.c, sched.c, syscall.c, driver_loader.c    │
│  ai_bootstrap.c                                  │
├─────────────────────────────────────────────────┤
│  Network Stack          │  Driver Store          │
│  TCP/IP, TLS, HTTP,     │  package.h, verify.c,  │
│  DHCP, DNS              │  install.c, catalog.c  │
├─────────────────────────────────────────────────┤
│  Portable Drivers (C)                            │
│  NVMe, AHCI, e1000, VirtIO, xHCI, USB HID,     │
│  PCIe, Device Tree, ACPI, eMMC                   │
├─────────────────────────────────────────────────┤
│  Hardware Abstraction Layer (HAL)                │
│  cpu.h, interrupt.h, timer.h, bus.h, io.h,      │
│  mmu.h, smp.h, console.h                        │
├──────────┬──────────┬───────────────────────────┤
│ x86-64   │ AArch64  │ RISC-V 64                 │
│ arch/    │ arch/    │ arch/                      │
│ x86_64/  │ aarch64/ │ riscv64/                  │
└──────────┴──────────┴───────────────────────────┘
```

## Boot Sequence

```
1. Firmware (BIOS/UEFI/SBI/U-Boot)
   └→ Loads bootloader (Pure64 on x86, or arch-specific)

2. Arch Boot Stub (arch/<arch>/boot.S)
   └→ CPU mode setup, MMU init, stack setup
   └→ Calls hal_init()

3. HAL Init (arch/<arch>/hal_init.c)
   └→ Console → CPU → MMU → Interrupts → Timer → Bus → SMP

4. Kernel Main (kernel/main.c)
   └→ Banner → Bus Scan → Built-in Drivers → Scheduler → Network

5. AI Bootstrap (kernel/ai_bootstrap.c)
   └→ Build HW manifest → DHCP → Connect to marketplace
   └→ Send manifest → Download drivers → Load & init

6. System Ready
   └→ All hardware operational, interactive mode
```

## Key Design Principles

### 1. HAL Abstraction
All architecture-specific code lives in `arch/<arch>/`. Everything above the HAL is pure portable C. Drivers never use inline assembly — they call HAL functions.

### 2. Minimal Boot Drivers
The kernel only needs: CPU init + UART + one NIC driver + TCP/IP. Everything else can be downloaded. This minimizes the code that must be ported per architecture.

### 3. Driver Hot-Loading
Drivers can be loaded at runtime from `.ajdrv` packages. The driver loader handles binary relocation and initialization. Drivers are signed with Ed25519 for security.

### 4. AI-First Configuration
Instead of manual configuration, the OS sends a hardware manifest to the AlJefra marketplace API. The AI analyzes the hardware and returns the optimal driver set. This means zero configuration for the user.

### 5. Exokernel Philosophy
The kernel provides minimal abstractions. Applications get near-hardware access through the HAL and syscall interface. This enables maximum performance and flexibility.

## Directory Structure

```
AlJefra-OS/
├── hal/            # HAL C headers (arch-independent interface)
├── arch/           # Architecture-specific implementations
│   ├── x86_64/     # Intel/AMD 64-bit
│   ├── aarch64/    # ARM 64-bit
│   └── riscv64/    # RISC-V 64-bit
├── kernel/         # Arch-independent kernel core
├── drivers/        # Portable C drivers
│   ├── storage/    # NVMe, AHCI, VirtIO-Blk, eMMC
│   ├── network/    # e1000, VirtIO-Net, WiFi
│   ├── input/      # xHCI, USB HID, PS/2
│   ├── display/    # Framebuffer, serial console
│   ├── gpu/        # NVIDIA GPU
│   └── bus/        # PCIe, Device Tree, ACPI
├── net/            # Network stack (TCP/IP, TLS, HTTP, DHCP)
├── ai/             # AI agent and marketplace client
├── store/          # Driver package format, verification, catalog
├── doc/            # Documentation
└── src/            # Original AlJefra kernel (x86-64 assembly)
```

## Supported Architectures

| Architecture | Status | Boot Method | Interrupt Controller | Timer |
|-------------|--------|-------------|---------------------|-------|
| x86-64 | Production | BIOS/UEFI via Pure64 | APIC/IO-APIC | HPET/KVM |
| AArch64 | Development | UEFI/DTB | GICv2/v3 | Generic Timer |
| RISC-V 64 | Planned | SBI/UEFI | PLIC/CLINT | mtime CSR |

## Driver Model

Drivers implement the `driver_ops_t` interface:

```c
typedef struct {
    const char       *name;
    driver_category_t category;
    hal_status_t    (*init)(hal_device_t *dev);
    void            (*shutdown)(void);
    /* Category-specific ops: read/write, net_tx/rx, input_poll */
} driver_ops_t;
```

Built-in drivers are compiled into the kernel image. Runtime drivers come from `.ajdrv` packages downloaded via the marketplace.

## AI Bootstrap Flow

```
Device boots → HAL init → Bus scan → Find NIC →
  Load NIC driver → DHCP → TCP/IP + TLS →
  POST /v1/manifest (hardware list) →
  Marketplace AI analyzes hardware →
  Returns optimal driver set →
  Download signed .ajdrv packages →
  Verify Ed25519 signatures →
  Load & initialize each driver →
  System fully operational
```
