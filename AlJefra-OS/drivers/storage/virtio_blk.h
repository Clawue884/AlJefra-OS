/* SPDX-License-Identifier: MIT */
/* AlJefra OS — VirtIO Block Storage Driver
 * VirtIO 1.x block device driver (PCI or MMIO transport).
 * Architecture-independent; uses HAL for all hardware access.
 */

#ifndef ALJEFRA_DRV_VIRTIO_BLK_H
#define ALJEFRA_DRV_VIRTIO_BLK_H

#include "../../hal/hal.h"

/* ── VirtIO PCI capability types ── */
#define VIRTIO_PCI_CAP_COMMON_CFG   1
#define VIRTIO_PCI_CAP_NOTIFY_CFG   2
#define VIRTIO_PCI_CAP_ISR_CFG      3
#define VIRTIO_PCI_CAP_DEVICE_CFG   4

/* ── VirtIO Common Configuration register offsets ── */
#define VIRTIO_COMMON_DFSELECT       0x00  /* Device Feature Select (32) */
#define VIRTIO_COMMON_DF             0x04  /* Device Feature (32) */
#define VIRTIO_COMMON_GFSELECT       0x08  /* Guest Feature Select (32) */
#define VIRTIO_COMMON_GF             0x0C  /* Guest Feature (32) */
#define VIRTIO_COMMON_MSIX           0x10  /* MSI-X Config Vector (16) */
#define VIRTIO_COMMON_NUMQ           0x12  /* Number of Queues (16) */
#define VIRTIO_COMMON_STATUS         0x14  /* Device Status (8) */
#define VIRTIO_COMMON_CFGGEN         0x15  /* Config Generation (8) */
#define VIRTIO_COMMON_QSELECT        0x16  /* Queue Select (16) */
#define VIRTIO_COMMON_QSIZE          0x18  /* Queue Size (16) */
#define VIRTIO_COMMON_QMSIX          0x1A  /* Queue MSI-X Vector (16) */
#define VIRTIO_COMMON_QENABLE        0x1C  /* Queue Enable (16) */
#define VIRTIO_COMMON_QNOTIFYOFF     0x1E  /* Queue Notify Offset (16) */
#define VIRTIO_COMMON_QDESCLO        0x20  /* Queue Descriptor Low (32) */
#define VIRTIO_COMMON_QDESCHI        0x24  /* Queue Descriptor High (32) */
#define VIRTIO_COMMON_QDRIVERLO      0x28  /* Queue Driver (Available) Low (32) */
#define VIRTIO_COMMON_QDRIVERHI      0x2C  /* Queue Driver (Available) High (32) */
#define VIRTIO_COMMON_QDEVICELO      0x30  /* Queue Device (Used) Low (32) */
#define VIRTIO_COMMON_QDEVICEHI      0x34  /* Queue Device (Used) High (32) */

/* ── VirtIO MMIO register offsets (alternative transport) ── */
#define VIRTIO_MMIO_MAGIC            0x000  /* 0x74726976 */
#define VIRTIO_MMIO_VERSION          0x004  /* Device version (2 for modern) */
#define VIRTIO_MMIO_DEVICE_ID        0x008  /* Virtio Subsystem Device ID */
#define VIRTIO_MMIO_VENDOR_ID        0x00C  /* Virtio Subsystem Vendor ID */
#define VIRTIO_MMIO_DEV_FEAT         0x010  /* Device features */
#define VIRTIO_MMIO_DEV_FEAT_SEL     0x014  /* Device feature word select */
#define VIRTIO_MMIO_DRV_FEAT         0x020  /* Driver features */
#define VIRTIO_MMIO_DRV_FEAT_SEL     0x024  /* Driver feature word select */
#define VIRTIO_MMIO_QUEUE_SEL        0x030  /* Virtual queue index */
#define VIRTIO_MMIO_QUEUE_NUM_MAX    0x034  /* Max virtual queue size */
#define VIRTIO_MMIO_QUEUE_NUM        0x038  /* Virtual queue size */
#define VIRTIO_MMIO_QUEUE_READY      0x044  /* Virtual queue ready */
#define VIRTIO_MMIO_QUEUE_NOTIFY     0x050  /* Queue notification */
#define VIRTIO_MMIO_INT_STATUS       0x060  /* Interrupt status */
#define VIRTIO_MMIO_INT_ACK          0x064  /* Interrupt acknowledge */
#define VIRTIO_MMIO_STATUS           0x070  /* Device status */
#define VIRTIO_MMIO_QUEUE_DESC_LO    0x080  /* Descriptor table address low */
#define VIRTIO_MMIO_QUEUE_DESC_HI    0x084  /* Descriptor table address high */
#define VIRTIO_MMIO_QUEUE_AVAIL_LO   0x090  /* Available ring address low */
#define VIRTIO_MMIO_QUEUE_AVAIL_HI   0x094  /* Available ring address high */
#define VIRTIO_MMIO_QUEUE_USED_LO    0x0A0  /* Used ring address low */
#define VIRTIO_MMIO_QUEUE_USED_HI    0x0A4  /* Used ring address high */

/* ── VirtIO Device Status bits ── */
#define VIRTIO_STATUS_ACK            0x01
#define VIRTIO_STATUS_DRIVER         0x02
#define VIRTIO_STATUS_DRIVER_OK      0x04
#define VIRTIO_STATUS_FEATURES_OK    0x08
#define VIRTIO_STATUS_NEEDS_RESET    0x40
#define VIRTIO_STATUS_FAILED         0x80

/* ── VirtIO Block feature bits ── */
#define VIRTIO_BLK_F_SIZE_MAX   (1u << 1)
#define VIRTIO_BLK_F_SEG_MAX    (1u << 2)
#define VIRTIO_BLK_F_GEOMETRY   (1u << 4)
#define VIRTIO_BLK_F_RO         (1u << 5)
#define VIRTIO_BLK_F_BLK_SIZE   (1u << 6)
#define VIRTIO_BLK_F_FLUSH      (1u << 9)

/* ── VirtIO Block request types ── */
#define VIRTIO_BLK_T_IN         0   /* Read */
#define VIRTIO_BLK_T_OUT        1   /* Write */
#define VIRTIO_BLK_T_FLUSH      4   /* Flush */

/* ── VirtIO Block status values ── */
#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2

/* ── Virtqueue sizes ── */
#define VIRTIO_BLK_QUEUE_SIZE   128

/* ── Virtqueue Descriptor (16 bytes) ── */
typedef struct __attribute__((packed)) {
    uint64_t addr;      /* Buffer physical address */
    uint32_t len;       /* Buffer length */
    uint16_t flags;     /* VRING_DESC_F_* */
    uint16_t next;      /* Next descriptor index (if flags & NEXT) */
} virtq_desc_t;

#define VRING_DESC_F_NEXT       1   /* Buffer continues via `next` */
#define VRING_DESC_F_WRITE      2   /* Device writes (vs. device reads) */

/* ── Available Ring ── */
typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];    /* ring[queue_size] */
} virtq_avail_t;

/* ── Used Ring Element ── */
typedef struct __attribute__((packed)) {
    uint32_t id;        /* Descriptor chain head index */
    uint32_t len;       /* Bytes written by device */
} virtq_used_elem_t;

/* ── Used Ring ── */
typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[];  /* ring[queue_size] */
} virtq_used_t;

/* ── VirtIO Block Request Header (16 bytes) ── */
typedef struct __attribute__((packed)) {
    uint32_t type;      /* VIRTIO_BLK_T_* */
    uint32_t reserved;
    uint64_t sector;    /* Starting sector (512-byte units) */
} virtio_blk_req_t;

/* ── Transport type ── */
typedef enum {
    VIRTIO_TRANSPORT_PCI  = 0,
    VIRTIO_TRANSPORT_MMIO = 1,
} virtio_transport_t;

/* ── Virtqueue state ── */
typedef struct {
    virtq_desc_t    *desc;          /* Descriptor table */
    virtq_avail_t   *avail;         /* Available ring */
    virtq_used_t    *used;          /* Used ring */
    uint64_t         desc_phys;
    uint64_t         avail_phys;
    uint64_t         used_phys;
    uint16_t         size;          /* Queue size (number of entries) */
    uint16_t         free_head;     /* Head of free descriptor list */
    uint16_t         last_used_idx; /* Last processed used index */
} virtqueue_t;

/* ── VirtIO Block device state ── */
typedef struct {
    hal_device_t      dev;
    virtio_transport_t transport;
    volatile void     *common_cfg;   /* PCI: common config MMIO / MMIO: base regs */
    volatile void     *notify_base;  /* PCI: notify cap base */
    volatile void     *device_cfg;   /* PCI: device-specific config */
    uint32_t           notify_off_mul; /* PCI: notify offset multiplier */
    virtqueue_t        vq;           /* Single virtqueue (queue 0) */
    uint64_t           capacity;     /* Total sectors (512-byte) */
    uint32_t           sector_size;  /* Logical sector size (usually 512) */
    bool               readonly;
    bool               initialized;
} virtio_blk_dev_t;

/* ── Public API ── */

/* Initialize VirtIO block device (PCI transport) */
hal_status_t virtio_blk_init(virtio_blk_dev_t *dev, hal_device_t *hal_dev);

/* Initialize VirtIO block device (MMIO transport) */
hal_status_t virtio_blk_init_mmio(virtio_blk_dev_t *dev, volatile void *base);

/* Read sectors */
hal_status_t virtio_blk_read(virtio_blk_dev_t *dev, uint64_t sector,
                             uint32_t count, void *buf, uint64_t buf_phys);

/* Write sectors */
hal_status_t virtio_blk_write(virtio_blk_dev_t *dev, uint64_t sector,
                              uint32_t count, const void *buf, uint64_t buf_phys);

/* Get capacity in sectors */
uint64_t virtio_blk_capacity(virtio_blk_dev_t *dev);

#endif /* ALJEFRA_DRV_VIRTIO_BLK_H */
