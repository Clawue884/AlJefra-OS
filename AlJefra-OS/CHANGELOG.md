# Changelog

All notable changes to AlJefra OS are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.0.0] - 2026-02-27

### Added

- **Core Kernel:** x86-64 assembly kernel (20 KB, 9,102 lines of ASM) with
  SMP scheduler, syscall interface, and 64 KB extended kernel support.
- **Multi-Architecture Support:** Hardware Abstraction Layer (HAL) with 9
  header interfaces enabling portable code across x86-64, ARM64 (Cortex-A72,
  GIC, Generic Timer, MMU), and RISC-V 64-bit (OpenSBI, PLIC, CLINT, Sv39).
- **22+ Portable C Drivers:** NVMe, AHCI, VirtIO-Blk, VirtIO-Net, eMMC, UFS,
  Intel e1000, RTL8169, Intel WiFi (AX200/AX210), Broadcom WiFi (RPi), xHCI
  USB 3.0, USB HID, PS/2, touchscreen, serial console, framebuffer, PCIe bus
  enumeration, Device Tree parser, and ACPI lite.
- **Full Networking Stack:** TCP/IP (ARP, IPv4, ICMP, UDP, TCP client), DNS
  resolver, DHCP client, TLS 1.2 via BearSSL, HTTP/1.1 client with chunked
  transfer encoding.
- **AI-Native Chat Interface:** Claude API integration with interactive REPL,
  SAX-style JSON parser, and HTTP client for AI communication.
- **Driver Marketplace:** Flask REST API (9 endpoints) with .ajdrv package
  format, runtime driver loading, Ed25519 signature verification (full RFC 8032
  implementation, 1,550 lines), OTA update system with CRC32 verification.
- **Self-Evolving Kernel:** 11 evolution generations with 200+ optimizations
  across 25 kernel files. Dual experiment system: AI-directed source
  optimization (Experiment A) and GPU-accelerated binary evolution
  (Experiment B, 4,032 lines of C).
- **GPU Compute:** NVIDIA RTX 5090 driver (951 lines ASM) with VRAM management,
  compute dispatch, fence synchronization, and AI scheduler.
- **Desktop GUI System:** Window manager, widget toolkit, mouse cursor, desktop
  shell, file browser, AI chat window, settings panel, and theme engine
  (planned as downloadable .ajdrv plugin).
- **Security:** Ed25519 code signing for all driver packages, TLS for all
  external connections, trust chain (root key, store key, package signatures).
- **Build System:** `make ARCH=x86_64|aarch64|riscv64` producing compact
  binaries (x86_64: 147 KB, aarch64: 153 KB, riscv64: 129 KB).
- **Website:** Static site for os.aljefra.com (20 files, 324 KB) with
  documentation, architecture overview, marketplace, and download pages.
- **Bootable USB:** GRUB2-based ISO image for bare-metal x86-64 boot.

### Architecture Verification

- x86-64: Full standalone boot with Multiboot1, 8 PCIe devices, DHCP,
  marketplace, runtime .ajdrv loading. Boot time under 15 seconds.
- ARM64: QEMU boot verified (Cortex-A72, GIC, ARM Generic Timer, MMU,
  4 devices, marketplace integration).
- RISC-V: QEMU boot verified (OpenSBI, Sv39 MMU, 12 devices, kernel_main).

---

*AlJefra OS -- The First Qatari Operating System*
*Built in Qatar. Built for the world.*
*Qatar IT -- www.QatarIT.com*
