# AlJefra OS -- System Architecture

## Vision

AlJefra OS is a **self-evolving exokernel** that boots on any device -- desktops, laptops, tablets, single-board computers, and servers -- across x86-64, ARM64, and RISC-V 64 architectures. The core insight is simple: we only need enough built-in drivers to establish network connectivity, then an AI agent handles everything else by downloading and installing drivers from the AlJefra Driver Marketplace.

This document describes the architecture of AlJefra OS v1.0, covering the layer model, design principles, directory structure, supported platforms, boot sequence, driver model, and AI bootstrap flow.

---

## Layer Diagram

The system is organized into six horizontal layers. Each layer depends only on the layer directly below it.

```
+==============================================================+
|                       LAYER 6: APPLICATIONS                  |
|    GUI Desktop  |  AI Chat  |  User Programs  |  Shell       |
+==============================================================+
|                       LAYER 5: AI AGENT                      |
|  Hardware Detection  |  Manifest Builder  |  Store Client    |
|  Self-Evolution Engine  |  Chat Interface                    |
+==============================================================+
|                       LAYER 4: KERNEL CORE                   |
|  Scheduler  |  Memory Manager  |  VFS  |  IPC  |  Syscalls   |
|  BMFS Filesystem  |  Process Management                      |
+==============================================================+
|                   LAYER 3: NETWORK & STORE                   |
|  Ethernet  |  ARP  |  IPv4  |  UDP  |  TCP  |  DHCP  |  DNS |
|  Marketplace Client  |  Driver Download  |  OTA Updates      |
+==============================================================+
|                  LAYER 2: PORTABLE DRIVERS                   |
|  Storage: NVMe, AHCI, VirtIO-Blk, eMMC, UFS                 |
|  Network: e1000, VirtIO-Net, RTL8169, Intel WiFi, BCM WiFi   |
|  Network Support: WiFi Framework, AES-CCMP                   |
|  Input:   PS/2, xHCI (USB 3.0), USB HID, Touchscreen        |
|  Display: Linear Framebuffer, Serial Console                 |
|  Bus:     PCIe, ACPI Lite, Device Tree Parser                |
+==============================================================+
|             LAYER 1: HARDWARE ABSTRACTION LAYER              |
|  CPU  |  Interrupts  |  Timer  |  MMU  |  Bus Scan          |
|  I/O (MMIO + Port I/O)  |  SMP  |  Console                  |
+==============================================================+
|              LAYER 0: ARCHITECTURE-SPECIFIC CODE             |
|       x86-64          |      ARM64         |   RISC-V 64     |
|  APIC, HPET, PIC      |  GIC, Generic      |  PLIC, CLINT    |
|  4-level paging        |  Timer, Sv48       |  Sv39 paging    |
|  BIOS/UEFI boot       |  UEFI/DTB boot     |  SBI/UEFI boot  |
+==============================================================+
|                       HARDWARE                               |
+==============================================================+
```

---

## Design Principles

### 1. HAL Abstraction

All hardware access goes through the Hardware Abstraction Layer. Code above Layer 1 never uses inline assembly, architecture-specific registers, or platform-specific constants. This means every driver, the kernel, the network stack, and the AI agent compile identically for all three architectures.

See [hal_spec.md](hal_spec.md) for the complete API specification.

### 2. Minimal Boot Drivers

The kernel ships with only the drivers needed to establish network connectivity:

- One storage driver (to load the kernel itself)
- One network driver (to reach the marketplace)
- One display driver (for early console output)
- Basic input (PS/2 keyboard for emergency interaction)

Everything else is downloaded from the marketplace after boot.

### 3. Driver Hot-Loading

Drivers can be loaded and unloaded at runtime without rebooting. The driver model uses a `driver_ops_t` structure with `init()` and `shutdown()` callbacks, allowing clean insertion and removal. Runtime drivers are packaged as `.ajdrv` files.

### 4. AI-First Configuration

Instead of requiring the user to know their hardware and manually install drivers, AlJefra OS uses an AI agent to:

1. Scan all buses (PCI, USB, platform) and build a hardware manifest.
2. Connect to the AlJefra Driver Marketplace over the network.
3. Send the manifest and receive a list of matching drivers.
4. Download, verify, load, and configure each driver automatically.

The user experience is: plug in, power on, wait, done.

### 5. Exokernel Philosophy

The kernel exposes hardware capabilities with minimal abstraction overhead. Drivers and applications can opt into direct hardware access through well-defined HAL interfaces, giving them maximum performance while maintaining portability.

---

## Directory Structure

```
AlJefra-OS/
|
|-- arch/                          Architecture-specific implementations
|   |-- x86_64/
|   |   |-- boot.asm               NASM boot code (Intel syntax)
|   |   |-- hal_init.c             x86-64 HAL initialization
|   |   |-- cpu.c                  APIC, CPUID, MSR access
|   |   |-- interrupt.c            IDT, APIC IRQ routing
|   |   |-- timer.c                HPET / PIT timer
|   |   |-- mmu.c                  4-level paging (PML4)
|   |   |-- smp.c                  AP startup via SIPI
|   |   |-- io.c                   Port I/O (in/out instructions)
|   |   |-- console.c              VGA text mode, serial (COM1)
|   |   +-- linker.ld              x86-64 linker script
|   |-- aarch64/
|   |   |-- boot.S                 ARM64 boot stub (GAS syntax)
|   |   |-- hal_init.c             ARM64 HAL initialization
|   |   |-- cpu.c                  System register access
|   |   |-- interrupt.c            GICv2/v3 IRQ distribution
|   |   |-- timer.c                Generic Timer (CNTPCT_EL0)
|   |   |-- mmu.c                  Sv48 page tables
|   |   |-- smp.c                  PSCI-based CPU bringup
|   |   |-- io.c                   MMIO-only I/O
|   |   |-- console.c              PL011 UART
|   |   +-- linker.ld              ARM64 linker script
|   +-- riscv64/
|       |-- boot.S                 RISC-V boot stub (GAS syntax)
|       |-- hal_init.c             RISC-V HAL initialization
|       |-- cpu.c                  CSR access, hart management
|       |-- interrupt.c            PLIC IRQ distribution
|       |-- timer.c                CLINT timer (mtime/mtimecmp)
|       |-- mmu.c                  Sv39 page tables
|       |-- smp.c                  Hart bringup via SBI
|       |-- io.c                   MMIO-only I/O
|       |-- console.c              SBI console / 16550 UART
|       +-- linker.ld              RISC-V linker script
|
|-- hal/                           HAL interface headers (portable)
|   |-- hal.h                      Master header -- includes all below
|   |-- cpu.h                      CPU init, ID, frequency, halt
|   |-- interrupt.h                IRQ register, enable, disable, EOI
|   |-- timer.h                    Tick counter, delay, one-shot
|   |-- bus.h                      Bus scan, config read/write
|   |-- io.h                       MMIO read/write, port I/O, DMA
|   |-- mmu.h                      Map, unmap, page alloc/free
|   |-- smp.h                      CPU count, current CPU, spinlocks
|   +-- console.h                  putc, puts, printf
|
|-- kernel/                        Core kernel (architecture-independent)
|   |-- main.c                     kernel_main() entry point
|   |-- sched.c                    Round-robin scheduler
|   |-- syscall.c                  System call dispatch
|   |-- driver_loader.c            Built-in + runtime driver loading
|   |-- ai_bootstrap.c             AI-driven driver download orchestration
|   |-- ai_chat.c                  Natural language command interface
|   |-- fs.c                       BMFS filesystem (read/write/list/create/delete)
|   |-- keyboard.c                 Keyboard input wiring (PS/2 + USB HID)
|   |-- dhcp.c                     Kernel-level DHCP client
|   |-- ota.c                      Over-the-air update system
|   |-- panic.c                    Kernel panic handler (register dump, backtrace)
|   |-- klog.c                     Persistent kernel logging (ring buffer + disk)
|   +-- memprotect.c               Memory protection (NX, WP, SMEP/SMAP, guard pages)
|
|-- drivers/                       Portable device drivers
|   |-- storage/                   NVMe, AHCI, VirtIO-Blk, eMMC, UFS
|   |-- network/                   e1000, VirtIO-Net, RTL8169, Intel WiFi, BCM WiFi,
|   |                              WiFi Framework, AES-CCMP
|   |-- input/                     PS/2, xHCI (USB 3.0), USB HID, Touchscreen
|   |-- display/                   Linear Framebuffer, Serial Console
|   +-- bus/                       PCIe enumeration, ACPI Lite, Device Tree Parser
|
|-- net/                           Network protocol stack
|   |-- tcp.c                      TCP client (SYN/ACK, send/recv, close)
|   +-- dhcp.c                     DHCP client (DORA sequence)
|
|-- ai/                            AI subsystem
|   +-- marketplace.c              Driver marketplace REST client
|
|-- store/                         Driver marketplace client
|-- gpu_engine/                    Framebuffer GPU rendering engine
|-- gui/                           Window manager, desktop shell, widgets
|-- programs/                      Built-in user applications
|-- lib/                           Shared libraries (libc, libm, string)
|-- aljefra/                       AlJefra system utilities
|-- evolution/                     Self-evolution / kernel update subsystem
|-- test/                          Unit and integration tests
|-- server/                        Marketplace server (reference impl)
|-- build/                         Compiled output (per-arch images)
|-- images/                        Logos, screenshots, branding assets
+-- doc/                           All documentation
```

---

## Supported Architectures

| Feature | x86-64 | ARM64 (AArch64) | RISC-V 64 |
|---------|--------|-----------------|-----------|
| **Interrupt Controller** | Local APIC + I/O APIC | GICv2 / GICv3 | PLIC |
| **Timer** | HPET (fallback: PIT 8254) | Generic Timer (CNTPCT_EL0) | CLINT (mtime/mtimecmp) |
| **Paging** | 4-level (PML4), 48-bit VA | Sv48, 48-bit VA | Sv39, 39-bit VA |
| **Boot Firmware** | BIOS or UEFI | UEFI + DeviceTree | SBI (OpenSBI) or UEFI |
| **Assembler** | NASM (Intel syntax) | GAS (ARM syntax) | GAS (RISC-V syntax) |
| **SMP Bringup** | SIPI (Startup IPI) | PSCI | SBI HSM |
| **Port I/O** | Yes (`in`/`out`) | No (MMIO only) | No (MMIO only) |
| **Console** | VGA text + COM1 serial | PL011 UART | SBI console / 16550 UART |
| **QEMU Machine** | `q35` or `pc` | `virt` | `virt` |

---

## Boot Sequence

The boot process differs per architecture at the firmware and early assembly level, but converges at `hal_init()`. The full sequence is:

```
+------------------+
|    Firmware       |   BIOS, UEFI, SBI, or DTB
+--------+---------+
         |
         v
+------------------+
|   Bootloader     |   Pure64 (x86), GRUB, or firmware direct
+--------+---------+
         |
         v
+------------------+
|    boot.S        |   Set up stack, clear BSS, jump to C
+--------+---------+
         |
         v
+------------------+
|   hal_init()     |   Console -> CPU -> MMU -> IRQ -> Timer -> Bus -> SMP
+--------+---------+
         |
         v
+------------------+
| kernel_main()    |   Print banner, scan buses, load built-in drivers
+--------+---------+
         |
         v
+------------------+
|   Bus Enumerate  |   PCI scan, USB scan -- discover all devices
+--------+---------+
         |
         v
+------------------+
|   Network Up     |   Load NIC driver, run DHCP, get IP address
+--------+---------+
         |
         v
+------------------+
| AI Bootstrap     |   Build manifest -> connect store -> download drivers
+--------+---------+
         |
         v
+------------------+
|  System Ready    |   All drivers loaded, desktop starts, AI chat available
+------------------+
```

See [boot_protocol.md](boot_protocol.md) for per-architecture boot details.

---

## Driver Model

Every AlJefra driver is a C module that exposes a `driver_ops_t` structure. The kernel calls its callbacks to initialize, operate, and shut down the driver.

### driver_ops_t Structure

```c
typedef enum {
    DRIVER_STORAGE,
    DRIVER_NETWORK,
    DRIVER_INPUT,
    DRIVER_DISPLAY,
    DRIVER_BUS
} driver_category_t;

typedef struct {
    const char         *name;        /* Human-readable driver name        */
    driver_category_t   category;    /* Driver category                   */
    hal_status_t      (*init)(void); /* Called once at load time           */
    void              (*shutdown)(void); /* Called at unload time          */

    /* Category-specific operations (union or tagged struct) */
    union {
        struct {
            int (*read)(uint64_t lba, uint32_t count, void *buf);
            int (*write)(uint64_t lba, uint32_t count, const void *buf);
            uint64_t (*capacity)(void);
        } storage;

        struct {
            int (*send)(const void *pkt, uint32_t len);
            int (*recv)(void *buf, uint32_t max_len);
            void (*get_mac)(uint8_t mac[6]);
        } network;

        struct {
            int (*poll)(void *event);
        } input;

        struct {
            int (*set_mode)(uint32_t width, uint32_t height, uint32_t bpp);
            void *(*get_framebuffer)(void);
            void (*flip)(void);
        } display;

        struct {
            int (*scan)(void (*callback)(hal_device_t *dev));
        } bus;
    } ops;
} driver_ops_t;
```

### Driver Lifecycle

1. **Discovery** -- The kernel or AI agent determines that a driver is needed (via PCI vendor/device ID match or marketplace query).
2. **Loading** -- For built-in drivers, the `init()` function is called directly. For runtime drivers (`.ajdrv`), the binary is loaded into memory first.
3. **Registration** -- The driver registers its `driver_ops_t` with the kernel driver manager.
4. **Operation** -- The kernel and applications use the category-specific ops to interact with the hardware.
5. **Shutdown** -- When the driver is no longer needed (or being replaced), `shutdown()` is called for clean teardown.

See [driver_guide.md](driver_guide.md) for the complete driver development guide.

---

## AI Bootstrap Flow

The AI bootstrap is what makes AlJefra OS unique. After the network is up, the following sequence executes:

```
1.  Scan PCI bus           -->  List of (vendor_id, device_id, class, subclass) tuples
2.  Scan USB bus           -->  List of (vendor_id, product_id, class) tuples
3.  Scan platform devices  -->  List of compatible strings (from DTB on ARM/RISC-V)
4.  Build JSON manifest    -->  { "arch": "x86_64", "devices": [ ... ] }
5.  DHCP (already done)    -->  IP address, gateway, DNS server
6.  DNS resolve            -->  Resolve marketplace API endpoint
7.  Connect to store       -->  HTTPS POST manifest to store.aljefra.com/api/match
8.  Receive driver list    -->  [ { "name": "nvidia-rtx4090", "url": "...", "sha256": "..." }, ... ]
9.  Download .ajdrv files  -->  Fetch each driver binary
10. Verify signatures      -->  Check SHA-256 hash and Ed25519 signature
11. Load into memory       -->  Map driver code, relocate symbols
12. Call driver init()     -->  Driver takes over its hardware
13. Report status          -->  Log success/failure for each driver
```

If the network is unavailable, the system continues with built-in drivers only and retries the AI bootstrap periodically.

---

## Key Statistics

| Metric | Value |
|--------|-------|
| Original lines of code | 67,295 (plus 101,889 vendored BearSSL) |
| Source files | 241 (excluding BearSSL) |
| Portable drivers | 22 |
| Supported architectures | 3 (x86-64, ARM64, RISC-V 64) |
| HAL header files | 9 |
| Network protocols | 7 (Ethernet, ARP, IPv4, UDP, TCP, DHCP, DNS) |
| Languages | C and Assembly |

---

## Further Reading

- [Boot Protocol](boot_protocol.md) -- Detailed boot sequence for each architecture
- [HAL Specification](hal_spec.md) -- Complete HAL API reference
- [Driver Guide](driver_guide.md) -- How to write AlJefra drivers
- [Marketplace Spec](marketplace_spec.md) -- Driver store protocol
- [Memory Maps](memory_maps.md) -- Physical and virtual memory layouts
- [Security Model](security_model.md) -- Security architecture
- [Porting Guide](porting_guide.md) -- Adding a new architecture

---

*AlJefra OS v1.0 -- Built in Qatar by [Qatar IT](https://www.qatarit.com)*
