/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Intel e1000/e1000e NIC Driver
 * Architecture-independent; uses HAL for all hardware access.
 */

#ifndef ALJEFRA_DRV_E1000_H
#define ALJEFRA_DRV_E1000_H

#include "../../hal/hal.h"

/* ── e1000 Register offsets ── */
#define E1000_CTRL      0x0000  /* Device Control */
#define E1000_STATUS    0x0008  /* Device Status */
#define E1000_EECD      0x0010  /* EEPROM/Flash Control */
#define E1000_EERD      0x0014  /* EEPROM Read */
#define E1000_ICR       0x00C0  /* Interrupt Cause Read */
#define E1000_ICS       0x00C8  /* Interrupt Cause Set */
#define E1000_IMS       0x00D0  /* Interrupt Mask Set */
#define E1000_IMC       0x00D8  /* Interrupt Mask Clear */
#define E1000_RCTL      0x0100  /* Receive Control */
#define E1000_RDBAL     0x2800  /* RX Descriptor Base Low */
#define E1000_RDBAH     0x2804  /* RX Descriptor Base High */
#define E1000_RDLEN     0x2808  /* RX Descriptor Length */
#define E1000_RDH       0x2810  /* RX Descriptor Head */
#define E1000_RDT       0x2818  /* RX Descriptor Tail */
#define E1000_TCTL      0x0400  /* Transmit Control */
#define E1000_TIPG      0x0410  /* Transmit Inter-Packet Gap */
#define E1000_TDBAL     0x3800  /* TX Descriptor Base Low */
#define E1000_TDBAH     0x3804  /* TX Descriptor Base High */
#define E1000_TDLEN     0x3808  /* TX Descriptor Length */
#define E1000_TDH       0x3810  /* TX Descriptor Head */
#define E1000_TDT       0x3818  /* TX Descriptor Tail */
#define E1000_RAL       0x5400  /* Receive Address Low (MAC bits 31:0) */
#define E1000_RAH       0x5404  /* Receive Address High (MAC bits 47:32 + flags) */
#define E1000_MTA       0x5200  /* Multicast Table Array (128 entries) */

/* ── CTRL bits ── */
#define E1000_CTRL_FD       (1u << 0)   /* Full Duplex */
#define E1000_CTRL_ASDE     (1u << 5)   /* Auto-Speed Detection Enable */
#define E1000_CTRL_SLU      (1u << 6)   /* Set Link Up */
#define E1000_CTRL_ILOS     (1u << 7)   /* Invert Loss-of-Signal */
#define E1000_CTRL_RST      (1u << 26)  /* Device Reset */
#define E1000_CTRL_VME      (1u << 30)  /* VLAN Mode Enable */
#define E1000_CTRL_PHY_RST  (1u << 31)  /* PHY Reset */

/* ── RCTL bits ── */
#define E1000_RCTL_EN       (1u << 1)   /* Receiver Enable */
#define E1000_RCTL_SBP      (1u << 2)   /* Store Bad Packets */
#define E1000_RCTL_UPE      (1u << 3)   /* Unicast Promiscuous Enable */
#define E1000_RCTL_MPE      (1u << 4)   /* Multicast Promiscuous Enable */
#define E1000_RCTL_LPE      (1u << 5)   /* Long Packet Enable */
#define E1000_RCTL_BAM      (1u << 15)  /* Broadcast Accept Mode */
#define E1000_RCTL_BSIZE_256  (3u << 16) /* Buffer Size 256 */
#define E1000_RCTL_BSIZE_512  (2u << 16) /* Buffer Size 512 */
#define E1000_RCTL_BSIZE_1024 (1u << 16) /* Buffer Size 1024 */
#define E1000_RCTL_BSIZE_2048 (0u << 16) /* Buffer Size 2048 (default) */
#define E1000_RCTL_BSIZE_4096 (3u << 16 | 1u << 25) /* BSEX + 4096 */
#define E1000_RCTL_BSIZE_8192 (2u << 16 | 1u << 25) /* BSEX + 8192 */
#define E1000_RCTL_SECRC    (1u << 26)  /* Strip Ethernet CRC */

/* ── TCTL bits ── */
#define E1000_TCTL_EN       (1u << 1)   /* Transmit Enable */
#define E1000_TCTL_PSP      (1u << 3)   /* Pad Short Packets */
#define E1000_TCTL_CT_SHIFT 4           /* Collision Threshold */
#define E1000_TCTL_COLD_SHIFT 12        /* Collision Distance */

/* ── TX Descriptor status/command bits ── */
#define E1000_TXD_CMD_EOP   (1u << 0)   /* End of Packet */
#define E1000_TXD_CMD_IFCS  (1u << 1)   /* Insert FCS */
#define E1000_TXD_CMD_RS    (1u << 3)   /* Report Status */
#define E1000_TXD_STAT_DD   (1u << 0)   /* Descriptor Done */

/* ── RX Descriptor status bits ── */
#define E1000_RXD_STAT_DD   (1u << 0)   /* Descriptor Done */
#define E1000_RXD_STAT_EOP  (1u << 1)   /* End of Packet */

/* ── EEPROM read bits ── */
#define E1000_EERD_START    (1u << 0)
#define E1000_EERD_DONE     (1u << 4)
#define E1000_EERD_ADDR_SHIFT 8
#define E1000_EERD_DATA_SHIFT 16

/* ── Ring sizes ── */
#define E1000_NUM_TX_DESC   32
#define E1000_NUM_RX_DESC   32
#define E1000_RX_BUF_SIZE   2048

/* ── TX Descriptor (legacy, 16 bytes) ── */
typedef struct __attribute__((packed)) {
    uint64_t addr;       /* Buffer physical address */
    uint16_t length;     /* Data length */
    uint8_t  cso;        /* Checksum offset */
    uint8_t  cmd;        /* Command field */
    uint8_t  sta;        /* Status (upper nibble = reserved) */
    uint8_t  css;        /* Checksum start */
    uint16_t special;    /* Special / VLAN */
} e1000_tx_desc_t;

/* ── RX Descriptor (legacy, 16 bytes) ── */
typedef struct __attribute__((packed)) {
    uint64_t addr;       /* Buffer physical address */
    uint16_t length;     /* Received length */
    uint16_t checksum;   /* Packet checksum */
    uint8_t  status;     /* Status */
    uint8_t  errors;     /* Errors */
    uint16_t special;    /* Special / VLAN */
} e1000_rx_desc_t;

/* ── NIC state ── */
typedef struct {
    hal_device_t      dev;
    volatile void     *regs;          /* BAR0 MMIO base */

    /* TX ring */
    e1000_tx_desc_t   *tx_ring;       /* Descriptor ring (DMA) */
    uint64_t           tx_ring_phys;
    void              *tx_bufs[E1000_NUM_TX_DESC]; /* TX buffer pointers */
    uint64_t           tx_bufs_phys[E1000_NUM_TX_DESC];
    uint16_t           tx_tail;

    /* RX ring */
    e1000_rx_desc_t   *rx_ring;       /* Descriptor ring (DMA) */
    uint64_t           rx_ring_phys;
    void              *rx_bufs[E1000_NUM_RX_DESC]; /* RX buffer pointers */
    uint64_t           rx_bufs_phys[E1000_NUM_RX_DESC];
    uint16_t           rx_tail;

    /* MAC address */
    uint8_t            mac[6];

    bool               initialized;
} e1000_dev_t;

/* ── Public API ── */

/* Initialize the e1000 NIC from a HAL device */
hal_status_t e1000_init(e1000_dev_t *nic, hal_device_t *dev);

/* Send a raw Ethernet frame. Returns HAL_OK on success. */
hal_status_t e1000_send(e1000_dev_t *nic, const void *frame, uint16_t length);

/* Receive a raw Ethernet frame (polling).
 * buf must be at least E1000_RX_BUF_SIZE bytes.
 * *length is set to actual received length.
 * Returns HAL_OK if a frame was received, HAL_NO_DEVICE if no frame pending. */
hal_status_t e1000_recv(e1000_dev_t *nic, void *buf, uint16_t *length);

/* Get MAC address */
void e1000_get_mac(e1000_dev_t *nic, uint8_t mac[6]);

/* Check link status (true = link up) */
bool e1000_link_up(e1000_dev_t *nic);

#endif /* ALJEFRA_DRV_E1000_H */
