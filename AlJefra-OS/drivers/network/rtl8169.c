/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Realtek RTL8169/RTL8168/RTL8111 Gigabit Ethernet Driver
 * Architecture-independent; uses HAL for all hardware access.
 *
 * This driver operates in C+ mode (descriptor-based DMA), which is the
 * modern mode supported by all chips in the RTL8169 family. The legacy
 * ring-buffer mode (RTL8139-style) is NOT used.
 *
 * References:
 *   - RTL8169/RTL8168/RTL8111 Programmer's Guide
 *   - Linux r8169 driver (drivers/net/ethernet/realtek/r8169_main.c)
 */

#include "rtl8169.h"
#include "../../lib/string.h"

/* ── Internal constants ── */
#define RTL_TIMEOUT_MS      3000
#define RTL_RESET_TIMEOUT   100   /* ms to wait for soft reset */
#define RTL_POLL_US         100

/* ── MMIO read/write wrappers ── */

static inline uint8_t rtl_read8(rtl8169_dev_t *nic, uint32_t off)
{
    return hal_mmio_read8((volatile void *)((uint8_t *)nic->regs + off));
}

static inline uint16_t rtl_read16(rtl8169_dev_t *nic, uint32_t off)
{
    return hal_mmio_read16((volatile void *)((uint8_t *)nic->regs + off));
}

static inline uint32_t rtl_read32(rtl8169_dev_t *nic, uint32_t off)
{
    return hal_mmio_read32((volatile void *)((uint8_t *)nic->regs + off));
}

static inline void rtl_write8(rtl8169_dev_t *nic, uint32_t off, uint8_t val)
{
    hal_mmio_write8((volatile void *)((uint8_t *)nic->regs + off), val);
}

static inline void rtl_write16(rtl8169_dev_t *nic, uint32_t off, uint16_t val)
{
    hal_mmio_write16((volatile void *)((uint8_t *)nic->regs + off), val);
}

static inline void rtl_write32(rtl8169_dev_t *nic, uint32_t off, uint32_t val)
{
    hal_mmio_write32((volatile void *)((uint8_t *)nic->regs + off), val);
}

/* ── Unlock/lock config registers ── */

static void rtl_unlock_config(rtl8169_dev_t *nic)
{
    rtl_write8(nic, RTL_9346CR, RTL_9346CR_EEM_CONFIG);
}

static void rtl_lock_config(rtl8169_dev_t *nic)
{
    rtl_write8(nic, RTL_9346CR, RTL_9346CR_EEM_NORMAL);
}

/* ── Soft reset ── */

static hal_status_t rtl_soft_reset(rtl8169_dev_t *nic)
{
    /* Issue reset via Command Register */
    rtl_write8(nic, RTL_CR, RTL_CR_RST);

    /* Wait for reset bit to self-clear */
    uint64_t deadline = hal_timer_ms() + RTL_RESET_TIMEOUT;
    while (hal_timer_ms() < deadline) {
        if (!(rtl_read8(nic, RTL_CR) & RTL_CR_RST))
            return HAL_OK;
        hal_timer_delay_us(RTL_POLL_US);
    }

    return HAL_TIMEOUT;
}

/* ── Read MAC address from IDR0-IDR5 ── */

static void rtl_read_mac(rtl8169_dev_t *nic)
{
    nic->mac[0] = rtl_read8(nic, RTL_IDR0);
    nic->mac[1] = rtl_read8(nic, RTL_IDR1);
    nic->mac[2] = rtl_read8(nic, RTL_IDR2);
    nic->mac[3] = rtl_read8(nic, RTL_IDR3);
    nic->mac[4] = rtl_read8(nic, RTL_IDR4);
    nic->mac[5] = rtl_read8(nic, RTL_IDR5);
}

/* ── TX ring setup ── */

static hal_status_t rtl_setup_tx(rtl8169_dev_t *nic)
{
    uint64_t ring_size = (uint64_t)RTL_NUM_TX_DESC * sizeof(rtl8169_tx_desc_t);

    /* Allocate TX descriptor ring (must be 256-byte aligned for RTL8169) */
    nic->tx_ring = (rtl8169_tx_desc_t *)hal_dma_alloc(ring_size, &nic->tx_ring_phys);
    if (!nic->tx_ring)
        return HAL_NO_MEMORY;
    memset(nic->tx_ring, 0, ring_size);

    /* Allocate TX buffers and initialize descriptors */
    for (uint32_t i = 0; i < RTL_NUM_TX_DESC; i++) {
        nic->tx_bufs[i] = hal_dma_alloc(RTL_TX_BUF_SIZE, &nic->tx_bufs_phys[i]);
        if (!nic->tx_bufs[i])
            return HAL_NO_MEMORY;

        nic->tx_ring[i].addr = nic->tx_bufs_phys[i];
        nic->tx_ring[i].opts1 = 0; /* Not owned by NIC, available to host */
        nic->tx_ring[i].opts2 = 0;
    }

    /* Mark the last descriptor with End-Of-Ring */
    nic->tx_ring[RTL_NUM_TX_DESC - 1].opts1 |= RTL_TXD_EOR;

    nic->tx_tail = 0;

    /* Tell the NIC where the TX descriptor ring lives */
    rtl_write32(nic, RTL_TNPDS,      (uint32_t)(nic->tx_ring_phys & 0xFFFFFFFF));
    rtl_write32(nic, RTL_TNPDS_HIGH, (uint32_t)(nic->tx_ring_phys >> 32));

    return HAL_OK;
}

/* ── RX ring setup ── */

static hal_status_t rtl_setup_rx(rtl8169_dev_t *nic)
{
    uint64_t ring_size = (uint64_t)RTL_NUM_RX_DESC * sizeof(rtl8169_rx_desc_t);

    /* Allocate RX descriptor ring */
    nic->rx_ring = (rtl8169_rx_desc_t *)hal_dma_alloc(ring_size, &nic->rx_ring_phys);
    if (!nic->rx_ring)
        return HAL_NO_MEMORY;
    memset(nic->rx_ring, 0, ring_size);

    /* Allocate RX buffers and initialize descriptors */
    for (uint32_t i = 0; i < RTL_NUM_RX_DESC; i++) {
        nic->rx_bufs[i] = hal_dma_alloc(RTL_RX_BUF_SIZE, &nic->rx_bufs_phys[i]);
        if (!nic->rx_bufs[i])
            return HAL_NO_MEMORY;

        nic->rx_ring[i].addr = nic->rx_bufs_phys[i];
        /* Hand ownership to NIC, set buffer size */
        nic->rx_ring[i].opts1 = RTL_RXD_OWN | RTL_RX_BUF_SIZE;
        nic->rx_ring[i].opts2 = 0;
    }

    /* Mark the last descriptor with End-Of-Ring */
    nic->rx_ring[RTL_NUM_RX_DESC - 1].opts1 |= RTL_RXD_EOR;

    nic->rx_tail = 0;

    /* Tell the NIC where the RX descriptor ring lives */
    rtl_write32(nic, RTL_RDSAR,      (uint32_t)(nic->rx_ring_phys & 0xFFFFFFFF));
    rtl_write32(nic, RTL_RDSAR_HIGH, (uint32_t)(nic->rx_ring_phys >> 32));

    return HAL_OK;
}

/* ── Public API ── */

hal_status_t rtl8169_init(rtl8169_dev_t *nic, hal_device_t *dev)
{
    hal_status_t st;

    nic->dev = *dev;
    nic->initialized = false;

    /* Enable bus mastering + memory space */
    hal_bus_pci_enable(dev);

    /* Map BAR0 (MMIO registers) */
    nic->regs = hal_bus_map_bar(dev, 0);
    if (!nic->regs)
        return HAL_ERROR;

    /* Soft reset the NIC */
    st = rtl_soft_reset(nic);
    if (st != HAL_OK)
        return st;

    /* Unlock config registers for write access */
    rtl_unlock_config(nic);

    /* Read MAC address from IDR registers (survives reset) */
    rtl_read_mac(nic);

    /* Disable all interrupts — we use polling mode */
    rtl_write16(nic, RTL_IMR, 0x0000);

    /* Clear any pending interrupt status */
    rtl_write16(nic, RTL_ISR, 0xFFFF);

    /* Set max RX packet size */
    rtl_write16(nic, RTL_RMS, RTL_RX_BUF_SIZE);

    /* Set max TX packet size (in units of 128 bytes) */
    rtl_write8(nic, RTL_MTPS, 0x3B); /* ~7.5 KB, sufficient for standard MTU */

    /* Set up TX descriptor ring */
    st = rtl_setup_tx(nic);
    if (st != HAL_OK)
        return st;

    /* Set up RX descriptor ring */
    st = rtl_setup_rx(nic);
    if (st != HAL_OK)
        return st;

    /* Configure TX:
     *   - Standard inter-frame gap (96-bit time)
     *   - Max DMA burst: unlimited (2048)
     */
    rtl_write32(nic, RTL_TCR, RTL_TCR_IFG_STD | RTL_TCR_MXDMA_2048);

    /* Configure RX:
     *   - Accept broadcast, multicast, and physical match (unicast)
     *   - Max DMA burst: 1024 bytes
     *   - No RX FIFO threshold (store & forward)
     */
    rtl_write32(nic, RTL_RCR,
                RTL_RCR_APM | RTL_RCR_AM | RTL_RCR_AB |
                RTL_RCR_MXDMA_1024 | RTL_RCR_RXFTH_NONE);

    /* Accept all multicast packets (set multicast hash to all 1s) */
    rtl_write32(nic, RTL_MAR0, 0xFFFFFFFF);
    rtl_write32(nic, RTL_MAR4, 0xFFFFFFFF);

    /* Enable C+ mode features (RX checksum offload) */
    rtl_write16(nic, RTL_CPCR, rtl_read16(nic, RTL_CPCR) | RTL_CPCR_RXCHKSUM);

    /* Lock config registers */
    rtl_lock_config(nic);

    /* Memory barrier before enabling DMA engines */
    hal_mmio_barrier();

    /* Enable TX and RX in the command register */
    rtl_write8(nic, RTL_CR, RTL_CR_TE | RTL_CR_RE);

    /* Wait for link to come up */
    hal_timer_delay_ms(100);

    nic->initialized = true;
    return HAL_OK;
}

hal_status_t rtl8169_send(rtl8169_dev_t *nic, const void *frame, uint16_t length)
{
    if (!nic->initialized)
        return HAL_ERROR;
    if (length == 0 || length > RTL_TX_BUF_SIZE)
        return HAL_ERROR;

    uint16_t idx = nic->tx_tail;
    rtl8169_tx_desc_t *desc = &nic->tx_ring[idx];

    /* Wait for descriptor to be available (OWN bit cleared by NIC) */
    uint64_t deadline = hal_timer_ms() + RTL_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        if (!(desc->opts1 & RTL_TXD_OWN))
            break;
        hal_timer_delay_us(RTL_POLL_US);
    }
    if (desc->opts1 & RTL_TXD_OWN)
        return HAL_TIMEOUT;

    /* Copy frame data to TX buffer */
    memcpy(nic->tx_bufs[idx], frame, length);

    /* Set up descriptor:
     *   - OWN: hand to NIC
     *   - FS + LS: single-descriptor packet
     *   - length in bits 15:0
     *   - Preserve EOR bit if this is the last descriptor in the ring
     */
    uint32_t eor = (idx == RTL_NUM_TX_DESC - 1) ? RTL_TXD_EOR : 0;
    desc->addr = nic->tx_bufs_phys[idx];
    desc->opts2 = 0;

    /* Memory barrier: ensure buffer data is visible before setting OWN */
    hal_mmio_barrier();

    desc->opts1 = RTL_TXD_OWN | RTL_TXD_FS | RTL_TXD_LS | eor | (uint32_t)length;

    /* Advance tail index */
    nic->tx_tail = (idx + 1) % RTL_NUM_TX_DESC;

    /* Memory barrier before doorbell */
    hal_mmio_barrier();

    /* Trigger TX DMA by writing to TxPoll register */
    rtl_write8(nic, RTL_TPPOLL, RTL_TPPOLL_NPQ);

    return HAL_OK;
}

hal_status_t rtl8169_recv(rtl8169_dev_t *nic, void *buf, uint16_t *length)
{
    if (!nic->initialized)
        return HAL_ERROR;

    uint16_t idx = nic->rx_tail;
    rtl8169_rx_desc_t *desc = &nic->rx_ring[idx];
    uint32_t opts1 = desc->opts1;
    hal_status_t result = HAL_OK;
    uint32_t eor;

    /* Check if descriptor is owned by the host (OWN bit cleared = received) */
    if (opts1 & RTL_RXD_OWN)
        return HAL_NO_DEVICE;  /* No packet available */

    /* Check for errors */
    if (opts1 & RTL_RXD_RES) {
        result = HAL_ERROR;
    }
    /* Check for complete single-segment packet (FS + LS both set) */
    else if (!((opts1 & RTL_RXD_FS) && (opts1 & RTL_RXD_LS))) {
        /* Multi-descriptor packet -- not supported, skip */
        result = HAL_ERROR;
    } else {
        /* Extract frame length (bits 13:0 of opts1, includes CRC) */
        uint16_t len = (uint16_t)(opts1 & RTL_RXD_LEN_MASK);

        /* Sanity check length */
        if (len > RTL_RX_BUF_SIZE)
            len = RTL_RX_BUF_SIZE;

        if (len < 18) {
            /* Too small: minimum Ethernet (14) + CRC (4) = 18 */
            result = HAL_ERROR;
        } else {
            /* Subtract the 4-byte CRC. RTL8169 in C+ mode includes CRC
             * in the reported length. */
            len -= 4;

            /* Copy received data */
            memcpy(buf, nic->rx_bufs[idx], len);
            *length = len;
        }
    }

    /* Recycle the descriptor -- hand it back to the NIC */
    eor = (idx == RTL_NUM_RX_DESC - 1) ? RTL_RXD_EOR : 0;
    desc->opts2 = 0;

    /* Memory barrier before handing back to NIC */
    hal_mmio_barrier();

    desc->opts1 = RTL_RXD_OWN | eor | RTL_RX_BUF_SIZE;

    /* Advance tail */
    nic->rx_tail = (idx + 1) % RTL_NUM_RX_DESC;

    return result;
}

void rtl8169_get_mac(rtl8169_dev_t *nic, uint8_t mac[6])
{
    for (int i = 0; i < 6; i++)
        mac[i] = nic->mac[i];
}

bool rtl8169_link_up(rtl8169_dev_t *nic)
{
    if (!nic->initialized)
        return false;
    uint8_t phy_status = rtl_read8(nic, RTL_PHY_STATUS);
    return (phy_status & RTL_PHY_LINKSTS) != 0;
}

/* ── driver_ops_t wrapper for built-in driver registration ── */
#include "../../kernel/driver_loader.h"

static rtl8169_dev_t g_rtl8169_nic;

static hal_status_t rtl8169_drv_init(hal_device_t *dev)
{
    return rtl8169_init(&g_rtl8169_nic, dev);
}

static int64_t rtl8169_drv_tx(const void *frame, uint64_t len)
{
    return rtl8169_send(&g_rtl8169_nic, frame, (uint16_t)len);
}

static int64_t rtl8169_drv_rx(void *frame, uint64_t max_len)
{
    uint16_t len = 0;
    hal_status_t rc = rtl8169_recv(&g_rtl8169_nic, frame, &len);
    if (rc != HAL_OK) return -1;
    return (int64_t)len;
}

static void rtl8169_drv_get_mac(uint8_t mac[6])
{
    rtl8169_get_mac(&g_rtl8169_nic, mac);
}

static const driver_ops_t rtl8169_driver_ops = {
    .name       = "rtl8169",
    .category   = DRIVER_CAT_NETWORK,
    .init       = rtl8169_drv_init,
    .shutdown   = NULL,
    .net_tx     = rtl8169_drv_tx,
    .net_rx     = rtl8169_drv_rx,
    .net_get_mac = rtl8169_drv_get_mac,
};

void rtl8169_register(void)
{
    driver_register_builtin(&rtl8169_driver_ops);
}
