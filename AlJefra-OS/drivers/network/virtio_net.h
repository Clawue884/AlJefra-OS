/* SPDX-License-Identifier: MIT */
/* AlJefra OS — VirtIO Network Driver
 * VirtIO 1.x network device driver (PCI or MMIO transport).
 * Architecture-independent; uses HAL for all hardware access.
 */

#ifndef ALJEFRA_DRV_VIRTIO_NET_H
#define ALJEFRA_DRV_VIRTIO_NET_H

#include "../../hal/hal.h"

/* Reuse VirtIO common definitions from virtio_blk.h for base structures,
 * but define network-specific items here. */

/* ── VirtIO common register offsets (PCI modern) ── */
#define VNET_COMMON_DFSELECT    0x00
#define VNET_COMMON_DF          0x04
#define VNET_COMMON_GFSELECT    0x08
#define VNET_COMMON_GF          0x0C
#define VNET_COMMON_MSIX        0x10
#define VNET_COMMON_NUMQ        0x12
#define VNET_COMMON_STATUS      0x14
#define VNET_COMMON_CFGGEN      0x15
#define VNET_COMMON_QSELECT     0x16
#define VNET_COMMON_QSIZE       0x18
#define VNET_COMMON_QMSIX       0x1A
#define VNET_COMMON_QENABLE     0x1C
#define VNET_COMMON_QNOTIFYOFF  0x1E
#define VNET_COMMON_QDESCLO     0x20
#define VNET_COMMON_QDESCHI     0x24
#define VNET_COMMON_QDRIVERLO   0x28
#define VNET_COMMON_QDRIVERHI   0x2C
#define VNET_COMMON_QDEVICELO   0x30
#define VNET_COMMON_QDEVICEHI   0x34

/* ── VirtIO MMIO offsets ── */
#define VNET_MMIO_MAGIC         0x000
#define VNET_MMIO_VERSION       0x004
#define VNET_MMIO_DEVICE_ID     0x008
#define VNET_MMIO_VENDOR_ID     0x00C
#define VNET_MMIO_DEV_FEAT      0x010
#define VNET_MMIO_DEV_FEAT_SEL  0x014
#define VNET_MMIO_DRV_FEAT      0x020
#define VNET_MMIO_DRV_FEAT_SEL  0x024
#define VNET_MMIO_QUEUE_SEL     0x030
#define VNET_MMIO_QUEUE_NUM_MAX 0x034
#define VNET_MMIO_QUEUE_NUM     0x038
#define VNET_MMIO_QUEUE_READY   0x044
#define VNET_MMIO_QUEUE_NOTIFY  0x050
#define VNET_MMIO_INT_STATUS    0x060
#define VNET_MMIO_INT_ACK       0x064
#define VNET_MMIO_STATUS        0x070
#define VNET_MMIO_QUEUE_DESC_LO 0x080
#define VNET_MMIO_QUEUE_DESC_HI 0x084
#define VNET_MMIO_QUEUE_AVAIL_LO 0x090
#define VNET_MMIO_QUEUE_AVAIL_HI 0x094
#define VNET_MMIO_QUEUE_USED_LO 0x0A0
#define VNET_MMIO_QUEUE_USED_HI 0x0A4

/* ── VirtIO PCI cap types ── */
#define VNET_PCI_CAP_COMMON     1
#define VNET_PCI_CAP_NOTIFY     2
#define VNET_PCI_CAP_ISR        3
#define VNET_PCI_CAP_DEVICE     4

/* ── VirtIO status bits ── */
#define VNET_STATUS_ACK         0x01
#define VNET_STATUS_DRIVER      0x02
#define VNET_STATUS_DRIVER_OK   0x04
#define VNET_STATUS_FEATURES_OK 0x08
#define VNET_STATUS_FAILED      0x80

/* ── VirtIO Network feature bits ── */
#define VIRTIO_NET_F_CSUM           (1u << 0)
#define VIRTIO_NET_F_GUEST_CSUM     (1u << 1)
#define VIRTIO_NET_F_MAC            (1u << 5)
#define VIRTIO_NET_F_STATUS         (1u << 16)
#define VIRTIO_NET_F_MRG_RXBUF     (1u << 15)

/* ── VirtIO Network header (prepended to every packet) ── */
typedef struct __attribute__((packed)) {
    uint8_t  flags;         /* VIRTIO_NET_HDR_F_* */
    uint8_t  gso_type;      /* VIRTIO_NET_HDR_GSO_* */
    uint16_t hdr_len;       /* Ethernet + IP + transport header length */
    uint16_t gso_size;      /* Maximum segment size for GSO */
    uint16_t csum_start;    /* Checksum start offset */
    uint16_t csum_offset;   /* Checksum value offset from csum_start */
    /* If VIRTIO_NET_F_MRG_RXBUF: uint16_t num_buffers follows */
} virtio_net_hdr_t;

#define VIRTIO_NET_HDR_F_NEEDS_CSUM  1
#define VIRTIO_NET_HDR_GSO_NONE      0
#define VIRTIO_NET_HDR_GSO_TCPV4     1
#define VIRTIO_NET_HDR_GSO_UDP       3
#define VIRTIO_NET_HDR_GSO_TCPV6     4

/* ── Virtqueue descriptor (same layout as virtio_blk) ── */
typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} vnet_desc_t;

#define VNET_DESC_F_NEXT    1
#define VNET_DESC_F_WRITE   2

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} vnet_avail_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} vnet_used_elem_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    vnet_used_elem_t ring[];
} vnet_used_t;

/* ── Queue sizes ── */
#define VNET_RX_QUEUE_SIZE   64
#define VNET_TX_QUEUE_SIZE   64
#define VNET_RX_BUF_SIZE     2048

/* ── Virtqueue state ── */
typedef struct {
    vnet_desc_t   *desc;
    vnet_avail_t  *avail;
    vnet_used_t   *used;
    uint64_t       desc_phys;
    uint64_t       avail_phys;
    uint64_t       used_phys;
    uint16_t       size;
    uint16_t       free_head;
    uint16_t       last_used_idx;
    uint16_t       notify_off;       /* PCI notify offset for this queue */
} vnet_queue_t;

/* ── Transport type ── */
typedef enum {
    VNET_TRANSPORT_PCI  = 0,
    VNET_TRANSPORT_MMIO = 1,
} vnet_transport_t;

/* ── VirtIO Network device state ── */
typedef struct {
    hal_device_t     dev;
    vnet_transport_t transport;
    volatile void   *common_cfg;
    volatile void   *notify_base;
    volatile void   *device_cfg;
    uint32_t         notify_off_mul;

    vnet_queue_t     rxq;               /* Receive queue (queue 0) */
    vnet_queue_t     txq;               /* Transmit queue (queue 1) */

    /* RX buffers (pre-allocated, posted to RX virtqueue) */
    void            *rx_bufs[VNET_RX_QUEUE_SIZE];
    uint64_t         rx_bufs_phys[VNET_RX_QUEUE_SIZE];

    /* TX header + buffer (per-slot) */
    void            *tx_hdrs[VNET_TX_QUEUE_SIZE];
    uint64_t         tx_hdrs_phys[VNET_TX_QUEUE_SIZE];

    uint8_t          mac[6];
    bool             initialized;
} virtio_net_dev_t;

/* ── Public API ── */

/* Initialize VirtIO network device (PCI transport) */
hal_status_t virtio_net_init(virtio_net_dev_t *dev, hal_device_t *hal_dev);

/* Initialize VirtIO network device (MMIO transport) */
hal_status_t virtio_net_init_mmio(virtio_net_dev_t *dev, volatile void *base);

/* Send a raw Ethernet frame */
hal_status_t virtio_net_send(virtio_net_dev_t *dev, const void *frame,
                             uint16_t length);

/* Receive a raw Ethernet frame (polling).
 * buf must be at least VNET_RX_BUF_SIZE bytes.
 * *length set to actual received bytes (excluding virtio_net_hdr).
 * Returns HAL_OK if frame received, HAL_NO_DEVICE if none available. */
hal_status_t virtio_net_recv(virtio_net_dev_t *dev, void *buf, uint16_t *length);

/* Get MAC address */
void virtio_net_get_mac(virtio_net_dev_t *dev, uint8_t mac[6]);

#endif /* ALJEFRA_DRV_VIRTIO_NET_H */
