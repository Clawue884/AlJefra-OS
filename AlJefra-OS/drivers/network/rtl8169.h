/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Realtek RTL8169/RTL8168/RTL8111 Gigabit Ethernet Driver
 * Architecture-independent; uses HAL for all hardware access.
 *
 * Supported chips:
 *   RTL8101E  (10/100, device 0x8136)
 *   RTL8168   (GbE,    device 0x8161, 0x8168)
 *   RTL8169   (GbE,    device 0x8169)
 */

#ifndef ALJEFRA_DRV_RTL8169_H
#define ALJEFRA_DRV_RTL8169_H

#include "../../hal/hal.h"

/* ── Realtek PCI IDs ── */
#define RTL_VENDOR_ID       0x10EC
#define RTL_DEV_8136        0x8136  /* RTL8101E */
#define RTL_DEV_8161        0x8161  /* RTL8168 variant */
#define RTL_DEV_8168        0x8168  /* RTL8168 */
#define RTL_DEV_8169        0x8169  /* RTL8169 */

/* ── RTL8169 Register Offsets ── */
#define RTL_IDR0            0x00    /* MAC address byte 0 */
#define RTL_IDR1            0x01    /* MAC address byte 1 */
#define RTL_IDR2            0x02    /* MAC address byte 2 */
#define RTL_IDR3            0x03    /* MAC address byte 3 */
#define RTL_IDR4            0x04    /* MAC address byte 4 */
#define RTL_IDR5            0x05    /* MAC address byte 5 */
#define RTL_MAR0            0x08    /* Multicast filter 0-3 */
#define RTL_MAR4            0x0C    /* Multicast filter 4-7 */
#define RTL_DTCCR           0x10    /* Dump Tally Counter Command Register */
#define RTL_TNPDS           0x20    /* TX Normal Priority Descriptors (low 32) */
#define RTL_TNPDS_HIGH      0x24    /* TX Normal Priority Descriptors (high 32) */
#define RTL_THPDS           0x28    /* TX High Priority Descriptors (low 32) */
#define RTL_THPDS_HIGH      0x2C    /* TX High Priority Descriptors (high 32) */
#define RTL_FLASH           0x30    /* Flash Memory Read/Write */
#define RTL_ERBCR           0x34    /* Early RX Byte Count */
#define RTL_ERSR            0x36    /* Early RX Status */
#define RTL_CR              0x37    /* Command Register (ChipCmd) */
#define RTL_TPPOLL          0x38    /* TX Priority Polling (TxPoll) */
#define RTL_IMR             0x3C    /* Interrupt Mask Register */
#define RTL_ISR             0x3E    /* Interrupt Status Register */
#define RTL_TCR             0x40    /* TX Configuration Register */
#define RTL_RCR             0x44    /* RX Configuration Register */
#define RTL_TCTR            0x48    /* Timer Count Register */
#define RTL_MPC             0x4C    /* Missed Packet Counter */
#define RTL_9346CR          0x50    /* 93C46 Command Register */
#define RTL_CONFIG0         0x51    /* Configuration Register 0 */
#define RTL_CONFIG1         0x52    /* Configuration Register 1 */
#define RTL_CONFIG2         0x53    /* Configuration Register 2 */
#define RTL_CONFIG3         0x54    /* Configuration Register 3 */
#define RTL_CONFIG4         0x55    /* Configuration Register 4 */
#define RTL_CONFIG5         0x56    /* Configuration Register 5 */
#define RTL_TIMERINT        0x58    /* Timer Interrupt Register */
#define RTL_PHYAR           0x60    /* PHY Access Register */
#define RTL_PHY_STATUS      0x6C    /* PHY Status */
#define RTL_RMS             0xDA    /* RX Max Size */
#define RTL_CPCR            0xE0    /* C+ Command Register */
#define RTL_RDSAR           0xE4    /* RX Descriptor Start Address (low 32) */
#define RTL_RDSAR_HIGH      0xE8    /* RX Descriptor Start Address (high 32) */
#define RTL_MTPS            0xEC    /* Max TX Packet Size */

/* ── Command Register (CR) bits ── */
#define RTL_CR_RST          (1u << 4)  /* Reset */
#define RTL_CR_RE           (1u << 3)  /* Receiver Enable */
#define RTL_CR_TE           (1u << 2)  /* Transmitter Enable */

/* ── TX Priority Polling (TPPOLL) bits ── */
#define RTL_TPPOLL_HPQ      (1u << 7)  /* High Priority Queue polling */
#define RTL_TPPOLL_NPQ      (1u << 6)  /* Normal Priority Queue polling */

/* ── Interrupt Mask/Status Register bits ── */
#define RTL_INT_ROK         (1u << 0)   /* RX OK */
#define RTL_INT_RER         (1u << 1)   /* RX Error */
#define RTL_INT_TOK         (1u << 2)   /* TX OK */
#define RTL_INT_TER         (1u << 3)   /* TX Error */
#define RTL_INT_RDU         (1u << 4)   /* RX Descriptor Unavailable */
#define RTL_INT_LINKCHG     (1u << 5)   /* Link Change */
#define RTL_INT_FOVW        (1u << 6)   /* RX FIFO Overflow */
#define RTL_INT_TDU         (1u << 7)   /* TX Descriptor Unavailable */
#define RTL_INT_SWINT       (1u << 8)   /* Software Interrupt */
#define RTL_INT_TIMEOUT     (1u << 14)  /* Timer Timeout */
#define RTL_INT_SERR        (1u << 15)  /* System Error */

/* ── TX Configuration Register (TCR) bits ── */
#define RTL_TCR_IFG_STD     (3u << 24)  /* InterFrameGap standard (96-bit time) */
#define RTL_TCR_MXDMA_2048  (7u << 8)   /* Max DMA burst: unlimited (2048 bytes) */

/* ── RX Configuration Register (RCR) bits ── */
#define RTL_RCR_AAP         (1u << 0)   /* Accept All Packets (promiscuous) */
#define RTL_RCR_APM         (1u << 1)   /* Accept Physical Match (unicast) */
#define RTL_RCR_AM          (1u << 2)   /* Accept Multicast */
#define RTL_RCR_AB          (1u << 3)   /* Accept Broadcast */
#define RTL_RCR_AR          (1u << 4)   /* Accept Runt */
#define RTL_RCR_AER         (1u << 5)   /* Accept Error Packets */
#define RTL_RCR_MXDMA_1024  (6u << 8)   /* Max DMA burst: 1024 bytes */
#define RTL_RCR_MXDMA_UNLIM (7u << 8)   /* Max DMA burst: unlimited */
#define RTL_RCR_RXFTH_NONE  (7u << 13)  /* No RX FIFO threshold (store & forward) */

/* ── C+ Command Register (CPCR) bits ── */
#define RTL_CPCR_RXVLAN     (1u << 6)   /* RX VLAN de-tagging */
#define RTL_CPCR_RXCHKSUM   (1u << 5)   /* RX checksum offload */

/* ── 93C46 Command Register ── */
#define RTL_9346CR_EEM_NORMAL   0x00    /* Normal operating mode */
#define RTL_9346CR_EEM_CONFIG   0xC0    /* Config register write enable */

/* ── PHY Status Register (0x6C) bits ── */
#define RTL_PHY_LINKSTS     (1u << 1)   /* Link status */
#define RTL_PHY_1000MF      (1u << 4)   /* 1000 Mbps full duplex */
#define RTL_PHY_100M        (1u << 3)   /* 100 Mbps */
#define RTL_PHY_10M         (1u << 2)   /* 10 Mbps */

/* ── TX Descriptor flags (first uint32_t of opts1) ── */
#define RTL_TXD_OWN         (1u << 31)  /* Owned by NIC */
#define RTL_TXD_EOR         (1u << 30)  /* End of Ring */
#define RTL_TXD_FS          (1u << 29)  /* First Segment */
#define RTL_TXD_LS          (1u << 28)  /* Last Segment */

/* ── RX Descriptor flags (first uint32_t of opts1) ── */
#define RTL_RXD_OWN         (1u << 31)  /* Owned by NIC */
#define RTL_RXD_EOR         (1u << 30)  /* End of Ring */
#define RTL_RXD_FS          (1u << 29)  /* First Segment */
#define RTL_RXD_LS          (1u << 28)  /* Last Segment */
#define RTL_RXD_RES         (1u << 21)  /* RX Error Summary */
#define RTL_RXD_LEN_MASK    0x00003FFFu /* Frame length mask (bits 13:0) */

/* ── Ring sizes ── */
#define RTL_NUM_TX_DESC     256
#define RTL_NUM_RX_DESC     256
#define RTL_TX_BUF_SIZE     2048
#define RTL_RX_BUF_SIZE     2048

/* ── TX Descriptor (C+ mode, 16 bytes) ── */
typedef struct __attribute__((packed, aligned(8))) {
    uint32_t opts1;         /* OWN | EOR | FS | LS | length */
    uint32_t opts2;         /* VLAN tag, offload flags */
    uint64_t addr;          /* Buffer physical address */
} rtl8169_tx_desc_t;

/* ── RX Descriptor (C+ mode, 16 bytes) ── */
typedef struct __attribute__((packed, aligned(8))) {
    uint32_t opts1;         /* OWN | EOR | FS | LS | length */
    uint32_t opts2;         /* VLAN tag, offload flags */
    uint64_t addr;          /* Buffer physical address */
} rtl8169_rx_desc_t;

/* ── NIC state ── */
typedef struct {
    hal_device_t       dev;
    volatile void     *regs;          /* BAR0 MMIO base */

    /* TX ring */
    rtl8169_tx_desc_t *tx_ring;       /* Descriptor ring (DMA) */
    uint64_t           tx_ring_phys;
    void              *tx_bufs[RTL_NUM_TX_DESC];
    uint64_t           tx_bufs_phys[RTL_NUM_TX_DESC];
    uint16_t           tx_tail;

    /* RX ring */
    rtl8169_rx_desc_t *rx_ring;       /* Descriptor ring (DMA) */
    uint64_t           rx_ring_phys;
    void              *rx_bufs[RTL_NUM_RX_DESC];
    uint64_t           rx_bufs_phys[RTL_NUM_RX_DESC];
    uint16_t           rx_tail;

    /* MAC address */
    uint8_t            mac[6];

    bool               initialized;
} rtl8169_dev_t;

/* ── Public API ── */

/* Initialize the RTL8169 NIC from a HAL device */
hal_status_t rtl8169_init(rtl8169_dev_t *nic, hal_device_t *dev);

/* Send a raw Ethernet frame. Returns HAL_OK on success. */
hal_status_t rtl8169_send(rtl8169_dev_t *nic, const void *frame, uint16_t length);

/* Receive a raw Ethernet frame (polling).
 * buf must be at least RTL_RX_BUF_SIZE bytes.
 * *length is set to actual received length.
 * Returns HAL_OK if a frame was received, HAL_NO_DEVICE if no frame pending. */
hal_status_t rtl8169_recv(rtl8169_dev_t *nic, void *buf, uint16_t *length);

/* Get MAC address */
void rtl8169_get_mac(rtl8169_dev_t *nic, uint8_t mac[6]);

/* Check link status (true = link up) */
bool rtl8169_link_up(rtl8169_dev_t *nic);

/* Register the driver with the kernel driver loader */
void rtl8169_register(void);

#endif /* ALJEFRA_DRV_RTL8169_H */
