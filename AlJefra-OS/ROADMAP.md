# AlJefra OS — Universal Boot Roadmap

> **Goal**: Boot on any device — x86-64, ARM64, RISC-V — with a minimum kernel
> that gets network connectivity, then uses AI to download everything else.

**Last updated**: 2026-02-26 (all QEMU verification complete)

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
- [x] `arch/x86_64/boot.S` — Multiboot1 stub (32→64 bit, PML4 identity map 4GB, GDT, SSE)
- [x] `arch/x86_64/cpu.c` — CPUID, FPU/SSE enable, RDTSC, RDRAND (standalone, no b_system)
- [x] `arch/x86_64/interrupt.c` — APIC EOI, handler table (standalone, polling mode)
- [x] `arch/x86_64/timer.c` — RDTSC + PIT Channel 2 calibration (standalone)
- [x] `arch/x86_64/io.c` — IN/OUT inline asm, DMA bump allocator
- [x] `arch/x86_64/bus.c` — Direct 0xCF8/0xCFC PCI config I/O (standalone)
- [x] `arch/x86_64/mmu.c` — 4-level page tables, bitmap allocator, multiboot1 memory map
- [x] `arch/x86_64/smp.c` — TTAS spinlock, CPUID-based APIC ID (standalone)
- [x] `arch/x86_64/console.c` — COM1 UART 115200/8N1, printf
- [x] `arch/x86_64/hal_init.c` — Ordered subsystem initialization
- [x] `arch/x86_64/start.c` — `_start` entry (BSS zero → hal_init → kernel_main)
- [x] `arch/x86_64/linker.ld` — Multiboot1, KERNEL_BASE=0x100000
- [x] **Compiles clean**: 44 objects → 134KB binary, 0 errors, 0 warnings
- [x] **Standalone boot**: No AlJefra b_system() dependency — boots via multiboot1

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
- [x] x86-64 compiles and links: **147 KB** (44 objects, standalone multiboot1)
- [x] aarch64 compiles and links: **153 KB** (42 objects)
- [x] riscv64 compiles and links: **129 KB** (42 objects)

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
- [x] x86-64 boots standalone on QEMU via multiboot1 (no AlJefra dependency)
- [x] HAL init: Console + CPU (Westmere) + MMU (255MB from multiboot mmap) + Timer (RDTSC+PIT) + PCIe + SMP
- [x] PCI scan finds 8 devices: e1000/VirtIO-Net, NVMe/VirtIO-Blk, xHCI, AHCI, host bridge, ISA bridge, SMBus
- [x] All 3 drivers load: VirtIO-Net + NVMe (read/write PASSED) + xHCI (USB keyboard detected on slot 1, speed=3)
- [x] VirtIO modern (1.0+): VirtIO-Net (0x1041) + VirtIO-Blk (0x1042) load, DHCP + storage test PASS
- [x] NVMe driver: QEMU NVMe device (serial=ALJEFRA001), admin+IO queues, Storage READ/WRITE test PASSED
- [x] USB keyboard: QEMU xHCI + usb-kbd, Port 5 connected, slot 1 assigned, boot protocol HID
- [x] DHCP obtains IP 10.0.2.15, TCP connects to marketplace, downloads .ajdrv
- [x] Full boot-to-ready sequence completes in <8 seconds on QEMU
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
- [x] Driver package (.ajdrv) downloaded over HTTP, header validated, entry called (169 bytes, RISC-V QEMU)
- [x] Ed25519 signature verification for .ajdrv downloads (wired into driver_load_runtime, trusted key set in bootstrap)
- [x] Downloaded driver loaded at runtime, initializes real hardware (QEMU VGA 800x600x32)
- [x] Full cycle: cold boot → AI → driver download → functional (x86-64 QEMU, <15s)
- [x] kernel_api_t vtable for runtime drivers (console, MMIO, DMA, timer, bus/PCI)
- [x] Position-independent .ajdrv build system (drivers/runtime/build_ajdrv.sh)

### 1.5 WiFi Support
- [x] `drivers/network/wifi_framework.c` — 802.11 framework (1404 lines)
- [x] `drivers/network/wifi_framework.h` — WiFi API header (460 lines)
- [x] `drivers/network/aes_ccmp.c` — AES-CCMP encryption (678 lines)
- [x] `drivers/network/intel_wifi.c` — Intel AX200/AX210 (1738 lines, full register-level driver)
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
- [x] ARM64 QEMU `virt` machine boots to UART output (Cortex-A72, PL011 UART at 0x09000000)
- [x] `_start` → EL2→EL1 drop → hal_init → kernel_main runs successfully
- [x] HAL: CPU (Cortex-A72 r0p3), GIC (288 IRQs), Timer (62.5MHz), MMU (256MB), Bus (5 devices), SMP (1 core)
- [x] Bus scan discovers 5 devices (PCIe), VirtIO-Net + VirtIO-Blk loaded
- [x] TCP/IP + marketplace client runs on ARM64 QEMU (DHCP 10.0.2.15 → ARP → TCP → HTTP POST 200 → driver queries → OTA check)
- [x] Full boot cycle: "AlJefra OS ready." (DHCP + marketplace + driver download attempts + OTA update check)

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
- [x] **Compiles clean**: 42 objects → 117KB binary, 0 errors

### 3.2 QEMU Testing
- [x] RISC-V QEMU `virt` machine boots to UART output (OpenSBI v1.7 → S-mode, RV64GC, Sv39 MMU)
- [x] HAL: CPU (RV64GC), PLIC (1024 IRQs), Timer (10MHz SBI), MMU Sv39 (256MB), Bus (14 devices), SMP (1 hart)
- [x] Bus scan discovers 14 devices (3 PCIe + 11 DT), VirtIO-Net + VirtIO-Blk loaded
- [x] TCP/IP + AI agent runs on RISC-V QEMU (DHCP 10.0.2.15 → ARP → TCP → HTTP POST 200 → driver queries → OTA check)
- [x] Full boot cycle: "AlJefra OS ready." (DHCP + marketplace + 12 driver download attempts + OTA update check)

### 3.3 Physical Hardware
- [ ] VisionFive 2 boots from SD card
- [ ] Milk-V Mars boots from SD card

### 3.4 Additional Drivers
- [x] `drivers/network/rtl8169.c` — Realtek RTL8169/8168/8111 Gigabit Ethernet (436 lines)
- [x] `drivers/storage/emmc.c` — eMMC/SD tuning: HS (50MHz), SDR50 (100MHz), DDR50, auto-tuning
- [x] `drivers/network/bcm_wifi.c` — Broadcom WiFi (RPi) (700 lines)

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
- [x] `drivers/storage/ufs.c` — UFS storage (1249 lines, full UFSHCI init, SCSI commands, power mode)
- [ ] Android ABL bootloader chain (Qualcomm/MediaTek)
- [ ] Qualcomm WiFi/modem driver (QCA6390)
- [ ] Pixel phone boots (unlocked bootloader)
- [ ] OnePlus phone boots

---

## Phase 5: Marketplace + Community

### 5.1 Server Infrastructure
- [x] Marketplace web API (REST + driver catalog DB) — `server/app.py`
- [x] Driver submission pipeline (upload, sign, review) — `server/ajdrv_builder.py`
- [x] OTA update client — `marketplace_check_updates()` + `marketplace_get_catalog()` fully implemented
- [x] OTA kernel update download + apply mechanism (staging header, CRC32 verification, storage staging area)

### 5.2 Community
- [x] AI evolution integration — POST /v1/evolve, metrics reporting, marketplace_submit_evolution()
- [x] Community audit system — POST/GET /v1/reviews, driver approval workflow, POST /v1/metrics
- [x] Developer onboarding documentation — `doc/developer_guide.md`

---

## Build Summary

| Architecture | Binary Size | Objects | Status |
|:------------|:-----------|:--------|:-------|
| **x86-64**  | 145 KB     | 44      | Boots standalone, NVMe R/W + USB kbd + VirtIO + marketplace + .ajdrv runtime load |
| **ARM64**   | 153 KB     | 42      | Boots on QEMU Cortex-A72, VirtIO + DHCP + marketplace + OTA check |
| **RISC-V**  | 129 KB     | 42      | Boots on QEMU rv64, OpenSBI + VirtIO + DHCP + marketplace + OTA check |

**Total**: ~120 source files, ~32,000 lines of C/ASM/Python/docs

### Shared Libraries (deduplication)
| Header | Functions | Replaces |
|:-------|:----------|:---------|
| `lib/string.h` | memcpy, memset, memcmp, memmove, str_eq, str_len, str_copy | 35+ local copies across 23 files |
| `lib/endian.h` | htons, htonl, ntohs, ntohl | 2 local copies in tcp.c + dhcp.c |
| `net/checksum.h` | ip_checksum (RFC 1071) | 2 local copies in tcp.c + dhcp.c |

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
