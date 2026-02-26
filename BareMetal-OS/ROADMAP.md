# AlJefra OS — Universal Boot Roadmap

> **Goal**: Boot on any device — x86-64, ARM64, RISC-V — with a minimum kernel
> that gets network connectivity, then uses AI to download everything else.

**Last updated**: 2026-02-26

---

## Phase 0: Foundation — HAL + x86-64 Refactor

### 0.1 HAL Interface Headers
- [x] `hal/hal.h` — Master include, status codes, arch detection
- [x] `hal/cpu.h` — CPU operations (init, halt, features, cycles, random)
- [x] `hal/interrupt.h` — IRQ registration, enable/disable, EOI
- [x] `hal/timer.h` — Nanosecond timer, delays, one-shot arm
- [x] `hal/bus.h` — Device discovery, PCIe config read/write
- [x] `hal/io.h` — MMIO read/write (8/16/32/64), port I/O, DMA alloc
- [x] `hal/mmu.h` — Page table management, memory map, page alloc
- [x] `hal/smp.h` — Multi-core init, work dispatch, spinlocks
- [x] `hal/console.h` — UART/serial output, printf, input

### 0.2 x86-64 HAL Implementation
- [x] `arch/x86_64/cpu.c` — CPUID, FPU/SSE enable, RDTSC, RDRAND
- [x] `arch/x86_64/interrupt.c` — APIC EOI, callback mapping
- [x] `arch/x86_64/timer.c` — Wraps b_system(TIMECOUNTER)
- [x] `arch/x86_64/io.c` — IN/OUT inline asm, DMA bump allocator
- [x] `arch/x86_64/bus.c` — Full PCIe enumeration (bus 0-255)
- [x] `arch/x86_64/mmu.c` — 4-level page tables, bitmap allocator
- [x] `arch/x86_64/smp.c` — TTAS spinlock, wraps b_system(SMP_*)
- [x] `arch/x86_64/console.c` — COM1 UART 115200/8N1, printf
- [x] `arch/x86_64/hal_init.c` — Ordered subsystem initialization
- [x] `arch/x86_64/start.c` — `_start` entry (BSS zero → hal_init → kernel_main)
- [x] `arch/x86_64/linker.ld` — .text at 0x1E0000, PHDRS segments
- [x] **Compiles clean**: 36 objects → 74KB binary, 0 errors, 0 warnings

### 0.3 Portable C Drivers
- [x] `drivers/storage/nvme.c` — NVMe (admin+IO queues, PRP, read/write)
- [x] `drivers/storage/ahci.c` — SATA/AHCI (port enum, IDENTIFY, DMA)
- [x] `drivers/storage/virtio_blk.c` — VirtIO block (PCI+MMIO dual transport)
- [x] `drivers/storage/emmc.c` — eMMC/SD (SDHCI, full card init, PIO)
- [x] `drivers/network/e1000.c` — Intel e1000 (TX/RX rings, EEPROM MAC)
- [x] `drivers/network/virtio_net.c` — VirtIO net (PCI+MMIO, RX/TX queues)
- [x] `drivers/input/xhci.c` — USB 3.0 host controller (DCBAA, TRBs)
- [x] `drivers/input/usb_hid.c` — USB HID keyboard/mouse (boot protocol)
- [x] `drivers/input/ps2.c` — PS/2 keyboard (scancode set 2)
- [x] `drivers/display/serial_console.c` — 16550 UART (port I/O + MMIO)
- [x] `drivers/display/lfb.c` — Framebuffer (8x16 font, scroll, 16/24/32bpp)
- [x] `drivers/bus/pcie.c` — PCIe (ECAM + legacy 0xCF8, BAR sizing, MSI-X)
- [x] `drivers/bus/dt_parser.c` — FDT parser (DTB traversal, reg/compat)
- [x] `drivers/bus/acpi_lite.c` — ACPI (RSDP, MADT, MCFG, FADT)

### 0.4 Kernel Core
- [x] `kernel/main.c` — Entry: banner → bus scan → driver match → net → AI
- [x] `kernel/sched.c` — Round-robin scheduler (64 tasks, cooperative yield)
- [x] `kernel/syscall.c` — 14 syscall dispatch (input/output/system/sched)
- [x] `kernel/driver_loader.c` — Built-in + runtime .ajdrv driver loading
- [x] `kernel/ai_bootstrap.c` — Bootstrap state machine (INIT→NET→AI→COMPLETE)

### 0.5 Network + DHCP
- [x] `net/dhcp.c` — Full DORA sequence (Discover→Offer→Request→Ack)

### 0.6 Build System
- [x] `Makefile` — Multi-arch (`make ARCH=x86_64|aarch64|riscv64`)
- [x] `lib/string.c` — Compiler builtins (memcpy, memset, memmove, memcmp)
- [x] x86-64 compiles and links: **104 KB** (40 objects)
- [x] aarch64 compiles and links: **109 KB** (39 objects)
- [x] riscv64 compiles and links: **93 KB** (39 objects)

### 0.7 Documentation
- [x] `doc/architecture.md` — Multi-arch overview, layer diagram
- [x] `doc/hal_spec.md` — Complete HAL API reference
- [x] `doc/driver_guide.md` — How to write portable drivers
- [x] `doc/boot_protocol.md` — Per-arch boot sequences, QEMU commands
- [x] `doc/marketplace_spec.md` — REST API, .ajdrv format
- [x] `doc/security_model.md` — Ed25519 trust chain, secure boot
- [x] `doc/porting_guide.md` — Adding new architectures
- [x] `doc/memory_maps.md` — Physical memory maps per arch

### 0.8 Integration Testing
- [x] x86-64 boots on QEMU with VirtIO (two-stage loader → AlJefra kernel at 0x1000000)
- [x] HAL init prints banner + CPU info + 7 PCIe devices on serial
- [ ] NVMe driver reads from QEMU NVMe device
- [ ] USB keyboard works on QEMU with xHCI
- [x] DHCP obtains IP 10.0.2.15 from QEMU user-mode networking
- [x] e1000 C driver loaded, initialized, and used for DHCP + marketplace TLS
- [ ] Physical x86-64 machine boots (Intel NUC or similar)

---

## Phase 1: AI Bootstrap + Driver Marketplace

### 1.1 Driver Package System
- [x] `store/package.h` — .ajdrv format (64-byte header, Ed25519 sig)
- [x] `store/verify.c` — SHA-512 implementation, Ed25519 framework
- [x] `store/install.c` — Verify → arch check → driver_load_runtime
- [x] `store/catalog.c` — In-memory catalog (128 entries)

### 1.2 Marketplace Client
- [x] `ai/marketplace.c` — JSON manifest builder, HTTP POST, download URL

### 1.3 AI Bootstrap Flow
- [x] `kernel/ai_bootstrap.c` — HW detect → DHCP → marketplace → download → load

### 1.4 Integration
- [x] Complete Ed25519 signature verification (curve25519 math, 1550 lines)
- [x] Minimal TCP/IP client (net/tcp.c) — ARP + TCP handshake over raw Ethernet frames
- [x] Marketplace HTTP client integrated: kernel → TCP → HTTP POST/GET → Flask server
- [x] AI agent connects, sends hardware manifest, receives driver list (HTTP 200 verified)
- [ ] Driver package (.ajdrv) downloaded over HTTPS, signature verified
- [ ] Downloaded driver loaded at runtime, initializes hardware
- [ ] Full cycle: cold boot → AI → driver download → functional (<60s)

### 1.5 WiFi Support
- [x] `drivers/network/wifi_framework.c` — 802.11 framework (1404 lines)
- [x] `drivers/network/wifi_framework.h` — WiFi API header (460 lines)
- [x] `drivers/network/aes_ccmp.c` — AES-CCMP encryption (678 lines)
- [ ] `drivers/network/intel_wifi.c` — Intel AX200/AX210
- [ ] WiFi connects on physical laptop

### 1.6 Marketplace Server
- [x] Deploy REST API (`server/app.py`, Flask, 363 lines)
- [x] GET /v1/catalog — Browse drivers (with filtering)
- [x] POST /v1/manifest — Send HW manifest, get driver list
- [x] GET /v1/drivers/{vendor}/{device}/{arch} — Download .ajdrv
- [x] POST /v1/drivers — Upload new driver
- [x] GET /v1/updates/{os_version} — Check for updates
- [x] `server/ajdrv_builder.py` — CLI tool to build .ajdrv packages (435 lines)
- [x] `server/driver_store.py` — File-based storage backend (216 lines)
- [x] `server/models.py` — Data models (190 lines)
- [x] `server/Dockerfile` + `docker-compose.yml` — Containerized deployment
- [x] 8 seed drivers for VirtIO, e1000, QEMU VGA across 3 architectures

---

## Phase 2: ARM64 Port

### 2.1 Architecture Support
- [x] `arch/aarch64/boot.S` — EL3→EL2→EL1 drop, exception vectors
- [x] `arch/aarch64/cpu.c` — MIDR_EL1 decode, FPU/NEON/SVE enable
- [x] `arch/aarch64/interrupt.c` — GICv2/v3 auto-detect
- [x] `arch/aarch64/timer.c` — ARM Generic Timer (CNTFRQ_EL0)
- [x] `arch/aarch64/io.c` — DSB SY barrier, MMIO-only
- [x] `arch/aarch64/bus.c` — PCIe ECAM + DT device enumeration
- [x] `arch/aarch64/mmu.c` — MAIR/TCR, 2MB block descriptors
- [x] `arch/aarch64/smp.c` — PSCI via HVC, LDAXR/STLXR spinlocks
- [x] `arch/aarch64/console.c` — PL011 UART at 0x09000000
- [x] `arch/aarch64/hal_init.c` — Ordered init
- [x] `arch/aarch64/linker.ld` — .text at 0x40000000
- [x] **Compiles clean**: 35 objects → 73KB binary, 0 errors

### 2.2 QEMU Testing
- [x] ARM64 QEMU `virt` machine boots to UART output (Cortex-A72)
- [x] `_start` → hal_init → kernel_main runs successfully
- [x] Bus scan discovers 4 devices (PCIe + DT)
- [x] TCP/IP + marketplace client runs on ARM64 QEMU (DHCP → ARP → TCP → HTTP POST → Flask server)

### 2.3 Physical Hardware
- [ ] Raspberry Pi 5 boots from SD card
- [ ] Raspberry Pi 5 gets network via USB Ethernet or WiFi
- [ ] NVIDIA Jetson boots via UEFI
- [ ] Ampere Altra (ARM server) boots via UEFI

---

## Phase 3: RISC-V Port

### 3.1 Architecture Support
- [x] `arch/riscv64/boot.S` — OpenSBI convention, trap vector
- [x] `arch/riscv64/cpu.c` — SBI v0.2 calls, FPU via sstatus.FS
- [x] `arch/riscv64/interrupt.c` — PLIC, SIE CSR, scause dispatch
- [x] `arch/riscv64/timer.c` — SBI timer extension, rdtime
- [x] `arch/riscv64/io.c` — FENCE IORW barrier, MMIO
- [x] `arch/riscv64/bus.c` — PCIe ECAM + VirtIO MMIO slots
- [x] `arch/riscv64/mmu.c` — Sv48 page tables
- [x] `arch/riscv64/smp.c` — SBI HSM, LR.D/SC.D spinlocks
- [x] `arch/riscv64/console.c` — NS16550A UART + SBI fallback
- [x] `arch/riscv64/hal_init.c` — Ordered init
- [x] `arch/riscv64/linker.ld` — .text at 0x80200000
- [x] **Compiles clean**: 35 objects → 65KB binary, 0 errors

### 3.2 QEMU Testing
- [x] RISC-V QEMU `virt` machine boots to UART output (OpenSBI + Sv39)
- [x] Bus scan discovers 12 devices (1 PCIe + 11 DT)
- [ ] TCP/IP + AI agent runs on RISC-V QEMU

### 3.3 Physical Hardware
- [ ] VisionFive 2 boots from SD card
- [ ] Milk-V Mars boots from SD card

### 3.4 Additional Drivers
- [x] `drivers/network/rtl8169.c` — Realtek RTL8169/8168/8111 Gigabit Ethernet (436 lines)
- [ ] `drivers/storage/emmc.c` — eMMC/SD tuning for SBCs
- [ ] `drivers/network/bcm_wifi.c` — Broadcom WiFi (RPi)

---

## Phase 4: Mobile/Tablet + Apple Silicon

### 4.1 Apple Silicon
- [ ] m1n1/Asahi firmware chain integration
- [ ] Apple NVMe driver
- [ ] Apple USB-C driver
- [ ] Broadcom WiFi for Apple hardware
- [ ] MacBook Air/Pro M-series boots

### 4.2 Mobile Devices
- [x] `drivers/input/touch.c` — Touchscreen framework (1291 lines, multi-touch, HID-over-I2C/USB)
- [x] `drivers/input/touch.h` — Touchscreen API header (268 lines)
- [x] `drivers/storage/ufs.h` — UFS driver header/interface (490 lines)
- [ ] `drivers/storage/ufs.c` — UFS storage implementation
- [ ] Android ABL bootloader chain (Qualcomm/MediaTek)
- [ ] Qualcomm WiFi/modem driver (QCA6390)
- [ ] Pixel phone boots (unlocked bootloader)
- [ ] OnePlus phone boots

---

## Phase 5: Marketplace + Community

### 5.1 Server Infrastructure
- [x] Marketplace web API (REST + driver catalog DB) — `server/app.py`
- [x] Driver submission pipeline (upload, sign, review) — `server/ajdrv_builder.py`
- [ ] OTA update mechanism

### 5.2 Community
- [ ] AI evolution integration — Claude/users evolve drivers
- [ ] Community audit system (version control, review, approve)
- [ ] Developer onboarding documentation

---

## Build Summary

| Architecture | Binary Size | Objects | Status |
|:------------|:-----------|:--------|:-------|
| **x86-64**  | 125 KB     | 42      | Builds clean, boots on QEMU |
| **ARM64**   | 133 KB     | 41      | Builds clean, boots on QEMU, marketplace HTTP verified |
| **RISC-V**  | 113 KB     | 41      | Builds clean, boots on QEMU |

**Total**: ~118 source files, ~29,000 lines of C/ASM/Python/docs

```
make ARCH=x86_64     # Build for x86-64 (default)
make ARCH=aarch64    # Build for ARM64
make ARCH=riscv64    # Build for RISC-V
make all-arch        # Build all three
make clean           # Clean build artifacts
```

---

## Progress Legend

- [x] = Complete and verified
- [ ] = Not started / in progress
