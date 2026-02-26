/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Broadcom WiFi Driver (BCM43455/BCM43456)
 *
 * Supports Broadcom FullMAC WiFi chips commonly found on:
 *   - Raspberry Pi 3B+/4/5 (BCM43455, SDIO)
 *   - Raspberry Pi Zero 2W (BCM43436)
 *   - Various ARM SBCs and tablets
 *
 * Architecture-independent; uses HAL for all hardware access.
 *
 * This is a FullMAC driver — the firmware on the chip handles 802.11
 * management frames, scanning, and association. The host sends ioctl
 * commands to the firmware and sends/receives Ethernet frames through
 * a shared ring buffer over SDIO.
 *
 * Key register/interface:
 *   - SDIO transport (CCCR, FBR, F2 data pipe)
 *   - Backplane access via SDIO F1
 *   - D11 core for DMA TX/RX rings
 *   - Firmware ioctl interface for configuration
 */

#include "../../hal/hal.h"
#include "../../kernel/driver_loader.h"
#include "../../lib/string.h"
#include "wifi_framework.h"

/* ── Broadcom SDIO Constants ── */

/* SDIO function numbers */
#define SDIO_FN_BACKPLANE     1   /* F1: backplane access */
#define SDIO_FN_WLAN          2   /* F2: WLAN data */

/* CCCR (Common Card Common Register) addresses */
#define SDIO_CCCR_IORDY      0x00
#define SDIO_CCCR_IOEN       0x02  /* I/O Enable */
#define SDIO_CCCR_IORDY_REG  0x03  /* I/O Ready */
#define SDIO_CCCR_INTEN       0x04  /* Interrupt Enable */
#define SDIO_CCCR_BUSPEED    0x13  /* Bus Speed Select */

/* Backplane address window registers (via F1) */
#define SBSDIO_FUNC1_SBADDR_LOW   0x1000A
#define SBSDIO_FUNC1_SBADDR_MID   0x1000B
#define SBSDIO_FUNC1_SBADDR_HIGH  0x1000C

/* Broadcom chip IDs */
#define BCM43455_CHIP_ID      0x4345  /* RPi 3B+/4 */
#define BCM43456_CHIP_ID      0x4356  /* RPi 5 variant */
#define BCM43436_CHIP_ID      0xA9BF  /* RPi Zero 2W */

/* Backplane core IDs */
#define CORE_ARM_CM3          0x82A
#define CORE_80211            0x812
#define CORE_SDIO_DEV         0x829
#define CORE_CHIPCOMMON       0x800

/* Chip common registers */
#define CHIPCOMMON_BASE        0x18000000
#define CC_CHIPID              0x00
#define CC_GPIOPULLUP          0x58
#define CC_GPIOPULLDOWN        0x5C

/* SDIO host-device shared control registers (via backplane) */
#define SDIO_INT_STATUS       0x20
#define SDIO_INT_HOST_MASK    0x24
#define SDIO_FUNC_INT_MASK    0x34
#define SDIO_TO_SB_MAILBOX    0x40
#define SDIO_TO_HOST_MAILBOX  0x44

/* Mailbox bits */
#define SMB_NAK               (1u << 0)
#define SMB_INT_ACK           (1u << 1)
#define SMB_USE_OOB           (1u << 2)
#define SMB_DEV_INT           (1u << 3)

#define HMB_FC_ON             (1u << 0)
#define HMB_FC_CHANGE         (1u << 1)
#define HMB_FRAME_IND         (1u << 2)
#define HMB_HOST_INT          (1u << 3)

/* BDC (Broadcom Dongle Communication) protocol header */
#define BDC_HEADER_LEN        4
#define BDC_FLAG_VER_MASK     0xF0
#define BDC_FLAG_VER_SHIFT    4
#define BDC_VERSION           2
#define BDC_FLAG2_IF_MASK     0x0F

/* CDC (Control Data Channel) ioctl header */
#define CDC_HEADER_LEN        16
#define CDC_SET_CMD           0x02
#define CDC_GET_CMD           0x00
#define CDC_IF_CMD            0x4000

/* Common ioctl commands */
#define WLC_GET_VERSION       1
#define WLC_UP                2
#define WLC_DOWN              3
#define WLC_SET_INFRA         20
#define WLC_SET_AUTH          22
#define WLC_GET_BSSID         23
#define WLC_GET_SSID          25
#define WLC_SET_SSID          26
#define WLC_SET_CHANNEL       30
#define WLC_DISASSOC          52
#define WLC_SET_COUNTRY       84
#define WLC_GET_VAR           262
#define WLC_SET_VAR           263
#define WLC_SET_WSEC          134
#define WLC_SET_WPA_AUTH      165
#define WLC_SET_WSEC_PMK      268
#define WLC_SCB_AUTHORIZE     288

/* Security modes */
#define WSEC_NONE             0
#define WSEC_WEP              1
#define WSEC_TKIP             2
#define WSEC_AES              4
#define WPA_AUTH_PSK           0x0004
#define WPA2_AUTH_PSK          0x0080

/* Maximum frame size */
#define BCM_MAX_FRAME         2048
#define BCM_MAX_SSID_LEN      32

/* ── Broadcom WiFi device state ── */

typedef struct {
    volatile void  *sdio_regs;      /* SDIO controller MMIO base */
    uint32_t        chip_id;        /* Broadcom chip ID */
    uint32_t        chip_rev;       /* Chip revision */

    /* Backplane window tracking */
    uint32_t        backplane_addr; /* Current backplane window base */

    /* SDIO DMA buffers */
    void           *tx_buf;         /* TX frame buffer (DMA) */
    uint64_t        tx_buf_phys;
    void           *rx_buf;         /* RX frame buffer (DMA) */
    uint64_t        rx_buf_phys;
    void           *ctl_buf;        /* Control (ioctl) buffer (DMA) */
    uint64_t        ctl_buf_phys;

    /* MAC address (from firmware) */
    uint8_t         mac[6];

    /* Ioctl sequence counter */
    uint16_t        ioctl_id;

    /* State */
    bool            fw_loaded;
    bool            up;
    bool            associated;

    /* WiFi framework context */
    wifi_ctx_t      wifi_ctx;

    /* HAL device */
    hal_device_t    hal_dev;
} bcm_wifi_dev_t;

/* MMIO register access through SDIO controller */
static inline uint32_t bcm_read32(bcm_wifi_dev_t *dev, uint32_t off)
{
    return hal_mmio_read32((volatile void *)((uint8_t *)dev->sdio_regs + off));
}

static inline void bcm_write32(bcm_wifi_dev_t *dev, uint32_t off, uint32_t val)
{
    hal_mmio_write32((volatile void *)((uint8_t *)dev->sdio_regs + off), val);
}

static inline uint8_t bcm_read8(bcm_wifi_dev_t *dev, uint32_t off)
{
    return hal_mmio_read8((volatile void *)((uint8_t *)dev->sdio_regs + off));
}

static inline void bcm_write8(bcm_wifi_dev_t *dev, uint32_t off, uint8_t val)
{
    hal_mmio_write8((volatile void *)((uint8_t *)dev->sdio_regs + off), val);
}

/* ── Backplane Access ── */

/* Set the backplane address window to access a specific 32KB region */
static void bcm_set_backplane_window(bcm_wifi_dev_t *dev, uint32_t addr)
{
    uint32_t base = addr & 0xFFFF8000;  /* 32KB aligned */
    if (base == dev->backplane_addr)
        return;

    bcm_write8(dev, SBSDIO_FUNC1_SBADDR_LOW, (uint8_t)(base >> 8));
    bcm_write8(dev, SBSDIO_FUNC1_SBADDR_MID, (uint8_t)(base >> 16));
    bcm_write8(dev, SBSDIO_FUNC1_SBADDR_HIGH, (uint8_t)(base >> 24));
    dev->backplane_addr = base;
}

/* Read a 32-bit value from the backplane */
static uint32_t bcm_bp_read32(bcm_wifi_dev_t *dev, uint32_t addr)
{
    bcm_set_backplane_window(dev, addr);
    uint32_t off = addr & 0x7FFF;  /* Offset within 32KB window */
    return bcm_read32(dev, off);
}

/* Write a 32-bit value to the backplane */
static void bcm_bp_write32(bcm_wifi_dev_t *dev, uint32_t addr, uint32_t val)
{
    bcm_set_backplane_window(dev, addr);
    uint32_t off = addr & 0x7FFF;
    bcm_write32(dev, off, val);
}

/* ── Chip Identification ── */

static hal_status_t bcm_identify(bcm_wifi_dev_t *dev)
{
    uint32_t chipid = bcm_bp_read32(dev, CHIPCOMMON_BASE + CC_CHIPID);
    dev->chip_id = chipid & 0xFFFF;
    dev->chip_rev = (chipid >> 16) & 0x0F;

    const char *name = "unknown";
    switch (dev->chip_id) {
    case BCM43455_CHIP_ID: name = "BCM43455"; break;
    case BCM43456_CHIP_ID: name = "BCM43456"; break;
    case BCM43436_CHIP_ID: name = "BCM43436"; break;
    }

    hal_console_printf("[bcm_wifi] Chip: %s rev %u (0x%04x)\n",
                       name, dev->chip_rev, dev->chip_id);

    if (dev->chip_id != BCM43455_CHIP_ID &&
        dev->chip_id != BCM43456_CHIP_ID &&
        dev->chip_id != BCM43436_CHIP_ID) {
        hal_console_printf("[bcm_wifi] Unsupported chip ID: 0x%04x\n", dev->chip_id);
        return HAL_NOT_SUPPORTED;
    }

    return HAL_OK;
}

/* ── SDIO Interface Initialization ── */

static hal_status_t bcm_sdio_init(bcm_wifi_dev_t *dev)
{
    /* Enable F1 (backplane) and F2 (WLAN) functions */
    bcm_write8(dev, SDIO_CCCR_IOEN, 0x06);  /* Enable F1 + F2 */

    /* Wait for functions to become ready */
    uint64_t deadline = hal_timer_ms() + 3000;
    while (hal_timer_ms() < deadline) {
        uint8_t ready = bcm_read8(dev, SDIO_CCCR_IORDY_REG);
        if ((ready & 0x06) == 0x06) {
            hal_console_puts("[bcm_wifi] SDIO F1+F2 ready\n");
            break;
        }
        hal_timer_delay_us(1000);
    }

    /* Enable high-speed mode if supported */
    uint8_t speed = bcm_read8(dev, SDIO_CCCR_BUSPEED);
    if (speed & 0x01) {  /* SHS (Support High Speed) */
        speed |= 0x02;   /* EHS (Enable High Speed) */
        bcm_write8(dev, SDIO_CCCR_BUSPEED, speed);
        hal_console_puts("[bcm_wifi] High-speed SDIO enabled\n");
    }

    /* Enable interrupts for F1 and F2 */
    bcm_write8(dev, SDIO_CCCR_INTEN, 0x07);  /* Master + F1 + F2 */

    return HAL_OK;
}

/* ── Firmware Ioctl Interface ── */

/* Send a control (ioctl) command to the firmware.
 * cmd: WLC_* command number
 * set: 1 for SET, 0 for GET
 * data/data_len: payload to send/receive
 * Returns HAL_OK on success. */
static hal_status_t bcm_ioctl(bcm_wifi_dev_t *dev, uint32_t cmd,
                                bool set, void *data, uint32_t data_len)
{
    if (!dev->ctl_buf)
        return HAL_ERROR;

    uint8_t *buf = (uint8_t *)dev->ctl_buf;
    memset(buf, 0, CDC_HEADER_LEN + data_len);

    /* Build CDC header (16 bytes) */
    uint32_t *hdr = (uint32_t *)buf;
    hdr[0] = cmd;                                /* ioctl command */
    hdr[1] = data_len;                           /* output buffer length */
    hdr[2] = (set ? CDC_SET_CMD : CDC_GET_CMD);  /* flags */
    hdr[2] |= (uint32_t)(dev->ioctl_id++ << 16); /* sequence id */
    hdr[3] = 0;                                  /* status (returned by fw) */

    /* Copy data payload after header */
    if (data && data_len > 0 && set)
        memcpy(buf + CDC_HEADER_LEN, data, data_len);

    /* Send via SDIO F2 control pipe */
    uint32_t total = CDC_HEADER_LEN + data_len;
    /* Round up to 4-byte boundary */
    total = (total + 3) & ~3u;

    /* Write to F2 data pipe */
    for (uint32_t i = 0; i < total / 4; i++) {
        bcm_write32(dev, 0x8000 + i * 4, ((uint32_t *)buf)[i]);
    }
    hal_mmio_barrier();

    /* Signal firmware: ring the doorbell */
    bcm_bp_write32(dev, SDIO_TO_SB_MAILBOX, SMB_DEV_INT);

    /* Wait for response */
    uint64_t deadline = hal_timer_ms() + 2000;
    while (hal_timer_ms() < deadline) {
        uint32_t mailbox = bcm_bp_read32(dev, SDIO_TO_HOST_MAILBOX);
        if (mailbox & HMB_FRAME_IND) {
            /* Acknowledge */
            bcm_bp_write32(dev, SDIO_TO_HOST_MAILBOX, mailbox);

            /* Read response from F2 */
            for (uint32_t i = 0; i < total / 4; i++) {
                ((uint32_t *)buf)[i] = bcm_read32(dev, 0x8000 + i * 4);
            }

            /* Check status in CDC header */
            uint32_t status = hdr[3];
            if (status != 0) {
                hal_console_printf("[bcm_wifi] ioctl %u failed: status=%u\n",
                                   cmd, status);
                return HAL_ERROR;
            }

            /* Copy response data back if GET */
            if (data && data_len > 0 && !set)
                memcpy(data, buf + CDC_HEADER_LEN, data_len);

            return HAL_OK;
        }
        hal_timer_delay_us(500);
    }

    hal_console_printf("[bcm_wifi] ioctl %u timed out\n", cmd);
    return HAL_TIMEOUT;
}

/* Send a named variable ioctl (WLC_GET_VAR / WLC_SET_VAR) */
static hal_status_t bcm_iovar(bcm_wifi_dev_t *dev, const char *name,
                                bool set, void *data, uint32_t data_len)
{
    uint32_t name_len = str_len(name) + 1;  /* Include null terminator */
    uint32_t total = name_len + data_len;
    if (total > 1024)
        return HAL_ERROR;

    uint8_t var_buf[1024];
    memset(var_buf, 0, sizeof(var_buf));

    /* iovar format: "name\0" + data */
    memcpy(var_buf, name, name_len);
    if (data && data_len > 0)
        memcpy(var_buf + name_len, data, data_len);

    uint32_t cmd = set ? WLC_SET_VAR : WLC_GET_VAR;
    return bcm_ioctl(dev, cmd, set, var_buf, total);
}

/* ── WiFi Operations ── */

/* Read MAC address from firmware */
static hal_status_t bcm_get_mac_from_fw(bcm_wifi_dev_t *dev)
{
    uint8_t mac_buf[6];
    memset(mac_buf, 0, 6);

    hal_status_t st = bcm_iovar(dev, "cur_etheraddr", false, mac_buf, 6);
    if (st != HAL_OK)
        return st;

    memcpy(dev->mac, mac_buf, 6);
    hal_console_printf("[bcm_wifi] MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                       dev->mac[0], dev->mac[1], dev->mac[2],
                       dev->mac[3], dev->mac[4], dev->mac[5]);
    return HAL_OK;
}

/* Bring the firmware interface UP */
static hal_status_t bcm_fw_up(bcm_wifi_dev_t *dev)
{
    /* Set infrastructure mode (STA) */
    uint32_t infra = 1;
    hal_status_t st = bcm_ioctl(dev, WLC_SET_INFRA, true, &infra, 4);
    if (st != HAL_OK)
        return st;

    /* Set country code to "XX" (world-safe) */
    char country[12];
    memset(country, 0, sizeof(country));
    country[0] = 'X'; country[1] = 'X';
    country[4] = 'X'; country[5] = 'X';
    /* Rev = 0 */
    bcm_ioctl(dev, WLC_SET_COUNTRY, true, country, 12);

    /* Bring interface up */
    st = bcm_ioctl(dev, WLC_UP, true, NULL, 0);
    if (st != HAL_OK) {
        hal_console_puts("[bcm_wifi] Failed to bring interface up\n");
        return st;
    }

    dev->up = true;
    hal_console_puts("[bcm_wifi] Interface UP\n");
    return HAL_OK;
}

/* ── WiFi HW Operations (for wifi_framework) ── */

static bcm_wifi_dev_t *g_bcm_dev;  /* Global for hw_ops callbacks */

static hal_status_t bcm_hw_tx_raw(const void *frame, uint32_t len)
{
    bcm_wifi_dev_t *dev = g_bcm_dev;
    if (!dev || !dev->up || len > BCM_MAX_FRAME - BDC_HEADER_LEN)
        return HAL_ERROR;

    uint8_t *tx = (uint8_t *)dev->tx_buf;

    /* Prepend BDC header */
    tx[0] = (BDC_VERSION << BDC_FLAG_VER_SHIFT);  /* Version + flags */
    tx[1] = 0;                                      /* Priority */
    tx[2] = 0;                                      /* Flags2 */
    tx[3] = 0;                                      /* Data offset (DWORDs) */

    memcpy(tx + BDC_HEADER_LEN, frame, len);

    uint32_t total = BDC_HEADER_LEN + len;
    total = (total + 3) & ~3u;  /* Align to 4 bytes */

    /* Write to F2 data pipe */
    for (uint32_t i = 0; i < total / 4; i++) {
        bcm_write32(dev, 0x8000 + i * 4, ((uint32_t *)tx)[i]);
    }
    hal_mmio_barrier();

    return HAL_OK;
}

static hal_status_t bcm_hw_rx_raw(void *buf, uint32_t *len)
{
    bcm_wifi_dev_t *dev = g_bcm_dev;
    if (!dev || !dev->up)
        return HAL_ERROR;

    /* Check for pending frame */
    uint32_t mailbox = bcm_bp_read32(dev, SDIO_TO_HOST_MAILBOX);
    if (!(mailbox & HMB_FRAME_IND))
        return HAL_NO_DEVICE;  /* No frame pending */

    /* Acknowledge */
    bcm_bp_write32(dev, SDIO_TO_HOST_MAILBOX, mailbox);

    /* Read frame header (first 4 bytes = SDIO header with length) */
    uint32_t hdr_word = bcm_read32(dev, 0x8000);
    uint32_t frame_len = hdr_word & 0xFFFF;
    if (frame_len < BDC_HEADER_LEN || frame_len > BCM_MAX_FRAME) {
        *len = 0;
        return HAL_ERROR;
    }

    /* Read the rest of the frame */
    uint8_t *rx = (uint8_t *)dev->rx_buf;
    uint32_t words = (frame_len + 3) / 4;
    ((uint32_t *)rx)[0] = hdr_word;
    for (uint32_t i = 1; i < words && i < BCM_MAX_FRAME / 4; i++) {
        ((uint32_t *)rx)[i] = bcm_read32(dev, 0x8000 + i * 4);
    }

    /* Strip BDC header */
    uint32_t bdc_offset = rx[3] * 4;  /* Data offset in DWORDs */
    uint32_t payload_off = BDC_HEADER_LEN + bdc_offset;
    if (payload_off >= frame_len) {
        *len = 0;
        return HAL_ERROR;
    }

    uint32_t payload_len = frame_len - payload_off;
    if (payload_len > BCM_MAX_FRAME)
        payload_len = BCM_MAX_FRAME;

    memcpy(buf, rx + payload_off, payload_len);
    *len = payload_len;

    return HAL_OK;
}

static hal_status_t bcm_hw_set_channel(uint8_t channel)
{
    bcm_wifi_dev_t *dev = g_bcm_dev;
    if (!dev) return HAL_ERROR;

    uint32_t ch = channel;
    return bcm_ioctl(dev, WLC_SET_CHANNEL, true, &ch, 4);
}

static void bcm_hw_get_mac(uint8_t mac[6])
{
    bcm_wifi_dev_t *dev = g_bcm_dev;
    if (dev) {
        for (int i = 0; i < 6; i++)
            mac[i] = dev->mac[i];
    }
}

static hal_status_t bcm_hw_set_promisc(bool enable)
{
    bcm_wifi_dev_t *dev = g_bcm_dev;
    if (!dev) return HAL_ERROR;

    uint32_t val = enable ? 1 : 0;
    return bcm_iovar(dev, "promisc", true, &val, 4);
}

static const wifi_hw_ops_t bcm_hw_ops = {
    .tx_raw      = bcm_hw_tx_raw,
    .rx_raw      = bcm_hw_rx_raw,
    .set_channel = bcm_hw_set_channel,
    .get_mac     = bcm_hw_get_mac,
    .set_promisc = bcm_hw_set_promisc,
};

/* ── Main Init ── */

static hal_status_t bcm_wifi_init(bcm_wifi_dev_t *dev, hal_device_t *hal_dev)
{
    hal_status_t st;

    dev->hal_dev = *hal_dev;
    dev->backplane_addr = 0xFFFFFFFF;
    dev->ioctl_id = 1;
    dev->fw_loaded = false;
    dev->up = false;
    dev->associated = false;

    /* For SDIO-attached chips (RPi), the SDIO controller is platform-specific.
     * Map the provided BAR or use the DT-provided MMIO base. */
    dev->sdio_regs = hal_bus_map_bar(hal_dev, 0);
    if (!dev->sdio_regs) {
        hal_console_puts("[bcm_wifi] Failed to map SDIO registers\n");
        return HAL_ERROR;
    }

    /* Allocate DMA buffers */
    dev->tx_buf = hal_dma_alloc(BCM_MAX_FRAME, &dev->tx_buf_phys);
    dev->rx_buf = hal_dma_alloc(BCM_MAX_FRAME, &dev->rx_buf_phys);
    dev->ctl_buf = hal_dma_alloc(2048, &dev->ctl_buf_phys);
    if (!dev->tx_buf || !dev->rx_buf || !dev->ctl_buf)
        return HAL_NO_MEMORY;

    /* Initialize SDIO transport */
    st = bcm_sdio_init(dev);
    if (st != HAL_OK)
        return st;

    /* Identify chip */
    st = bcm_identify(dev);
    if (st != HAL_OK)
        return st;

    /* Note: On real hardware, firmware must be loaded here.
     * The firmware binary (e.g., brcmfmac43455-sdio.bin) is typically
     * loaded from storage. For now, we assume firmware is pre-loaded
     * (e.g., by bootloader or stored in flash). */
    dev->fw_loaded = true;

    /* Set global pointer for hw_ops callbacks */
    g_bcm_dev = dev;

    /* Read MAC address from firmware */
    st = bcm_get_mac_from_fw(dev);
    if (st != HAL_OK) {
        /* Generate a random MAC if we can't read from firmware */
        dev->mac[0] = 0xB8; dev->mac[1] = 0x27; dev->mac[2] = 0xEB;
        dev->mac[3] = 0x12; dev->mac[4] = 0x34; dev->mac[5] = 0x56;
        hal_console_puts("[bcm_wifi] Using fallback MAC address\n");
    }

    /* Bring firmware interface up */
    st = bcm_fw_up(dev);
    if (st != HAL_OK)
        return st;

    /* Initialize WiFi framework with our hardware ops */
    st = wifi_init(&dev->wifi_ctx, &bcm_hw_ops);
    if (st != HAL_OK) {
        hal_console_puts("[bcm_wifi] WiFi framework init failed\n");
        return st;
    }

    hal_console_puts("[bcm_wifi] Driver initialized\n");
    return HAL_OK;
}

static void bcm_wifi_shutdown(bcm_wifi_dev_t *dev)
{
    if (dev->up) {
        bcm_ioctl(dev, WLC_DOWN, true, NULL, 0);
        dev->up = false;
    }

    if (dev->tx_buf) hal_dma_free(dev->tx_buf, BCM_MAX_FRAME);
    if (dev->rx_buf) hal_dma_free(dev->rx_buf, BCM_MAX_FRAME);
    if (dev->ctl_buf) hal_dma_free(dev->ctl_buf, 2048);

    dev->tx_buf = NULL;
    dev->rx_buf = NULL;
    dev->ctl_buf = NULL;

    hal_console_puts("[bcm_wifi] Shutdown\n");
}

/* ── driver_ops_t wrapper ── */

static bcm_wifi_dev_t g_bcm_nic;

static hal_status_t bcm_drv_init(hal_device_t *dev)
{
    return bcm_wifi_init(&g_bcm_nic, dev);
}

static void bcm_drv_shutdown(void)
{
    bcm_wifi_shutdown(&g_bcm_nic);
}

static int64_t bcm_drv_tx(const void *frame, uint64_t len)
{
    hal_status_t st = bcm_hw_tx_raw(frame, (uint32_t)len);
    return (st == HAL_OK) ? (int64_t)len : -1;
}

static int64_t bcm_drv_rx(void *frame, uint64_t max_len)
{
    uint32_t len = 0;
    hal_status_t st = bcm_hw_rx_raw(frame, &len);
    if (st != HAL_OK) return -1;
    if (len > max_len) len = (uint32_t)max_len;
    return (int64_t)len;
}

static void bcm_drv_get_mac(uint8_t mac[6])
{
    bcm_hw_get_mac(mac);
}

static const driver_ops_t bcm_wifi_driver_ops = {
    .name        = "bcm_wifi",
    .category    = DRIVER_CAT_NETWORK,
    .init        = bcm_drv_init,
    .shutdown    = bcm_drv_shutdown,
    .net_tx      = bcm_drv_tx,
    .net_rx      = bcm_drv_rx,
    .net_get_mac = bcm_drv_get_mac,
};

void bcm_wifi_register(void)
{
    driver_register_builtin(&bcm_wifi_driver_ops);
}
