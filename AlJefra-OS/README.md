<p align="center">
  <img src="images/AlJefra OS Logo.svg" alt="AlJefra OS" width="400">
</p>

<h1 align="center">AlJefra OS</h1>

<p align="center">
  <strong>The First Qatari Operating System -- AI-Native, Self-Evolving</strong>
</p>

<p align="center">
  <a href="https://github.com/qatarit-com/AlJefra-OS/actions"><img src="https://img.shields.io/github/actions/workflow/status/qatarit-com/AlJefra-OS/build.yml?branch=main&label=build" alt="Build Status"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License: MIT"></a>
  <a href="#supported-architectures"><img src="https://img.shields.io/badge/arch-x86--64%20%7C%20ARM64%20%7C%20RISC--V%2064-green.svg" alt="Architectures"></a>
  <a href="#"><img src="https://img.shields.io/badge/lines%20of%20code-61%2C664-orange.svg" alt="Lines of Code"></a>
  <a href="#"><img src="https://img.shields.io/badge/drivers-22%2B-purple.svg" alt="Drivers"></a>
  <a href="https://os.aljefra.com"><img src="https://img.shields.io/badge/website-os.aljefra.com-blue" alt="Website"></a>
</p>

<p align="center"><em>Boot on any device. AI downloads the rest.</em></p>

---

## Overview

AlJefra OS v1.0 is a ground-up operating system written in **61,664 lines of original C and Assembly** (plus 101,889 lines of vendored BearSSL for TLS). It boots with a minimal kernel on any supported architecture, then uses an onboard AI agent to detect hardware, connect to the AlJefra Driver Marketplace, and automatically download, install, and configure every driver the machine needs -- all without human intervention.

A single portable codebase compiles for all three supported architectures.

## Key Features

- **Universal Boot** -- One codebase compiles for x86-64, ARM64, and RISC-V 64, producing architecture-specific kernel ELFs.
- **AI Bootstrap** -- An embedded AI agent scans hardware at boot, builds a manifest, and fetches matching drivers from the cloud.
- **Driver Marketplace** -- Over-the-air driver store where vendors publish signed `.ajdrv` packages that the OS downloads at runtime.
- **Hardware Abstraction Layer (HAL)** -- A clean, portable HAL lets every driver and kernel subsystem compile once and run on all three architectures.
- **22 Portable Drivers** -- Storage (NVMe, AHCI, VirtIO-blk), networking (e1000, VirtIO-net, RTL8169), display (Bochs VBE, serial console), input (PS/2, USB HID), and more.
- **Full Network Stack** -- Ethernet, ARP, IPv4, UDP, DHCP, DNS, and TCP. TLS 1.2 provided by vendored BearSSL library.
- **GPU & GUI Desktop** -- Framebuffer-based GPU engine, window manager, desktop shell, and graphical applications.
- **Hot-Load Drivers** -- The kernel can load new `.ajdrv` driver packages at runtime without a restart.
- **Self-Evolving Kernel** -- AI-directed source optimization and GPU-accelerated genetic algorithms for kernel improvement (52 optimizations across 10 generations).
- **BMFS Filesystem** -- BareMetal File System support for persistent storage.
- **AI Chat Interface** -- Built-in NLP chat engine with pattern-matching for English and Arabic, mapping natural language to 19 system actions. External LLM backends supported when online.
- **Bilingual Interface** -- Full Arabic and English language support throughout the UI.

## Architecture

```
+----------------------------------------------------------+
|                    Applications                          |
|        GUI Desktop  |  AI Chat  |  User Programs         |
+----------------------------------------------------------+
|                    AI Agent                               |
|     Hardware Detection | Manifest Builder | Store Client |
+----------------------------------------------------------+
|                   Kernel Core                             |
|   Scheduler | Memory Manager | VFS | IPC | Syscalls      |
+----------------------------------------------------------+
|              Network Stack & Store Client                 |
|     Ethernet | ARP | IPv4 | UDP/TCP | DHCP | DNS         |
+----------------------------------------------------------+
|                 Portable Drivers (22+)                    |
|  NVMe | AHCI | e1000 | VirtIO | USB | GPU | PS/2 | ...  |
+----------------------------------------------------------+
|            Hardware Abstraction Layer (HAL)               |
|  CPU | IRQ | Timer | MMU | Bus | I/O | SMP | Console     |
+----------------------------------------------------------+
|             Architecture-Specific Code                    |
|        x86-64       |     ARM64      |    RISC-V 64      |
|    APIC, HPET,      |   GIC, Generic |  PLIC, CLINT,     |
|    4-level paging    |   Timer, Sv48  |  Sv39             |
+----------------------------------------------------------+
|                     Hardware                              |
+----------------------------------------------------------+
```

See [doc/architecture.md](doc/architecture.md) for the full design document.

## Quick Start

### Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| GCC cross-compilers | 12+ | `x86_64-elf-gcc`, `aarch64-none-elf-gcc`, `riscv64-unknown-elf-gcc` |
| NASM | 2.15+ | x86-64 assembly |
| GNU Make | 4.3+ | Build system |
| QEMU | 7.0+ | Emulation and testing |
| `wget` or `curl` | any | AI bootstrap network tests |

On Debian/Ubuntu:

```bash
sudo apt install build-essential nasm qemu-system-x86 qemu-system-arm \
  qemu-system-misc gcc-aarch64-linux-gnu gcc-riscv64-linux-gnu
```

### Build from Source

```bash
git clone https://github.com/qatarit-com/AlJefra-OS.git
cd AlJefra-OS
```

**x86-64:**

```bash
make ARCH=x86_64          # produces build/x86_64/kernel.elf
```

**ARM64:**

```bash
make ARCH=aarch64         # produces build/aarch64/kernel.elf
```

**RISC-V 64:**

```bash
make ARCH=riscv64         # produces build/riscv64/kernel.elf
```

**All architectures:**

```bash
make all                  # builds all three images
```

### Run in QEMU

**x86-64:**

```bash
qemu-system-x86_64 \
  -drive file=build/x86_64/kernel.elf,format=raw \
  -m 256M \
  -smp 2 \
  -device e1000,netdev=net0 \
  -netdev user,id=net0 \
  -serial stdio
```

**ARM64:**

```bash
qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a72 \
  -drive file=build/aarch64/kernel.elf,format=raw \
  -m 256M \
  -smp 2 \
  -device virtio-net-device,netdev=net0 \
  -netdev user,id=net0 \
  -nographic
```

**RISC-V 64:**

```bash
qemu-system-riscv64 \
  -M virt \
  -drive file=build/riscv64/kernel.elf,format=raw \
  -m 256M \
  -smp 2 \
  -device virtio-net-device,netdev=net0 \
  -netdev user,id=net0 \
  -nographic
```

### USB Boot (Pre-Built Images)

Download the latest pre-built image from **[os.aljefra.com](https://os.aljefra.com)** and write it to a USB drive:

```bash
# Linux / macOS
sudo dd if=aljefra_os_v1.0.iso of=/dev/sdX bs=4M status=progress
sync
```

Boot from the USB drive via your BIOS/UEFI boot menu.

## Project Structure

```
AlJefra-OS/
|-- Makefile                  Root build system
|-- aljefra.sh                Build helper script
|-- arch/                     Architecture-specific code
|   |-- x86_64/               x86-64 boot, HAL impl, linker scripts
|   |-- aarch64/              ARM64 boot, HAL impl, linker scripts
|   +-- riscv64/              RISC-V 64 boot, HAL impl, linker scripts
|-- hal/                      Hardware Abstraction Layer headers
|   |-- hal.h                 Master HAL header
|   |-- cpu.h                 CPU operations
|   |-- interrupt.h           IRQ management
|   |-- timer.h               System timer
|   |-- bus.h                 Bus enumeration (PCI/MMIO)
|   |-- io.h                  MMIO and port I/O
|   |-- mmu.h                 Virtual memory
|   |-- smp.h                 Multi-core / spinlocks
|   +-- console.h             Early console output
|-- kernel/                   Core kernel
|   |-- main.c                Kernel entry point
|   |-- sched.c               Process scheduler
|   |-- mm.c                  Memory manager
|   |-- vfs.c                 Virtual filesystem
|   +-- syscall.c             System call interface
|-- drivers/                  Portable device drivers (22+)
|   |-- storage/              NVMe, AHCI, VirtIO-blk, ...
|   |-- network/              e1000, VirtIO-net, RTL8169, WiFi, ...
|   |-- display/              Bochs VBE (QEMU VGA), Serial Console
|   |-- input/                PS/2 keyboard/mouse, USB HID
|   +-- bus/                  PCI, USB host controllers
|-- net/                      Network stack
|   |-- ethernet.c            Ethernet framing
|   |-- arp.c                 ARP
|   |-- ipv4.c                IPv4
|   |-- udp.c                 UDP
|   |-- tcp.c                 TCP
|   |-- dhcp.c                DHCP client
|   +-- dns.c                 DNS resolver
|-- ai/                       AI subsystem
|   |-- bootstrap.c           AI hardware bootstrap agent
|   |-- manifest.c            Hardware manifest builder
|   +-- chat.c                AI chat interface
|-- store/                    Driver marketplace client
|-- gpu_engine/               GPU rendering engine
|-- gui/                      Windowing system and desktop shell
|-- programs/                 User-space applications
|-- lib/                      Shared libraries (libc, libm, ...)
|-- aljefra/                  AlJefra-specific system utilities
|-- evolution/                Self-evolution subsystem
|-- build/                    Build output directory
|-- test/                     Test suites
|-- server/                   Marketplace server reference impl
|-- images/                   Logos, screenshots, assets
|-- doc/                      Documentation
|   |-- architecture.md       System architecture
|   |-- boot_protocol.md      Boot sequence for each arch
|   |-- hal_spec.md           HAL API specification
|   |-- driver_guide.md       How to write drivers
|   |-- marketplace_spec.md   Driver store protocol
|   |-- memory_maps.md        Memory layouts per arch
|   |-- security_model.md     Security design
|   |-- porting_guide.md      Adding a new architecture
|   |-- developer_guide.md    Developer onboarding
|   |-- plugin-sdk.md         Plugin SDK reference
|   |-- hardware-compatibility.md  Tested hardware list
|   +-- release-process.md    Release workflow
|-- CONTRIBUTING.md           Contribution guidelines
|-- CODE_OF_CONDUCT.md        Community code of conduct
|-- CHANGELOG.md              Release changelog
|-- ROADMAP.md                Project roadmap
+-- LICENSE                   MIT License
```

## Documentation

| Document | Description |
|----------|-------------|
| [Architecture](doc/architecture.md) | System design, layer model, and design principles |
| [Boot Protocol](doc/boot_protocol.md) | Boot sequence for x86-64, ARM64, and RISC-V 64 |
| [HAL Specification](doc/hal_spec.md) | Complete Hardware Abstraction Layer API reference |
| [Driver Guide](doc/driver_guide.md) | How to write and publish AlJefra drivers |
| [Marketplace Spec](doc/marketplace_spec.md) | Driver store protocol and `.ajdrv` format |
| [Memory Maps](doc/memory_maps.md) | Physical and virtual memory layouts |
| [Security Model](doc/security_model.md) | Security architecture and threat model |
| [Porting Guide](doc/porting_guide.md) | Adding support for a new CPU architecture |
| [Developer Guide](doc/developer_guide.md) | Getting started for contributors |
| [Plugin SDK](doc/plugin-sdk.md) | Plugin development reference |
| [Hardware Compatibility](doc/hardware-compatibility.md) | Tested hardware and peripherals |
| [Release Process](doc/release-process.md) | How releases are built and published |

## Verified Platforms

| Platform | Architecture | Status | Notes |
|----------|-------------|--------|-------|
| QEMU `q35` | x86-64 | Verified | Primary development target |
| QEMU `virt` | ARM64 | Verified | Cortex-A72 profile |
| QEMU `virt` | RISC-V 64 | Verified | With OpenSBI firmware |
| Intel NUC (10th gen) | x86-64 | Verified | Bare-metal USB boot |
| Raspberry Pi 4 | ARM64 | Verified | Via UEFI firmware |
| SiFive HiFive Unmatched | RISC-V 64 | Verified | Via SBI |
| Generic x86-64 PC | x86-64 | Verified | BIOS and UEFI |
| VirtualBox | x86-64 | Verified | Raw disk image |
| VMware Workstation | x86-64 | Verified | Raw disk image |

## System Requirements

| Resource | Minimum | Recommended |
|----------|---------|-------------|
| RAM | 256 MB | 512 MB |
| Storage | 16 MB | 128 MB |
| CPU Cores | 1 | 2+ |
| Network | Ethernet (for AI bootstrap) | Gigabit Ethernet |
| Display | Optional (serial console) | 1024x768 framebuffer |

## Contributing

We welcome contributions from the global open-source community.

Please read **[CONTRIBUTING.md](CONTRIBUTING.md)** for guidelines on:

- Reporting bugs and requesting features
- Setting up a development environment
- Code style and commit conventions
- Submitting pull requests
- Driver contribution process

All contributors must follow our **[Code of Conduct](CODE_OF_CONDUCT.md)**.

## Links

- **Website:** [os.aljefra.com](https://os.aljefra.com)
- **GitHub:** [github.com/qatarit-com/AlJefra-OS](https://github.com/qatarit-com/AlJefra-OS)
- **Issues:** [GitHub Issues](https://github.com/qatarit-com/AlJefra-OS/issues)
- **Discussions:** [GitHub Discussions](https://github.com/qatarit-com/AlJefra-OS/discussions)
- **Developer:** [Qatar IT](https://www.qatarit.com)

## License

AlJefra OS is released under the **MIT License**.

Copyright (c) 2025 AlJefra

See [LICENSE](LICENSE) for full text.

---

<p align="center">
  <strong>Built in Qatar</strong><br>
  Developed by <a href="https://www.qatarit.com">Qatar IT</a>
</p>
