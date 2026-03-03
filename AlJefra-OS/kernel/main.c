/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Architecture-Independent Kernel Entry
 *
 * Called after arch-specific boot code has:
 *   1. Set up CPU, MMU, interrupts, timer
 *   2. Initialized early console (UART/serial)
 *   3. Called hal_init()
 *
 * This module orchestrates the rest of the boot:
 *   Bus scan → driver matching → network → AI bootstrap → interactive
 */

#include "../hal/hal.h"
#include "sched.h"
#include "syscall.h"
#include "driver_loader.h"
#include "ai_bootstrap.h"

/* Forward declarations for subsystem init */
static void banner(void);
static void detect_hardware(void);
static void register_builtin_drivers(void);
static void load_builtin_drivers(void);
static void start_network(void);

/* Forward declarations for built-in driver registration */
extern void e1000_register(void);
extern void rtl8169_register(void);
extern void virtio_net_register(void);
extern void virtio_blk_register(void);
extern void ahci_register(void);
extern void nvme_register(void);
extern void touch_register(void);
extern void ufs_register(void);
extern void intel_wifi_register(void);
extern void xhci_register(void);
extern void bcm_wifi_register(void);

/* ── Hardware manifest (filled by bus scan) ── */
static hal_device_t g_devices[HAL_BUS_MAX_DEVICES];
static uint32_t     g_device_count;

/* ── Kernel entry point ── */
void kernel_main(void)
{
    banner();

    /* Phase 0: Register built-in driver ops tables */
    register_builtin_drivers();

    /* Phase 1: Hardware discovery */
    hal_console_puts("[kernel] Scanning buses...\n");
    detect_hardware();

    /* Phase 2: Load built-in drivers (compiled into kernel image) */
    hal_console_puts("[kernel] Loading built-in drivers...\n");
    load_builtin_drivers();

    /* Phase 3: Initialize scheduler */
    hal_console_puts("[kernel] Starting scheduler...\n");
    sched_init();

    /* Phase 4: Bring up network */
    hal_console_puts("[kernel] Starting network...\n");
    start_network();

    /* Phase 5: AI bootstrap — connect to marketplace, download drivers */
    hal_console_puts("[kernel] Starting AI bootstrap...\n");
    ai_bootstrap(g_devices, g_device_count);

    /* Phase 6: Interactive — kernel is fully up */
    hal_console_puts("[kernel] AlJefra OS ready.\n");

    /* Main loop: handle syscalls and interrupts */
    syscall_loop();

    /* Should never reach here */
    hal_console_puts("[kernel] PANIC: syscall_loop returned\n");
    for (;;)
        hal_cpu_halt();
}

/* ── Boot banner ── */
static void banner(void)
{
    hal_console_puts("\n");
    hal_console_puts("==============================================\n");
    hal_console_puts("  AlJefra OS — Universal Boot\n");
    hal_console_puts("  Architecture: ");

    switch (hal_arch()) {
    case HAL_ARCH_X86_64:  hal_console_puts("x86-64\n");  break;
    case HAL_ARCH_AARCH64: hal_console_puts("AArch64\n"); break;
    case HAL_ARCH_RISCV64: hal_console_puts("RISC-V 64\n"); break;
    }

    hal_cpu_info_t cpu;
    hal_cpu_get_info(&cpu);
    hal_console_puts("  CPU: ");
    hal_console_puts(cpu.model);
    hal_console_puts("\n");
    hal_console_printf("  Cores: %u\n", cpu.cores_logical);
    hal_console_printf("  RAM: %u MB\n", (uint32_t)(hal_mmu_total_ram() / (1024 * 1024)));
    hal_console_puts("==============================================\n\n");
}

/* ── Hardware discovery ── */
static void detect_hardware(void)
{
    g_device_count = hal_bus_scan(g_devices, HAL_BUS_MAX_DEVICES);
    hal_console_printf("[kernel] Found %u devices\n", g_device_count);

    for (uint32_t i = 0; i < g_device_count; i++) {
        hal_device_t *d = &g_devices[i];
        if (d->bus_type == HAL_BUS_PCIE) {
            hal_console_printf("  [%02x:%02x.%x] %04x:%04x class %02x:%02x\n",
                               d->bus, d->dev, d->func,
                               d->vendor_id, d->device_id,
                               d->class_code, d->subclass);
        }
    }
}

/* ── PCI class codes for driver matching ── */
#define PCI_CLASS_STORAGE      0x01
#define PCI_CLASS_NETWORK      0x02
#define PCI_CLASS_DISPLAY      0x03
#define PCI_CLASS_SERIAL_BUS   0x0C

#define PCI_SUBCLASS_NVME      0x08
#define PCI_SUBCLASS_AHCI      0x06
#define PCI_SUBCLASS_UFS       0x09
#define PCI_SUBCLASS_ETHERNET  0x00
#define PCI_SUBCLASS_USB       0x03

/* ── Register all built-in driver ops tables ──
 *
 * These 11 "registered" drivers implement driver_ops_t and are matched by
 * PCI vendor/device ID in load_builtin_drivers() below.
 *
 * The remaining 11 "library" drivers (ps2, usb_hid, lfb, serial_console,
 * acpi_lite, dt_parser, pcie, emmc, wifi_framework, aes_ccmp) provide
 * utility APIs called directly by the kernel — they do not use this
 * registration path.  See kernel/driver_loader.h for the full list.
 */
static void register_builtin_drivers(void)
{
    e1000_register();
    rtl8169_register();
    virtio_net_register();
    virtio_blk_register();
    ahci_register();
    nvme_register();
    touch_register();
    ufs_register();
    intel_wifi_register();
    xhci_register();
    bcm_wifi_register();
}

/* ── Load built-in (compiled-in) drivers ── */
static void load_builtin_drivers(void)
{
    uint32_t loaded = 0;

    for (uint32_t i = 0; i < g_device_count; i++) {
        hal_device_t *d = &g_devices[i];
        hal_status_t rc;

        /* Storage: NVMe */
        if (d->class_code == PCI_CLASS_STORAGE && d->subclass == PCI_SUBCLASS_NVME) {
            rc = driver_load_builtin("nvme", d);
            if (rc == HAL_OK) loaded++;
        }

        /* Storage: AHCI/SATA */
        if (d->class_code == PCI_CLASS_STORAGE && d->subclass == PCI_SUBCLASS_AHCI) {
            rc = driver_load_builtin("ahci", d);
            if (rc == HAL_OK) loaded++;
        }

        /* Network: Intel e1000/e1000e */
        if (d->class_code == PCI_CLASS_NETWORK && d->subclass == PCI_SUBCLASS_ETHERNET) {
            if (d->vendor_id == 0x8086) {
                rc = driver_load_builtin("e1000", d);
                if (rc == HAL_OK) loaded++;
            }
        }

        /* Network: Realtek RTL8169/RTL8168/RTL8111/RTL8101E */
        if (d->class_code == PCI_CLASS_NETWORK && d->subclass == PCI_SUBCLASS_ETHERNET) {
            if (d->vendor_id == 0x10EC &&
                (d->device_id == 0x8136 || d->device_id == 0x8161 ||
                 d->device_id == 0x8168 || d->device_id == 0x8169)) {
                rc = driver_load_builtin("rtl8169", d);
                if (rc == HAL_OK) loaded++;
            }
        }

        /* Network: VirtIO-Net (legacy 0x1000 + modern 0x1041) */
        if (d->vendor_id == 0x1AF4 &&
            (d->device_id == 0x1000 || d->device_id == 0x1041)) {
            if (d->class_code == PCI_CLASS_NETWORK) {
                rc = driver_load_builtin("virtio-net", d);
                if (rc == HAL_OK) loaded++;
            }
        }

        /* Storage: VirtIO-Blk (legacy 0x1001 + modern 0x1042) */
        if (d->vendor_id == 0x1AF4 &&
            (d->device_id == 0x1001 || d->device_id == 0x1042)) {
            rc = driver_load_builtin("virtio-blk", d);
            if (rc == HAL_OK) loaded++;
        }

        /* Storage: UFS */
        if (d->class_code == PCI_CLASS_STORAGE && d->subclass == PCI_SUBCLASS_UFS) {
            rc = driver_load_builtin("ufs", d);
            if (rc == HAL_OK) loaded++;
        }

        /* Network: Intel WiFi (AX200/AX210) */
        if (d->class_code == PCI_CLASS_NETWORK && d->vendor_id == 0x8086 &&
            (d->device_id == 0x2723 || d->device_id == 0x2725)) {
            rc = driver_load_builtin("intel-wifi", d);
            if (rc == HAL_OK) loaded++;
        }

        /* USB: xHCI */
        if (d->class_code == PCI_CLASS_SERIAL_BUS && d->subclass == PCI_SUBCLASS_USB) {
            if (d->prog_if == 0x30) { /* xHCI */
                rc = driver_load_builtin("xhci", d);
                if (rc == HAL_OK) loaded++;
            }
        }

        /* Network: Broadcom WiFi (SDIO, via Device Tree on RPi) */
        if ((d->bus_type == HAL_BUS_DT || d->bus_type == HAL_BUS_MMIO) &&
            d->compatible[0] != '\0') {
            const char *c = d->compatible;
            int is_brcm = 0;
            for (int j = 0; c[j] && c[j+1] && c[j+2] && c[j+3]; j++) {
                if (c[j] == 'b' && c[j+1] == 'r' && c[j+2] == 'c' && c[j+3] == 'm') {
                    is_brcm = 1;
                    break;
                }
            }
            if (is_brcm) {
                rc = driver_load_builtin("bcm_wifi", d);
                if (rc == HAL_OK) loaded++;
            }
        }

        /* Input: Touchscreen — only try on I2C controller DT nodes,
         * NOT on every platform device (avoids MMIO hangs on UART/PLIC/etc) */
        if ((d->bus_type == HAL_BUS_DT || d->bus_type == HAL_BUS_MMIO) &&
            d->compatible[0] != '\0') {
            /* Check if this is an I2C controller */
            int is_i2c = 0;
            const char *c = d->compatible;
            /* Look for "i2c" substring in compatible string */
            for (int j = 0; c[j] && c[j+1] && c[j+2]; j++) {
                if (c[j] == 'i' && c[j+1] == '2' && c[j+2] == 'c') {
                    is_i2c = 1;
                    break;
                }
            }
            if (is_i2c) {
                rc = driver_load_builtin("touch", d);
                if (rc == HAL_OK) loaded++;
            }
        }
    }

    hal_console_printf("[kernel] Loaded %u built-in drivers\n", loaded);

    /* Quick storage test — write a pattern then read it back */
    const driver_ops_t *stor = driver_get_storage();
    if (stor && stor->read && stor->write) {
        hal_console_printf("[kernel] Testing storage driver '%s'...\n", stor->name);

        uint64_t phys;
        uint8_t *buf = (uint8_t *)hal_dma_alloc(512, &phys);
        uint8_t *buf2 = (uint8_t *)hal_dma_alloc(512, &phys);
        if (buf && buf2) {
            /* Write a known pattern to LBA 0 */
            for (int i = 0; i < 512; i++)
                buf[i] = (uint8_t)(0xA5 ^ i);

            int64_t wr = stor->write(buf, 0, 1);
            if (wr > 0) {
                /* Clear read buffer */
                for (int i = 0; i < 512; i++)
                    buf2[i] = 0;

                /* Read it back */
                int64_t rd = stor->read(buf2, 0, 1);
                if (rd > 0) {
                    /* Verify pattern */
                    int ok = 1;
                    for (int i = 0; i < 512; i++) {
                        if (buf2[i] != (uint8_t)(0xA5 ^ i)) {
                            ok = 0;
                            hal_console_printf("[kernel] Storage MISMATCH at byte %d: got 0x%02x expected 0x%02x\n",
                                               i, buf2[i], (uint8_t)(0xA5 ^ i));
                            break;
                        }
                    }
                    if (ok)
                        hal_console_puts("[kernel] Storage READ/WRITE test PASSED\n");
                } else {
                    hal_console_puts("[kernel] Storage read failed\n");
                }
            } else {
                hal_console_puts("[kernel] Storage write failed\n");
            }
        }
    }
}

/* ── Network startup ── */
static void start_network(void)
{
    /* The AI bootstrap module handles DHCP + full network bringup */
    hal_console_puts("[kernel] Network will be configured by AI bootstrap\n");
}
