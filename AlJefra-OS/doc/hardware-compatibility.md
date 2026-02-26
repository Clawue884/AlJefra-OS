# AlJefra OS -- Hardware Compatibility List

This document tracks hardware support across all three architectures. It serves
as both a reference for users and a coordination tool for contributors.

---

## Tier System

| Tier   | Meaning                                                      |
|--------|--------------------------------------------------------------|
| Tier 1 | **Verified** -- Driver exists AND tested on QEMU or real HW  |
| Tier 2 | **Code Complete** -- Driver written, not yet verified on HW   |
| Tier 3 | **Planned** -- No driver yet, on the roadmap                  |

---

## Tier 1 -- Verified

These devices have working drivers and have been tested on QEMU.

| Device            | Driver File             | Arch     | Tester      | Notes                          |
|-------------------|-------------------------|----------|-------------|--------------------------------|
| Intel e1000       | drivers/net/e1000.c     | x86_64   | Core team   | QEMU 82540EM; also 82574L, I217, I219 |
| VirtIO-Blk        | drivers/storage/virtio_blk.c | x86_64, aarch64, riscv64 | Core team | Legacy + modern (0x1001, 0x1042) |
| VirtIO-Net        | drivers/net/virtio_net.c | x86_64, aarch64, riscv64 | Core team | Legacy + modern (0x1000, 0x1041) |
| PS/2 Keyboard     | drivers/input/ps2.c     | x86_64   | Core team   | Standard AT keyboard           |
| Serial UART       | drivers/serial/serial.c | x86_64, aarch64, riscv64 | Core team | 16550A compatible, PL011 (ARM) |
| VGA/LFB           | drivers/display/framebuffer.c | x86_64 | Core team | Linear framebuffer via Multiboot |
| PCIe Bus          | drivers/bus/pcie.c      | x86_64, aarch64, riscv64 | Core team | Enumeration + device matching  |

---

## Tier 2 -- Code Complete

Driver code exists and compiles. Testing on real hardware or additional QEMU
configurations is needed.

| Device              | Driver File                 | Arch     | Notes                              |
|---------------------|-----------------------------|----------|------------------------------------|
| NVMe                | drivers/storage/nvme.c      | x86_64   | Admin + I/O queue, 4K sectors      |
| AHCI (SATA)         | drivers/storage/ahci.c      | x86_64   | Port multiplier not yet supported  |
| RTL8169             | drivers/net/rtl8169.c       | x86_64   | Realtek Gigabit Ethernet           |
| xHCI USB 3.0        | drivers/bus/xhci.c          | x86_64   | Host controller, basic enumeration |
| USB HID             | drivers/input/usb_hid.c     | x86_64   | Keyboard + mouse via xHCI          |
| Intel WiFi AX200/210| drivers/net/wifi_intel.c    | x86_64   | Firmware loading not yet tested    |
| BCM WiFi (RPi)      | drivers/net/wifi_bcm.c      | aarch64  | Broadcom 43xx for Raspberry Pi     |
| eMMC                | drivers/storage/emmc.c      | aarch64  | SD/eMMC with speed tuning          |
| UFS                 | drivers/storage/ufs.c       | aarch64  | Universal Flash Storage            |
| Touchscreen         | drivers/input/touchscreen.c | aarch64  | Generic I2C/SPI touch panels       |
| NVIDIA GPU          | drivers/gpu/nvidia.asm      | x86_64   | RTX 5090, compute + VRAM mgmt     |
| Device Tree Parser  | drivers/bus/dt_parser.c     | aarch64, riscv64 | FDT parsing for device discovery |
| ACPI Lite           | drivers/bus/acpi_lite.c     | x86_64   | Basic ACPI table parsing           |

---

## Tier 3 -- Planned

No driver code yet. Contributions welcome.

| Device              | Target Arch    | Priority  | Notes                            |
|---------------------|----------------|-----------|----------------------------------|
| AMD GPU (RDNA)      | x86_64         | Medium    | Display + compute                |
| Intel GPU (Xe)      | x86_64         | Medium    | Integrated graphics              |
| Audio HDA           | x86_64         | Medium    | Intel High Definition Audio      |
| Bluetooth           | x86_64, aarch64| Low       | HCI over USB or UART             |
| Intel i225/i226     | x86_64         | High      | 2.5 GbE NIC                     |
| Realtek RTL8125     | x86_64         | Medium    | 2.5 GbE NIC                     |
| USB Mass Storage    | x86_64         | Medium    | Bulk-only transport via xHCI     |
| NVDIMM              | x86_64         | Low       | Persistent memory                |
| Virtio-GPU          | x86_64         | Medium    | QEMU virtual GPU                 |
| Audio (USB)         | x86_64         | Low       | USB Audio Class                  |

---

## QEMU Tested Configurations

These QEMU command lines are used for CI and manual testing:

### x86-64

```bash
qemu-system-x86_64 -machine q35 -cpu Westmere -m 256 -smp 1 \
  -kernel build/x86_64/bin/kernel_x86_64.bin -nographic \
  -device e1000,netdev=n0 -netdev user,id=n0 \
  -drive file=test.img,format=raw,if=none,id=d0 \
  -device virtio-blk-pci,drive=d0
```

### ARM64

```bash
qemu-system-aarch64 -M virt -cpu cortex-a72 -m 256 -nographic \
  -kernel build/aarch64/bin/kernel_aarch64.bin \
  -device virtio-blk-device,drive=d0 \
  -drive file=test.img,format=raw,if=none,id=d0
```

### RISC-V 64

```bash
qemu-system-riscv64 -M virt -m 256 -nographic \
  -kernel build/riscv64/bin/kernel_riscv64.bin \
  -device virtio-blk-device,drive=d0 \
  -drive file=test.img,format=raw,if=none,id=d0
```

---

## How to Contribute Hardware Testing

### Testing on Real Hardware

1. Build AlJefra OS for the appropriate architecture.
2. Write the image to a USB drive or SD card (see `doc/boot_protocol.md`).
3. Boot the device and capture the serial console output.
4. Report results by opening a GitHub issue with:
   - Device make/model
   - CPU / architecture
   - Full serial console log
   - Whether boot succeeded
   - Which drivers loaded (or failed)

### Testing Report Template

```
## Hardware Test Report

**Device:** [Make and model]
**CPU:** [e.g., Intel Core i7-12700, Raspberry Pi 4B]
**Architecture:** [x86_64 / aarch64 / riscv64]
**AlJefra OS Version:** [e.g., v1.0.0]
**Date:** [YYYY-MM-DD]

### Boot Result
- [ ] Boot successful
- [ ] Boot failed (describe where it stopped)

### Drivers Loaded
| Driver    | Status  | Notes           |
|-----------|---------|-----------------|
| e1000     | OK      |                 |
| nvme      | FAIL    | Timeout on init |

### Serial Console Log
(Paste full log here)

### Additional Notes
(Any other observations)
```

---

## Known Issues

| Issue                                | Affected Device    | Status     |
|--------------------------------------|--------------------|------------|
| eMMC speed tuning untested on HW     | eMMC (ARM64)       | Open       |
| WiFi firmware loading not implemented | Intel AX200/AX210  | Open       |
| BCM WiFi needs RPi-specific DT       | BCM43xx (RPi)      | Open       |
| xHCI enumeration incomplete          | USB 3.0 hubs       | Open       |
| NVMe multi-namespace not supported    | NVMe SSDs          | Open       |
| AHCI port multiplier not handled      | SATA PM             | Open       |
| SMP limited to 1 CPU on QEMU Westmere| x86-64 SMP         | Workaround |

---

*Last updated: 2026-02-27*
*AlJefra OS -- Built in Qatar. Built for the world.*
