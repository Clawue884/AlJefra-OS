# AlJefra OS v1.0

**The AI-Native Operating System — Boot on any device. AI downloads the rest.**

AlJefra OS is a universal boot operating system that runs on x86-64, ARM64, and RISC-V architectures. It boots with a minimal kernel, connects to the network, and uses AI to automatically detect hardware and download the right drivers from the AlJefra Store.

> **Note:** AlJefra OS is under active development. Some hardware configurations may not work correctly yet.

## Key Features

- **Universal Boot**: Single kernel design boots on x86-64, ARM64 (Cortex-A72+), and RISC-V 64-bit
- **AI Bootstrap**: Boots → network → AI agent detects hardware → downloads drivers automatically
- **Driver Marketplace**: Ed25519-signed `.ajdrv` driver packages downloaded at runtime
- **Hardware Abstraction Layer**: Clean HAL separates arch-specific code from portable drivers
- **14 Portable Drivers**: NVMe, AHCI, VirtIO, eMMC, e1000, WiFi, USB, serial, framebuffer, PCIe
- **Full Network Stack**: TCP/IP, DHCP, TLS (BearSSL), HTTP/1.1, DNS
- **GPU Acceleration**: NVIDIA driver + evolution framework
- **Exokernel Design**: Direct hardware access with minimal overhead

## Architecture

```
┌─────────────────────────────────────────┐
│           Applications / AI Agent       │
├─────────────────────────────────────────┤
│        Kernel Core (C, portable)        │
│   scheduler · syscalls · driver loader  │
├─────────────────────────────────────────┤
│    HAL — Hardware Abstraction Layer     │
├────────────┬────────────┬───────────────┤
│  x86-64    │  AArch64   │   RISC-V 64   │
│  boot.S    │  boot.S    │   boot.S      │
│  cpu/irq   │  GIC/timer │   PLIC/SBI    │
│  APIC/HPET │  PSCI/MMU  │   Sv39/Sv48   │
└────────────┴────────────┴───────────────┘
```

## Quick Start

### Build from Source

```bash
# Prerequisites (Debian/Ubuntu)
sudo apt install nasm gcc make \
  aarch64-linux-gnu-gcc riscv64-linux-gnu-gcc

# Build for all architectures
make all-arch

# Or build for a specific architecture
make ARCH=x86_64
make ARCH=aarch64
make ARCH=riscv64
```

### Run on QEMU

```bash
# x86-64
qemu-system-x86_64 -machine q35 -cpu Westmere -m 256 -smp 1 \
  -kernel build/x86_64/bin/kernel_x86_64.bin -nographic

# ARM64
qemu-system-aarch64 -M virt -cpu cortex-a72 -m 256 -nographic \
  -kernel build/aarch64/bin/kernel_aarch64.bin

# RISC-V
qemu-system-riscv64 -M virt -m 256 -nographic \
  -kernel build/riscv64/bin/kernel_riscv64.bin
```

### Boot on Real Hardware (USB)

See the [Downloads](https://os.aljefra.com/download.html) page for bootable USB images and flashing instructions.

## Project Structure

```
AlJefra-OS/
├── arch/           # Architecture-specific code (x86_64, aarch64, riscv64)
├── hal/            # Hardware Abstraction Layer headers
├── kernel/         # Portable kernel core (scheduler, syscalls, driver loader)
├── drivers/        # 14 portable C drivers (storage, network, input, display, bus)
├── net/            # Network stack (TCP/IP, DHCP)
├── ai/             # AI agent + marketplace client
├── store/          # Driver package verification and installation
├── server/         # Marketplace REST API server
├── aljefra/        # Original x86-64 assembly kernel
├── doc/            # Documentation
├── evolution/      # Kernel evolution framework
├── website/        # os.aljefra.com website
└── Makefile        # Multi-arch build system
```

## Documentation

- [Architecture Overview](doc/architecture.md)
- [HAL Specification](doc/hal_spec.md)
- [Driver Development Guide](doc/driver_guide.md)
- [Boot Protocol](doc/boot_protocol.md)
- [Marketplace API](doc/marketplace_spec.md)
- [Security Model](doc/security_model.md)
- [Porting Guide](doc/porting_guide.md)
- [Memory Maps](doc/memory_maps.md)

## Verified Platforms

| Architecture | Platform | Status |
|-------------|----------|--------|
| x86-64 | QEMU (Westmere) | Boots, NVMe+USB+VirtIO+Marketplace |
| ARM64 | QEMU (Cortex-A72) | Boots, VirtIO+DHCP+Marketplace |
| RISC-V 64 | QEMU (rv64gc) | Boots, VirtIO+DHCP+Marketplace |

## License

MIT License. Copyright (c) 2025 AlJefra.

## Links

- Website: [os.aljefra.com](https://os.aljefra.com)
- Source: [git.sidracode.com](https://git.sidracode.com)
