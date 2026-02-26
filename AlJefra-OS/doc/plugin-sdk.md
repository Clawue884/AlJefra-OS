# AlJefra OS -- Plugin SDK Reference

## Overview

The AlJefra Plugin SDK allows developers to build loadable plugins for AlJefra
OS using the `.ajdrv` package format. Plugins are position-independent C
binaries that run inside the kernel address space and receive services through
a vtable (`kernel_api_t`).

Plugins can extend AlJefra OS with new hardware drivers, GUI extensions, or
system services -- all without recompiling the kernel.

---

## Table of Contents

1. [The .ajdrv Package Format](#the-ajdrv-package-format)
2. [Plugin Types](#plugin-types)
3. [Kernel API Reference (kernel_api_t)](#kernel-api-reference)
4. [Driver Operations (driver_ops_t)](#driver-operations)
5. [Build Requirements](#build-requirements)
6. [Build Process](#build-process)
7. [Example: Hello World Driver](#example-hello-world-driver)
8. [Example: Storage Driver Skeleton](#example-storage-driver-skeleton)
9. [Critical Rules](#critical-rules)
10. [Signing with Ed25519](#signing-with-ed25519)
11. [Testing in QEMU](#testing-in-qemu)
12. [Publishing to the Marketplace](#publishing-to-the-marketplace)
13. [Limitations and Pitfalls](#limitations-and-pitfalls)

---

## The .ajdrv Package Format

An `.ajdrv` file is a self-contained driver package with this layout:

```
Offset    Size      Field
------    ----      -----
0x00      4         Magic: "AJDR" (0x52444A41)
0x04      2         Format version (currently 1)
0x06      2         Architecture (0 = x86_64, 1 = aarch64, 2 = riscv64)
0x08      2         Vendor ID (PCI vendor or 0xFFFF for non-PCI)
0x0A      2         Device ID (PCI device or 0xFFFF for non-PCI)
0x0C      2         Category (see driver_category_t)
0x0E      2         Reserved (zero)
0x10      4         Metadata JSON offset (from start of file)
0x14      4         Metadata JSON length
0x18      4         Binary code offset (from start of file)
0x1C      4         Binary code length
0x20      4         Entry point offset (relative to binary start)
0x24      28        Reserved (zero)
0x40      variable  Metadata JSON (UTF-8, null-terminated)
...       variable  PIC binary (position-independent machine code)
...       64        Ed25519 signature over all preceding bytes
```

**Total header size:** 64 bytes.

The metadata JSON contains human-readable information:

```json
{
    "name": "my_driver",
    "version": "1.0.0",
    "description": "My custom hardware driver",
    "author": "Developer Name",
    "license": "MIT",
    "min_os_version": "1.0.0"
}
```

---

## Plugin Types

| Category ID | Enum                 | Description                         |
|-------------|----------------------|-------------------------------------|
| 0           | DRIVER_CAT_STORAGE   | Block storage (NVMe, AHCI, eMMC)   |
| 1           | DRIVER_CAT_NETWORK   | Network interface (Ethernet, WiFi)  |
| 2           | DRIVER_CAT_INPUT     | Input device (keyboard, mouse, touch)|
| 3           | DRIVER_CAT_DISPLAY   | Display / framebuffer               |
| 4           | DRIVER_CAT_GPU       | GPU compute / graphics              |
| 5           | DRIVER_CAT_BUS       | Bus controller (PCIe, USB host)     |
| 6           | DRIVER_CAT_OTHER     | System service, utility, extension  |

---

## Kernel API Reference

When a runtime driver is loaded, the kernel passes a pointer to `kernel_api_t`.
This is the **only** way for a plugin to access hardware and kernel services.

```c
typedef struct {
    /* Console output */
    void (*puts)(const char *s);

    /* MMIO register access */
    uint32_t (*mmio_read32)(volatile void *addr);
    void     (*mmio_write32)(volatile void *addr, uint32_t val);
    uint8_t  (*mmio_read8)(volatile void *addr);
    void     (*mmio_write8)(volatile void *addr, uint8_t val);
    void     (*mmio_barrier)(void);    /* Memory barrier / fence */

    /* DMA buffer management */
    void *(*dma_alloc)(uint64_t size, uint64_t *phys);
    void  (*dma_free)(void *ptr, uint64_t size);

    /* Timer services */
    void     (*delay_us)(uint64_t us);     /* Busy-wait delay */
    uint64_t (*timer_ms)(void);            /* Millisecond timestamp */

    /* PCI bus operations */
    void          (*pci_enable)(hal_device_t *dev);
    volatile void *(*map_bar)(hal_device_t *dev, uint32_t idx);
    uint32_t      (*pci_read32)(uint32_t bdf, uint32_t reg);
    void          (*pci_write32)(uint32_t bdf, uint32_t reg, uint32_t val);
} kernel_api_t;
```

### Function Details

| Function       | Description                                          |
|----------------|------------------------------------------------------|
| `puts`         | Write a null-terminated string to the kernel console |
| `mmio_read32`  | Read a 32-bit value from a memory-mapped register    |
| `mmio_write32` | Write a 32-bit value to a memory-mapped register     |
| `mmio_read8`   | Read an 8-bit value from a memory-mapped register    |
| `mmio_write8`  | Write an 8-bit value to a memory-mapped register     |
| `mmio_barrier` | Issue a memory fence (ensures ordering of MMIO ops)  |
| `dma_alloc`    | Allocate a DMA-capable buffer; returns virt+phys addr|
| `dma_free`     | Free a previously allocated DMA buffer               |
| `delay_us`     | Busy-wait for a given number of microseconds         |
| `timer_ms`     | Return current timestamp in milliseconds             |
| `pci_enable`   | Enable bus-mastering and memory space for a PCI dev  |
| `map_bar`      | Map a PCI BAR into kernel virtual address space      |
| `pci_read32`   | Read a 32-bit PCI config register (BDF + offset)     |
| `pci_write32`  | Write a 32-bit PCI config register (BDF + offset)    |

---

## Driver Operations

Every plugin must expose a `driver_ops_t` structure as its entry point.
The kernel calls the `init` function with the matched device.

```c
typedef struct {
    const char       *name;
    driver_category_t category;

    /* Lifecycle */
    hal_status_t    (*init)(hal_device_t *dev);
    void            (*shutdown)(void);

    /* Storage drivers */
    int64_t         (*read)(void *buf, uint64_t lba, uint32_t count);
    int64_t         (*write)(const void *buf, uint64_t lba, uint32_t count);

    /* Network drivers */
    int64_t         (*net_tx)(const void *frame, uint64_t len);
    int64_t         (*net_rx)(void *frame, uint64_t max_len);
    void            (*net_get_mac)(uint8_t mac[6]);

    /* Input drivers */
    int             (*input_poll)(void);   /* Returns keycode or -1 */
} driver_ops_t;
```

Set unused function pointers to `(void *)0` (NULL).

---

## Build Requirements

- **GCC** (native or cross-compiler for the target architecture)
- **Mandatory flags:** `-fPIC -fno-tree-vectorize`
- **Recommended flags:** `-ffreestanding -nostdlib -fno-builtin`
- **No standard library** -- all functionality comes through `kernel_api_t`
- **Position-independent code** -- the driver is loaded at an arbitrary address

### Architecture-Specific Compilers

| Architecture | Compiler                    | Extra flags                            |
|--------------|-----------------------------|----------------------------------------|
| x86_64       | `gcc`                       | `-m64 -march=x86-64 -mno-red-zone`    |
| aarch64      | `aarch64-linux-gnu-gcc`     | `-march=armv8-a`                       |
| riscv64      | `riscv64-linux-gnu-gcc`     | `-march=rv64gc -mabi=lp64d`           |

---

## Build Process

Use the provided build script:

```bash
cd drivers/runtime/

./build_ajdrv.sh <source.c> <arch> <vendor_id> <device_id> <category> <name> <desc>
```

**Parameters:**

| Parameter   | Description                              | Example          |
|-------------|------------------------------------------|------------------|
| source.c    | Path to the driver C source file         | `qemu_vga.c`    |
| arch        | Target architecture                      | `x86_64`         |
| vendor_id   | PCI vendor ID (hex without 0x)           | `1234`           |
| device_id   | PCI device ID (hex without 0x)           | `1111`           |
| category    | Category number (see table above)        | `3`              |
| name        | Short driver name                        | `qemu_vga`       |
| desc        | Human-readable description (quoted)      | `"QEMU VGA"`     |

**Example:**

```bash
./build_ajdrv.sh my_nic.c x86_64 8086 100e 1 intel_e1000 "Intel e1000 NIC"
```

This produces `my_nic.ajdrv` -- ready to sign and deploy.

---

## Example: Hello World Driver

A minimal driver that prints a message on load:

```c
/* SPDX-License-Identifier: MIT */
/* hello.c -- Minimal AlJefra OS driver example */

#include "../../kernel/driver_loader.h"

static const kernel_api_t *K;

static driver_ops_t g_ops;

static hal_status_t hello_init(hal_device_t *dev)
{
    K->puts("hello: driver loaded successfully!\n");
    return HAL_OK;
}

static void hello_shutdown(void)
{
    K->puts("hello: shutting down\n");
}

/* Entry point -- called by the kernel loader.
 * Returns a pointer to the driver_ops_t structure.
 */
driver_ops_t *ajdrv_entry(const kernel_api_t *api)
{
    K = api;

    /* IMPORTANT: Initialize ops at runtime, NOT with static initializer */
    g_ops.name     = "hello";
    g_ops.category = DRIVER_CAT_OTHER;
    g_ops.init     = hello_init;
    g_ops.shutdown = hello_shutdown;

    return &g_ops;
}
```

Build it:

```bash
./build_ajdrv.sh hello.c x86_64 ffff ffff 6 hello "Hello World driver"
```

---

## Example: Storage Driver Skeleton

```c
/* SPDX-License-Identifier: MIT */
/* my_storage.c -- Storage driver skeleton for AlJefra OS */

#include "../../kernel/driver_loader.h"

static const kernel_api_t *K;
static volatile void *regs;

static driver_ops_t g_ops;

static hal_status_t storage_init(hal_device_t *dev)
{
    K->pci_enable(dev);
    regs = K->map_bar(dev, 0);
    if (!regs) {
        K->puts("my_storage: BAR0 map failed\n");
        return HAL_ERR_IO;
    }

    /* Read device status register */
    uint32_t status = K->mmio_read32((volatile uint8_t *)regs + 0x04);
    K->puts("my_storage: device detected, status=0x");
    /* ... print hex ... */

    /* Reset device */
    K->mmio_write32((volatile uint8_t *)regs + 0x00, 0x01);
    K->delay_us(1000);
    K->mmio_barrier();

    /* Allocate DMA command buffer */
    uint64_t dma_phys;
    void *dma_buf = K->dma_alloc(4096, &dma_phys);
    if (!dma_buf) {
        K->puts("my_storage: DMA alloc failed\n");
        return HAL_ERR_NOMEM;
    }

    K->puts("my_storage: init complete\n");
    return HAL_OK;
}

static int64_t storage_read(void *buf, uint64_t lba, uint32_t count)
{
    /* Issue read command via MMIO registers */
    /* ... hardware-specific logic ... */
    return (int64_t)count;
}

static int64_t storage_write(const void *buf, uint64_t lba, uint32_t count)
{
    /* Issue write command via MMIO registers */
    /* ... hardware-specific logic ... */
    return (int64_t)count;
}

static void storage_shutdown(void)
{
    K->puts("my_storage: shutdown\n");
}

driver_ops_t *ajdrv_entry(const kernel_api_t *api)
{
    K = api;

    g_ops.name     = "my_storage";
    g_ops.category = DRIVER_CAT_STORAGE;
    g_ops.init     = storage_init;
    g_ops.shutdown = storage_shutdown;
    g_ops.read     = storage_read;
    g_ops.write    = storage_write;

    return &g_ops;
}
```

---

## Critical Rules

These rules prevent hard-to-debug failures in runtime-loaded drivers:

### 1. Never Use Static Initializers for Pointer Fields

**WRONG** (generates absolute addresses baked at link time):

```c
static driver_ops_t g_ops = {
    .name = "my_driver",
    .init = my_init,       /* ABSOLUTE ADDRESS -- will crash at runtime */
};
```

**CORRECT** (generates RIP-relative `lea` instructions):

```c
static driver_ops_t g_ops;

driver_ops_t *ajdrv_entry(const kernel_api_t *api)
{
    g_ops.name = "my_driver";
    g_ops.init = my_init;  /* Runtime assignment -- position-independent */
    return &g_ops;
}
```

### 2. Always Compile with -fPIC -fno-tree-vectorize

The `-fPIC` flag makes all code position-independent. The `-fno-tree-vectorize`
flag prevents GCC from auto-vectorizing loops, which can introduce absolute
address references.

### 3. No Standard Library

Runtime drivers have no libc. Use the `kernel_api_t` vtable for all I/O.
If you need `memcpy` or `memset`, implement them inline or request them
through the kernel API.

### 4. No Global Constructors

Do not rely on `__attribute__((constructor))` or C++ global constructors.
The kernel does not run `.init_array` for runtime drivers.

---

## Signing with Ed25519

All `.ajdrv` packages must be signed before they can be loaded by AlJefra OS.

### Signing Process

1. Generate a keypair (done once):
   ```bash
   # Using the AlJefra signing tool
   python3 server/sign_tool.py keygen --out my_key
   # Produces: my_key.pub (32 bytes) and my_key.sec (64 bytes)
   ```

2. Sign the package:
   ```bash
   python3 server/sign_tool.py sign --key my_key.sec --input my_driver.ajdrv
   # Appends 64-byte Ed25519 signature to the file
   ```

3. Verify (optional, for testing):
   ```bash
   python3 server/sign_tool.py verify --key my_key.pub --input my_driver.ajdrv
   ```

### Trust Model

The OS ships with the AlJefra Store public key embedded in the kernel. Only
packages signed with the corresponding private key (held by AlJefra / Qatar IT)
will pass verification. Third-party developers submit unsigned packages to the
marketplace; the store signs them after review.

See `doc/security_model.md` for the full trust chain documentation.

---

## Testing in QEMU

### Load a Runtime Driver

1. Start the marketplace server:
   ```bash
   cd server/
   python3 app.py
   ```

2. Upload your `.ajdrv` to the marketplace:
   ```bash
   curl -X POST http://localhost:5000/v1/drivers/upload \
     -F "file=@my_driver.ajdrv"
   ```

3. Boot AlJefra OS in QEMU with network access:
   ```bash
   qemu-system-x86_64 -machine q35 -cpu Westmere -m 256 -smp 1 \
     -kernel build/x86_64/bin/kernel_x86_64.bin -nographic \
     -netdev user,id=net0,hostfwd=tcp::5555-:80 \
     -device e1000,netdev=net0
   ```

4. The kernel will connect to the marketplace, discover your driver, download
   it, verify the signature, and load it. Watch the serial console for output.

### Debugging Tips

- Add `K->puts()` calls liberally for tracing.
- Use `K->timer_ms()` to measure initialization time.
- Check return values from all kernel API calls.
- If a driver crashes at load time, the most common cause is static
  initializers for pointer fields (see Critical Rules above).

---

## Publishing to the Marketplace

1. **Build** your driver with `build_ajdrv.sh`.
2. **Test** locally on QEMU.
3. **Submit** via GitHub pull request to the `marketplace-drivers` branch, or
   upload through the marketplace web UI at `os.aljefra.com/marketplace`.
4. **Review:** The AlJefra team will review your driver for safety, code
   quality, and compatibility.
5. **Sign and publish:** Once approved, the driver is signed with the store
   key and published to the marketplace.

### Submission Checklist

- [ ] Driver builds with `build_ajdrv.sh` without warnings
- [ ] Driver loads and initializes in QEMU
- [ ] No static initializers for function pointers
- [ ] All error paths handled (BAR mapping failure, DMA alloc failure, etc.)
- [ ] Source code includes SPDX license header
- [ ] Metadata JSON is complete (name, version, description, author, license)

---

## Limitations and Pitfalls

1. **No dynamic memory allocator** beyond `dma_alloc`. Plan your memory
   usage at init time.

2. **Single-threaded execution.** Drivers run in the context of the calling
   thread. Do not create threads or use locks.

3. **No interrupt registration** in the current SDK version. Drivers must use
   polling. Interrupt support is planned for SDK v2.

4. **8 KB download buffer.** Very large drivers may need to be split or
   compressed. This limit will be raised in a future release.

5. **Architecture-specific binaries.** You must build separate `.ajdrv` files
   for each target architecture (x86_64, aarch64, riscv64).

6. **VirtIO PCI capability parsing.** When parsing VirtIO PCI capabilities,
   `cfg_type` is byte 3 of dword 0 (`cap_hdr >> 24`), NOT byte 0 of dword 1.
   This has caused bugs in several early drivers.

---

*AlJefra OS Plugin SDK v1.0*
*Qatar IT -- www.QatarIT.com*
