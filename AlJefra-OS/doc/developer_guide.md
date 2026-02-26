# AlJefra OS — Developer Onboarding Guide

## Quick Start

### Prerequisites

```bash
# Debian/Ubuntu
sudo apt install build-essential gcc-aarch64-linux-gnu gcc-riscv64-linux-gnu \
                 nasm qemu-system-x86 qemu-system-arm qemu-system-misc
```

### Build

```bash
cd AlJefra-OS

# Build for a single architecture (default: x86-64)
make                       # x86-64
make ARCH=aarch64          # ARM64
make ARCH=riscv64          # RISC-V 64

# Build all architectures
make all-arch

# Clean
make clean
```

Output binaries go to `build/<arch>/bin/kernel_<arch>.bin`.

### Run in QEMU

```bash
# x86-64 (multiboot1 kernel, serial output)
qemu-system-x86_64 -machine q35 -cpu Westmere -smp 1 -m 256 \
  -kernel build/x86_64/kernel.elf -serial stdio -display none \
  -device virtio-net-pci,netdev=n0 -netdev user,id=n0 \
  -drive if=none,id=d0,file=/tmp/disk.img,format=raw \
  -device virtio-blk-pci,drive=d0

# ARM64 (Device Tree, UART output)
qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 256 \
  -kernel build/aarch64/bin/kernel_aarch64.bin -serial stdio -display none \
  -device virtio-net-pci,netdev=n0 -netdev user,id=n0

# RISC-V 64 (OpenSBI + kernel, UART output)
qemu-system-riscv64 -machine virt -m 256 \
  -kernel build/riscv64/bin/kernel_riscv64.bin -serial stdio -display none \
  -device virtio-net-pci,netdev=n0 -netdev user,id=n0
```

### Run with Marketplace Server

```bash
# Terminal 1: Start the marketplace Flask server
cd server && python3 app.py

# Terminal 2: Run QEMU with host port forwarding
# QEMU user-mode networking maps 10.0.2.2 → host automatically
qemu-system-x86_64 -machine q35 -cpu Westmere -smp 1 -m 256 \
  -kernel build/x86_64/kernel.elf -serial stdio -display none \
  -device virtio-net-pci,netdev=n0 -netdev user,id=n0 \
  -drive if=none,id=d0,file=/tmp/disk.img,format=raw \
  -device virtio-blk-pci,drive=d0
```

---

## Project Structure

```
AlJefra-OS/
├── hal/            # Hardware Abstraction Layer headers (architecture-independent API)
├── arch/           # Architecture-specific implementations
│   ├── x86_64/     # x86-64: boot.S, cpu.c, interrupt.c, timer.c, io.c, bus.c, mmu.c, smp.c
│   ├── aarch64/    # ARM64: boot.S, cpu.c, interrupt.c (GIC), timer.c, mmu.c, smp.c (PSCI)
│   └── riscv64/    # RISC-V: boot.S, cpu.c, interrupt.c (PLIC), timer.c, mmu.c (Sv39)
├── kernel/         # Architecture-independent kernel core
│   ├── main.c      # Entry point: banner → bus scan → driver load → network → AI bootstrap
│   ├── sched.c     # Cooperative scheduler
│   ├── syscall.c   # Syscall dispatch
│   ├── driver_loader.c  # Built-in + runtime (.ajdrv) driver loading
│   └── ai_bootstrap.c   # AI-driven hardware setup (DHCP → marketplace → download drivers)
├── drivers/        # Portable C drivers (use HAL only, no inline asm)
│   ├── storage/    # NVMe, AHCI, VirtIO-Blk, eMMC, UFS
│   ├── network/    # e1000, VirtIO-Net, RTL8169, Intel WiFi, BCM WiFi, WiFi framework
│   ├── input/      # xHCI (USB 3.0), USB HID, PS/2, Touchscreen
│   ├── display/    # Linear framebuffer, serial console
│   ├── bus/        # PCIe enumeration, Device Tree parser, ACPI lite
│   └── runtime/    # .ajdrv packages (position-independent drivers)
├── net/            # Minimal network stack (DHCP, TCP)
├── ai/             # AI marketplace client
├── store/          # Package verification (Ed25519), installation, OTA updates
├── lib/            # Runtime library (memcpy, memset, etc.)
├── server/         # Marketplace Flask API server
└── doc/            # Documentation
```

---

## How It Works

### Boot Sequence

1. **Arch-specific boot** (`arch/<arch>/boot.S`) — CPU init, MMU, interrupts, UART
2. **HAL init** (`arch/<arch>/hal_init.c`) — Registers arch functions into HAL vtable
3. **kernel_main()** — Architecture-independent from here on:
   - Register built-in driver ops tables
   - Bus scan (PCIe on x86, Device Tree on ARM/RISC-V)
   - Match devices to drivers, init matched drivers
   - DHCP → TCP init → Connect to marketplace
   - Send hardware manifest → download recommended .ajdrv drivers
   - Load runtime drivers → system ready

### Driver Categories

| Category | Examples | HAL functions used |
|----------|---------|-------------------|
| Storage | NVMe, AHCI, VirtIO-Blk, eMMC, UFS | `hal_mmio_*`, `hal_dma_alloc`, `hal_bus_*` |
| Network | e1000, VirtIO-Net, RTL8169, WiFi | `hal_mmio_*`, `hal_dma_alloc`, `hal_irq_*` |
| Input | xHCI, USB HID, PS/2, Touchscreen | `hal_mmio_*`, `hal_irq_register` |
| Display | Framebuffer, Serial console | `hal_mmio_*` |
| Bus | PCIe, Device Tree, ACPI | `hal_bus_*` |

---

## Writing a New Driver

### 1. Create the driver file

```c
/* drivers/network/my_nic.c */
#include "../../hal/hal.h"
#include "../../kernel/driver_loader.h"

static hal_status_t my_nic_init(hal_device_t *dev)
{
    /* Map BAR0 for MMIO access */
    volatile void *regs = hal_bus_map_bar(dev, 0);
    if (!regs) return HAL_ERROR;

    /* Enable bus mastering */
    hal_bus_pci_enable(dev);

    /* Initialize hardware... */
    uint32_t status = hal_mmio_read32(regs + 0x08);
    hal_console_printf("[my_nic] Status: 0x%08x\n", status);

    return HAL_OK;
}

static int64_t my_nic_tx(const void *frame, uint64_t len) { /* ... */ }
static int64_t my_nic_rx(void *frame, uint64_t max_len) { /* ... */ }
static void my_nic_get_mac(uint8_t mac[6]) { /* ... */ }

static const driver_ops_t my_nic_ops = {
    .name       = "my_nic",
    .category   = DRIVER_CAT_NETWORK,
    .init       = my_nic_init,
    .net_tx     = my_nic_tx,
    .net_rx     = my_nic_rx,
    .net_get_mac = my_nic_get_mac,
};

void my_nic_register(void)
{
    driver_register_builtin(&my_nic_ops);
}
```

### 2. Register in kernel/main.c

```c
extern void my_nic_register(void);

// In register_builtin_drivers():
    my_nic_register();

// In load_builtin_drivers():
    if (d->vendor_id == 0xAAAA && d->device_id == 0xBBBB) {
        rc = driver_load_builtin("my_nic", d);
        if (rc == HAL_OK) loaded++;
    }
```

### 3. Build and test

The Makefile auto-discovers `drivers/network/*.c`. Just run `make`.

---

## Adding a New Architecture

See `doc/porting_guide.md` for the full guide. Summary:

1. Create `arch/<newarch>/` with these files:
   - `boot.S` — Entry point, CPU mode setup, stack, call `hal_init()`
   - `cpu.c` — Implement `hal_cpu_*` functions
   - `interrupt.c` — Implement `hal_irq_*` functions
   - `timer.c` — Implement `hal_timer_*` functions
   - `io.c` — Implement `hal_mmio_*` functions
   - `bus.c` — Implement `hal_bus_*` functions
   - `mmu.c` — Implement `hal_mmu_*` functions
   - `smp.c` — Implement `hal_smp_*` functions
   - `console.c` — UART/serial for `hal_console_*`
   - `hal_init.c` — Populate the HAL vtable
   - `linker.ld` — Memory layout

2. Add toolchain to Makefile (new `ifeq ($(ARCH),newarch)` block)

3. Test in QEMU: `qemu-system-<newarch> -kernel build/<newarch>/bin/kernel_<newarch>.bin`

---

## Runtime Drivers (.ajdrv)

Runtime drivers are position-independent binaries downloaded from the marketplace. They use a kernel API vtable instead of direct HAL calls.

### Building a .ajdrv

```bash
cd drivers/runtime
./build_ajdrv.sh my_driver.c x86_64
# Output: my_driver.ajdrv
```

### .ajdrv format

| Offset | Size | Field |
|--------|------|-------|
| 0x00 | 4 | Magic (0x414A4456 = "AJDV") |
| 0x04 | 4 | Version |
| 0x08 | 4 | Architecture (0=x86_64, 1=aarch64, 2=riscv64, 0xFF=any) |
| 0x0C | 4 | Code offset |
| 0x10 | 4 | Code size |
| 0x14 | 4 | Entry offset (relative to code start) |
| 0x18 | 4 | Name offset |
| 0x1C | 4 | Name size |
| 0x20 | 64 | Ed25519 signature |
| 0x60+ | ... | Code + data |

### Signing

```bash
python3 server/ajdrv_builder.py sign my_driver.ajdrv --key private_key.pem
```

---

## Marketplace API

The marketplace server (in `server/`) provides:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/v1/manifest` | POST | Send hardware manifest, get driver recommendations |
| `/v1/drivers/{vendor}/{device}/{arch}` | GET | Download .ajdrv for specific device |
| `/v1/catalog` | GET | Browse all available drivers |
| `/v1/updates/{version}` | GET | Check for OS updates |
| `/v1/drivers` | POST | Upload a new driver (with signature) |

See `doc/marketplace_spec.md` for the full API reference.

---

## Coding Conventions

- **No inline assembly** in drivers — use HAL functions
- **No libc** — use `hal_console_printf` for output, `lib/string.c` for memcpy/memset
- **No dynamic memory** — use `hal_dma_alloc()` for buffers
- **Compile with `-Werror`** — all warnings are errors
- **Naming**: `snake_case` for functions and variables, `UPPER_CASE` for constants
- **Driver names**: lowercase with underscores (e.g., `virtio_net`, `bcm_wifi`)

---

## Key Documentation

| Document | Description |
|----------|-------------|
| `doc/architecture.md` | Multi-arch design overview |
| `doc/hal_spec.md` | HAL interface specification (all functions) |
| `doc/driver_guide.md` | How to write portable drivers |
| `doc/boot_protocol.md` | Boot process per architecture |
| `doc/marketplace_spec.md` | Store API reference |
| `doc/security_model.md` | Ed25519 signing, trust chain |
| `doc/porting_guide.md` | How to add a new architecture |
| `doc/memory_maps.md` | Memory layouts per architecture |
| `ROADMAP.md` | Project status and future plans |
