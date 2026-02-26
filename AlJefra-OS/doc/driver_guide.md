# AlJefra OS — Driver Development Guide

## Overview

AlJefra OS drivers are portable C modules that use the HAL interface. A driver never contains inline assembly — all hardware access goes through `hal_mmio_*`, `hal_bus_*`, `hal_irq_*`, and other HAL functions.

## Driver Structure

Every driver implements the `driver_ops_t` interface defined in `kernel/driver_loader.h`:

```c
typedef struct {
    const char       *name;       // "nvme", "e1000", "xhci"
    driver_category_t category;   // DRIVER_CAT_STORAGE, _NETWORK, etc.

    hal_status_t    (*init)(hal_device_t *dev);  // Initialize for device
    void            (*shutdown)(void);            // Clean shutdown

    // Storage drivers:
    int64_t         (*read)(void *buf, uint64_t lba, uint32_t count);
    int64_t         (*write)(const void *buf, uint64_t lba, uint32_t count);

    // Network drivers:
    int64_t         (*net_tx)(const void *frame, uint64_t len);
    int64_t         (*net_rx)(void *frame, uint64_t max_len);
    void            (*net_get_mac)(uint8_t mac[6]);

    // Input drivers:
    int             (*input_poll)(void);  // Returns keycode or -1
} driver_ops_t;
```

## Writing a Driver

### 1. Create header and source files

```
drivers/<category>/<name>.h
drivers/<category>/<name>.c
```

### 2. Define hardware registers

Use packed structs for MMIO register blocks:

```c
typedef struct __attribute__((packed)) {
    volatile uint32_t CTRL;     // 0x00: Device control
    volatile uint32_t STATUS;   // 0x04: Device status
    // ...
} my_device_regs_t;
```

### 3. Implement init function

```c
static my_device_regs_t *g_regs;

static hal_status_t my_driver_init(hal_device_t *dev)
{
    // Enable PCI bus-mastering and memory space
    hal_bus_pci_enable(dev);

    // Map BAR0
    g_regs = (my_device_regs_t *)hal_bus_map_bar(dev, 0);
    if (!g_regs) return HAL_ERROR;

    // Reset device
    hal_mmio_write32(&g_regs->CTRL, CTRL_RESET);
    hal_timer_delay_us(100);

    // Wait for reset complete
    uint64_t start = hal_timer_ms();
    while (hal_mmio_read32(&g_regs->STATUS) & STATUS_RESET) {
        if (hal_timer_ms() - start > 1000) return HAL_TIMEOUT;
    }

    // Register interrupt handler
    hal_irq_register(dev->irq, my_irq_handler, NULL);
    hal_irq_enable(dev->irq);

    return HAL_OK;
}
```

### 4. Register as built-in driver

```c
static const driver_ops_t my_driver_ops = {
    .name     = "my_driver",
    .category = DRIVER_CAT_STORAGE,
    .init     = my_driver_init,
    .shutdown = my_driver_shutdown,
    .read     = my_driver_read,
    .write    = my_driver_write,
};

// Called during kernel startup
void my_driver_register(void)
{
    driver_register_builtin(&my_driver_ops);
}
```

## Hardware Access Rules

1. **MMIO only**: Use `hal_mmio_read32()` / `hal_mmio_write32()`, never raw pointer dereference
2. **Barriers**: Call `hal_mmio_barrier()` between writes that must be ordered
3. **DMA buffers**: Allocate with `hal_dma_alloc()`, never use stack/global buffers for DMA
4. **Timeouts**: Every hardware wait loop must have a timeout using `hal_timer_ms()`
5. **No busy-wait**: Use `hal_timer_delay_us()` for short waits, not empty loops
6. **Interrupts**: Register handlers via `hal_irq_register()`, acknowledge via `hal_irq_eoi()`

## Driver Categories

| Category | Ops Required | Examples |
|----------|-------------|---------|
| `DRIVER_CAT_STORAGE` | `init`, `read`, `write` | NVMe, AHCI, VirtIO-Blk, eMMC |
| `DRIVER_CAT_NETWORK` | `init`, `net_tx`, `net_rx`, `net_get_mac` | e1000, VirtIO-Net |
| `DRIVER_CAT_INPUT` | `init`, `input_poll` | xHCI+USB HID, PS/2 |
| `DRIVER_CAT_DISPLAY` | `init` | Framebuffer, serial console |
| `DRIVER_CAT_GPU` | `init` | NVIDIA GPU |
| `DRIVER_CAT_BUS` | `init` | PCIe, Device Tree, ACPI |

## Building as .ajdrv Package

Runtime drivers are distributed as `.ajdrv` files. To create one:

1. Compile the driver to a position-independent binary:
   ```bash
   gcc -ffreestanding -fPIC -c my_driver.c -o my_driver.o
   ld -shared -o my_driver.bin my_driver.o
   ```

2. Create the package with the `ajdrv_pack` tool:
   ```bash
   ajdrv_pack --name "my_driver" --arch x86_64 \
              --vendor 0x1234 --device 0x5678 \
              --code my_driver.bin --key private.key \
              --output my_driver.ajdrv
   ```

The entry point must be a function returning `const driver_ops_t *`.

## Testing

1. **QEMU**: Add the device to QEMU command line and test in the full OS
2. **Unit test**: Mock the HAL functions and test driver logic in isolation
3. **Hardware**: Test on physical devices via the boot image
