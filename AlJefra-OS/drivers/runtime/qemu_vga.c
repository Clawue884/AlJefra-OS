/* SPDX-License-Identifier: MIT */
/* AlJefra OS — QEMU Standard VGA Runtime Driver (.ajdrv)
 *
 * Supports: QEMU Standard VGA (vendor 1234, device 1111)
 * Uses Bochs VBE interface at I/O ports 0x01CE/0x01CF.
 *
 * This file is compiled as position-independent code and packaged
 * into a .ajdrv binary for runtime loading by the kernel.
 *
 * Entry point: driver_entry() — receives kernel_api_t*, returns driver_ops_t*
 */

#include "../../kernel/driver_loader.h"

/* ── Bochs VBE constants ── */
#define VBE_INDEX_PORT  0x01CE
#define VBE_DATA_PORT   0x01CF

#define VBE_INDEX_ID           0x00
#define VBE_INDEX_XRES         0x01
#define VBE_INDEX_YRES         0x02
#define VBE_INDEX_BPP          0x03
#define VBE_INDEX_ENABLE       0x04
#define VBE_INDEX_BANK         0x05
#define VBE_INDEX_VIRT_WIDTH   0x06
#define VBE_INDEX_VIRT_HEIGHT  0x07
#define VBE_INDEX_X_OFFSET     0x08
#define VBE_INDEX_Y_OFFSET     0x09

#define VBE_DISPI_DISABLED     0x00
#define VBE_DISPI_ENABLED      0x01
#define VBE_DISPI_LFB_ENABLED  0x40

/* ── x86 I/O port helpers (inline asm, no HAL needed) ── */

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static void vbe_write(uint16_t index, uint16_t data)
{
    outw(VBE_INDEX_PORT, index);
    outw(VBE_DATA_PORT, data);
}

static uint16_t vbe_read(uint16_t index)
{
    outw(VBE_INDEX_PORT, index);
    return inw(VBE_DATA_PORT);
}

/* ── Driver state ── */

static const kernel_api_t *g_kapi;
static volatile uint32_t  *g_framebuffer;
static uint32_t            g_fb_size;
static uint16_t            g_width;
static uint16_t            g_height;
static uint16_t            g_bpp;
static int                 g_initialized;

/* ── Driver operations ── */

static hal_status_t qemu_vga_init(hal_device_t *dev)
{
    if (!g_kapi)
        return HAL_ERROR;

    /* Enable PCI memory + bus mastering */
    g_kapi->pci_enable(dev);

    /* Map BAR0 — VRAM framebuffer */
    volatile void *bar0 = g_kapi->map_bar(dev, 0);
    if (!bar0)
        return HAL_ERROR;

    g_framebuffer = (volatile uint32_t *)bar0;

    /* Check VBE version */
    uint16_t vbe_id = vbe_read(VBE_INDEX_ID);
    if (vbe_id < 0xB0C0) {
        g_kapi->puts("[qemu-vga] VBE not detected\n");
        return HAL_NO_DEVICE;
    }

    /* Set 800x600x32 mode */
    g_width  = 800;
    g_height = 600;
    g_bpp    = 32;

    vbe_write(VBE_INDEX_ENABLE, VBE_DISPI_DISABLED);
    vbe_write(VBE_INDEX_XRES, g_width);
    vbe_write(VBE_INDEX_YRES, g_height);
    vbe_write(VBE_INDEX_BPP, g_bpp);
    vbe_write(VBE_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    g_fb_size = (uint32_t)g_width * g_height * (g_bpp / 8);

    /* Fill screen with dark blue to show driver is alive */
    uint32_t color = 0x001A3366;  /* Dark blue */
    for (uint32_t i = 0; i < (uint32_t)g_width * g_height; i++)
        g_framebuffer[i] = color;

    g_initialized = 1;
    g_kapi->puts("[qemu-vga] Display initialized: 800x600x32\n");

    return HAL_OK;
}

static void qemu_vga_shutdown(void)
{
    vbe_write(VBE_INDEX_ENABLE, VBE_DISPI_DISABLED);
    g_initialized = 0;
}

/* ── Driver name and ops table ── */

static const char driver_name[] = "qemu-vga";

/* Runtime-initialized ops table — must NOT use static initializers for
 * pointer fields because those become absolute addresses resolved at
 * link time (address 0).  Instead, we fill it at runtime using
 * RIP-relative addressing which produces correct relocated pointers. */
static driver_ops_t g_ops;

/* ── Entry point — called by kernel driver loader ── */

const driver_ops_t *driver_entry(const kernel_api_t *api)
{
    g_kapi = api;

    /* Initialize ops table at runtime so function/string pointers
     * are computed via RIP-relative addressing (position-independent). */
    g_ops.name     = driver_name;
    g_ops.category = DRIVER_CAT_DISPLAY;
    g_ops.init     = qemu_vga_init;
    g_ops.shutdown = qemu_vga_shutdown;

    return &g_ops;
}
