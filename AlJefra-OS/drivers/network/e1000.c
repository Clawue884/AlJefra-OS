/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Intel e1000/e1000e NIC Driver Implementation
 * Architecture-independent; uses HAL for all hardware access.
 */

#include "e1000.h"
#include "../../lib/string.h"

/* ── Internal constants ── */
#define E1000_TIMEOUT_MS    3000
#define E1000_POLL_US       100
#define E1000_TX_BUF_SIZE   2048

/* ── Helpers ── */

static inline uint32_t e1000_read(e1000_dev_t *nic, uint32_t off)
{
    return hal_mmio_read32((volatile void *)((uint8_t *)nic->regs + off));
}

static inline void e1000_write(e1000_dev_t *nic, uint32_t off, uint32_t val)
{
    hal_mmio_write32((volatile void *)((uint8_t *)nic->regs + off), val);
}

/* ── EEPROM read ── */

static uint16_t e1000_eeprom_read(e1000_dev_t *nic, uint8_t addr)
{
    uint32_t val;

    /* Start EEPROM read */
    e1000_write(nic, E1000_EERD,
                E1000_EERD_START | ((uint32_t)addr << E1000_EERD_ADDR_SHIFT));

    /* Wait for completion */
    uint64_t deadline = hal_timer_ms() + E1000_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        val = e1000_read(nic, E1000_EERD);
        if (val & E1000_EERD_DONE)
            return (uint16_t)(val >> E1000_EERD_DATA_SHIFT);
        hal_timer_delay_us(E1000_POLL_US);
    }

    return 0;
}

/* ── MAC address detection ── */

static void e1000_read_mac(e1000_dev_t *nic)
{
    /* Try reading from EEPROM first */
    uint16_t w0 = e1000_eeprom_read(nic, 0);
    uint16_t w1 = e1000_eeprom_read(nic, 1);
    uint16_t w2 = e1000_eeprom_read(nic, 2);

    if (w0 != 0 || w1 != 0 || w2 != 0) {
        nic->mac[0] = (uint8_t)(w0 & 0xFF);
        nic->mac[1] = (uint8_t)(w0 >> 8);
        nic->mac[2] = (uint8_t)(w1 & 0xFF);
        nic->mac[3] = (uint8_t)(w1 >> 8);
        nic->mac[4] = (uint8_t)(w2 & 0xFF);
        nic->mac[5] = (uint8_t)(w2 >> 8);
    } else {
        /* Fallback: read from RAL/RAH registers */
        uint32_t ral = e1000_read(nic, E1000_RAL);
        uint32_t rah = e1000_read(nic, E1000_RAH);
        nic->mac[0] = (uint8_t)(ral & 0xFF);
        nic->mac[1] = (uint8_t)((ral >> 8) & 0xFF);
        nic->mac[2] = (uint8_t)((ral >> 16) & 0xFF);
        nic->mac[3] = (uint8_t)((ral >> 24) & 0xFF);
        nic->mac[4] = (uint8_t)(rah & 0xFF);
        nic->mac[5] = (uint8_t)((rah >> 8) & 0xFF);
    }
}

/* ── TX ring setup ── */

static hal_status_t e1000_setup_tx(e1000_dev_t *nic)
{
    uint64_t ring_size = (uint64_t)E1000_NUM_TX_DESC * sizeof(e1000_tx_desc_t);

    /* Allocate TX descriptor ring */
    nic->tx_ring = (e1000_tx_desc_t *)hal_dma_alloc(ring_size, &nic->tx_ring_phys);
    if (!nic->tx_ring)
        return HAL_NO_MEMORY;
    memset(nic->tx_ring, 0, ring_size);

    /* Allocate TX buffers */
    for (uint32_t i = 0; i < E1000_NUM_TX_DESC; i++) {
        nic->tx_bufs[i] = hal_dma_alloc(E1000_TX_BUF_SIZE, &nic->tx_bufs_phys[i]);
        if (!nic->tx_bufs[i])
            return HAL_NO_MEMORY;
        nic->tx_ring[i].addr = nic->tx_bufs_phys[i];
        nic->tx_ring[i].sta = E1000_TXD_STAT_DD; /* Mark as available */
    }

    /* Set TX descriptor base address */
    e1000_write(nic, E1000_TDBAL, (uint32_t)(nic->tx_ring_phys & 0xFFFFFFFF));
    e1000_write(nic, E1000_TDBAH, (uint32_t)(nic->tx_ring_phys >> 32));

    /* Set TX descriptor ring length (in bytes) */
    e1000_write(nic, E1000_TDLEN, (uint32_t)ring_size);

    /* Set head and tail */
    e1000_write(nic, E1000_TDH, 0);
    e1000_write(nic, E1000_TDT, 0);
    nic->tx_tail = 0;

    /* Configure Transmit Inter-Packet Gap */
    /* Standard values: IPGT=10, IPGR1=10, IPGR2=10 */
    e1000_write(nic, E1000_TIPG, (10u << 0) | (10u << 10) | (10u << 20));

    /* Enable transmitter */
    uint32_t tctl = E1000_TCTL_EN | E1000_TCTL_PSP |
                    (15u << E1000_TCTL_CT_SHIFT) |    /* Collision threshold */
                    (64u << E1000_TCTL_COLD_SHIFT);   /* Collision distance (FD) */
    e1000_write(nic, E1000_TCTL, tctl);

    return HAL_OK;
}

/* ── RX ring setup ── */

static hal_status_t e1000_setup_rx(e1000_dev_t *nic)
{
    uint64_t ring_size = (uint64_t)E1000_NUM_RX_DESC * sizeof(e1000_rx_desc_t);

    /* Allocate RX descriptor ring */
    nic->rx_ring = (e1000_rx_desc_t *)hal_dma_alloc(ring_size, &nic->rx_ring_phys);
    if (!nic->rx_ring)
        return HAL_NO_MEMORY;
    memset(nic->rx_ring, 0, ring_size);

    /* Allocate RX buffers */
    for (uint32_t i = 0; i < E1000_NUM_RX_DESC; i++) {
        nic->rx_bufs[i] = hal_dma_alloc(E1000_RX_BUF_SIZE, &nic->rx_bufs_phys[i]);
        if (!nic->rx_bufs[i])
            return HAL_NO_MEMORY;
        nic->rx_ring[i].addr = nic->rx_bufs_phys[i];
        nic->rx_ring[i].status = 0;
    }

    /* Set RX descriptor base address */
    e1000_write(nic, E1000_RDBAL, (uint32_t)(nic->rx_ring_phys & 0xFFFFFFFF));
    e1000_write(nic, E1000_RDBAH, (uint32_t)(nic->rx_ring_phys >> 32));

    /* Set RX descriptor ring length */
    e1000_write(nic, E1000_RDLEN, (uint32_t)ring_size);

    /* Set head and tail */
    e1000_write(nic, E1000_RDH, 0);
    e1000_write(nic, E1000_RDT, E1000_NUM_RX_DESC - 1);
    nic->rx_tail = 0;

    /* Set MAC address in RAL/RAH */
    uint32_t ral = (uint32_t)nic->mac[0] |
                   ((uint32_t)nic->mac[1] << 8) |
                   ((uint32_t)nic->mac[2] << 16) |
                   ((uint32_t)nic->mac[3] << 24);
    uint32_t rah = (uint32_t)nic->mac[4] |
                   ((uint32_t)nic->mac[5] << 8) |
                   (1u << 31);  /* Address Valid */
    e1000_write(nic, E1000_RAL, ral);
    e1000_write(nic, E1000_RAH, rah);

    /* Clear Multicast Table Array */
    for (uint32_t i = 0; i < 128; i++) {
        e1000_write(nic, E1000_MTA + i * 4, 0);
    }

    /* Enable receiver */
    uint32_t rctl = E1000_RCTL_EN |
                    E1000_RCTL_BAM |       /* Accept broadcast */
                    E1000_RCTL_BSIZE_2048 | /* 2KB buffers */
                    E1000_RCTL_SECRC;       /* Strip CRC */
    e1000_write(nic, E1000_RCTL, rctl);

    return HAL_OK;
}

/* ── Public API ── */

hal_status_t e1000_init(e1000_dev_t *nic, hal_device_t *dev)
{
    hal_status_t st;

    nic->dev = *dev;
    nic->initialized = false;

    /* Enable bus mastering + memory space */
    hal_bus_pci_enable(dev);

    /* Map BAR0 */
    nic->regs = hal_bus_map_bar(dev, 0);
    if (!nic->regs)
        return HAL_ERROR;

    /* Disable interrupts */
    e1000_write(nic, E1000_IMC, 0xFFFFFFFF);

    /* Reset device */
    uint32_t ctrl = e1000_read(nic, E1000_CTRL);
    ctrl |= E1000_CTRL_RST;
    e1000_write(nic, E1000_CTRL, ctrl);
    hal_timer_delay_ms(10);

    /* Wait for reset to complete */
    uint64_t deadline = hal_timer_ms() + E1000_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        ctrl = e1000_read(nic, E1000_CTRL);
        if (!(ctrl & E1000_CTRL_RST))
            break;
        hal_timer_delay_us(E1000_POLL_US);
    }

    /* Disable interrupts again after reset */
    e1000_write(nic, E1000_IMC, 0xFFFFFFFF);

    /* Clear pending interrupts */
    e1000_read(nic, E1000_ICR);

    /* Set link up */
    ctrl = e1000_read(nic, E1000_CTRL);
    ctrl |= E1000_CTRL_SLU | E1000_CTRL_ASDE;
    ctrl &= ~E1000_CTRL_ILOS;
    ctrl &= ~E1000_CTRL_PHY_RST;
    e1000_write(nic, E1000_CTRL, ctrl);

    /* Read MAC address */
    e1000_read_mac(nic);

    /* Set up TX */
    st = e1000_setup_tx(nic);
    if (st != HAL_OK)
        return st;

    /* Set up RX */
    st = e1000_setup_rx(nic);
    if (st != HAL_OK)
        return st;

    /* Wait a bit for link to come up */
    hal_timer_delay_ms(100);

    nic->initialized = true;
    return HAL_OK;
}

hal_status_t e1000_send(e1000_dev_t *nic, const void *frame, uint16_t length)
{
    if (!nic->initialized)
        return HAL_ERROR;
    if (length > E1000_TX_BUF_SIZE)
        return HAL_ERROR;

    uint16_t idx = nic->tx_tail;
    e1000_tx_desc_t *desc = &nic->tx_ring[idx];

    /* Wait for descriptor to be available (DD set) */
    uint64_t deadline = hal_timer_ms() + E1000_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        if (desc->sta & E1000_TXD_STAT_DD)
            break;
        hal_timer_delay_us(E1000_POLL_US);
    }
    if (!(desc->sta & E1000_TXD_STAT_DD))
        return HAL_TIMEOUT;

    /* Copy frame data to TX buffer */
    memcpy(nic->tx_bufs[idx], frame, length);

    /* Set up descriptor */
    desc->addr = nic->tx_bufs_phys[idx];
    desc->length = length;
    desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    desc->sta = 0;
    desc->cso = 0;
    desc->css = 0;
    desc->special = 0;

    /* Advance tail */
    nic->tx_tail = (idx + 1) % E1000_NUM_TX_DESC;

    /* Memory barrier before doorbell */
    hal_mmio_barrier();

    /* Ring the doorbell */
    e1000_write(nic, E1000_TDT, nic->tx_tail);

    return HAL_OK;
}

hal_status_t e1000_recv(e1000_dev_t *nic, void *buf, uint16_t *length)
{
    if (!nic->initialized)
        return HAL_ERROR;

    uint16_t idx = nic->rx_tail;
    e1000_rx_desc_t *desc = &nic->rx_ring[idx];

    /* Check if descriptor has a received packet */
    if (!(desc->status & E1000_RXD_STAT_DD))
        return HAL_NO_DEVICE;  /* No packet available */

    /* Check for end of packet */
    if (!(desc->status & E1000_RXD_STAT_EOP)) {
        /* Multi-descriptor packet — not supported, skip */
        desc->status = 0;
        uint16_t old_tail = nic->rx_tail;
        nic->rx_tail = (idx + 1) % E1000_NUM_RX_DESC;
        e1000_write(nic, E1000_RDT, old_tail);
        return HAL_ERROR;
    }

    /* Copy received data */
    uint16_t len = desc->length;
    if (len > E1000_RX_BUF_SIZE) len = E1000_RX_BUF_SIZE;
    memcpy(buf, nic->rx_bufs[idx], len);
    *length = len;

    /* Reset descriptor */
    desc->status = 0;

    /* Advance tail and update RDT */
    uint16_t old_tail = nic->rx_tail;
    nic->rx_tail = (idx + 1) % E1000_NUM_RX_DESC;
    e1000_write(nic, E1000_RDT, old_tail);

    return HAL_OK;
}

void e1000_get_mac(e1000_dev_t *nic, uint8_t mac[6])
{
    for (int i = 0; i < 6; i++)
        mac[i] = nic->mac[i];
}

bool e1000_link_up(e1000_dev_t *nic)
{
    if (!nic->initialized)
        return false;
    uint32_t status = e1000_read(nic, E1000_STATUS);
    return (status & (1u << 1)) != 0;  /* LU = Link Up */
}

/* ── driver_ops_t wrapper for built-in driver registration ── */
#include "../../kernel/driver_loader.h"

static e1000_dev_t g_e1000_nic;

static hal_status_t e1000_drv_init(hal_device_t *dev)
{
    return e1000_init(&g_e1000_nic, dev);
}

static int64_t e1000_drv_tx(const void *frame, uint64_t len)
{
    return e1000_send(&g_e1000_nic, frame, (uint16_t)len);
}

static int64_t e1000_drv_rx(void *frame, uint64_t max_len)
{
    uint16_t len = 0;
    hal_status_t rc = e1000_recv(&g_e1000_nic, frame, &len);
    if (rc != HAL_OK) return -1;
    return (int64_t)len;
}

static void e1000_drv_get_mac(uint8_t mac[6])
{
    e1000_get_mac(&g_e1000_nic, mac);
}

static const driver_ops_t e1000_driver_ops = {
    .name       = "e1000",
    .category   = DRIVER_CAT_NETWORK,
    .init       = e1000_drv_init,
    .shutdown   = NULL,
    .net_tx     = e1000_drv_tx,
    .net_rx     = e1000_drv_rx,
    .net_get_mac = e1000_drv_get_mac,
};

void e1000_register(void)
{
    driver_register_builtin(&e1000_driver_ops);
}
