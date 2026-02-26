/* SPDX-License-Identifier: MIT */
/* AlJefra OS — VirtIO Block Storage Driver Implementation
 * Architecture-independent VirtIO 1.x block device driver.
 * Supports both PCI and MMIO transports via HAL.
 */

#include "virtio_blk.h"
#include "../../lib/string.h"

/* ── Internal constants ── */
#define VIRTIO_TIMEOUT_MS     3000
#define VIRTIO_POLL_US        50

/* ── MMIO transport helpers ── */

static inline uint32_t vblk_mmio_read(virtio_blk_dev_t *dev, uint32_t off)
{
    return hal_mmio_read32((volatile void *)((uint8_t *)dev->common_cfg + off));
}

static inline void vblk_mmio_write(virtio_blk_dev_t *dev, uint32_t off, uint32_t val)
{
    hal_mmio_write32((volatile void *)((uint8_t *)dev->common_cfg + off), val);
}

/* ── PCI transport helpers ── */

static inline uint8_t vblk_pci_read8(volatile void *base, uint32_t off)
{
    return hal_mmio_read8((volatile void *)((uint8_t *)base + off));
}

static inline uint16_t vblk_pci_read16(volatile void *base, uint32_t off)
{
    return hal_mmio_read16((volatile void *)((uint8_t *)base + off));
}

static inline uint32_t vblk_pci_read32(volatile void *base, uint32_t off)
{
    return hal_mmio_read32((volatile void *)((uint8_t *)base + off));
}

static inline void vblk_pci_write8(volatile void *base, uint32_t off, uint8_t val)
{
    hal_mmio_write8((volatile void *)((uint8_t *)base + off), val);
}

static inline void vblk_pci_write16(volatile void *base, uint32_t off, uint16_t val)
{
    hal_mmio_write16((volatile void *)((uint8_t *)base + off), val);
}

static inline void vblk_pci_write32(volatile void *base, uint32_t off, uint32_t val)
{
    hal_mmio_write32((volatile void *)((uint8_t *)base + off), val);
}

/* ── Device status helpers (transport-independent) ── */

static void vblk_set_status(virtio_blk_dev_t *dev, uint8_t status)
{
    if (dev->transport == VIRTIO_TRANSPORT_MMIO) {
        vblk_mmio_write(dev, VIRTIO_MMIO_STATUS, status);
    } else {
        vblk_pci_write8(dev->common_cfg, VIRTIO_COMMON_STATUS, status);
    }
}

static uint8_t vblk_get_status(virtio_blk_dev_t *dev)
{
    if (dev->transport == VIRTIO_TRANSPORT_MMIO) {
        return (uint8_t)vblk_mmio_read(dev, VIRTIO_MMIO_STATUS);
    } else {
        return vblk_pci_read8(dev->common_cfg, VIRTIO_COMMON_STATUS);
    }
}

/* ── Virtqueue setup ── */

static hal_status_t vblk_setup_vq(virtio_blk_dev_t *dev)
{
    virtqueue_t *vq = &dev->vq;
    uint16_t qsize;

    if (dev->transport == VIRTIO_TRANSPORT_MMIO) {
        /* Select queue 0 */
        vblk_mmio_write(dev, VIRTIO_MMIO_QUEUE_SEL, 0);
        qsize = (uint16_t)vblk_mmio_read(dev, VIRTIO_MMIO_QUEUE_NUM_MAX);
    } else {
        vblk_pci_write16(dev->common_cfg, VIRTIO_COMMON_QSELECT, 0);
        qsize = vblk_pci_read16(dev->common_cfg, VIRTIO_COMMON_QSIZE);
    }

    if (qsize == 0)
        return HAL_ERROR;
    if (qsize > VIRTIO_BLK_QUEUE_SIZE)
        qsize = VIRTIO_BLK_QUEUE_SIZE;

    vq->size = qsize;

    /* Allocate descriptor table: qsize * 16 bytes */
    uint64_t desc_size = (uint64_t)qsize * sizeof(virtq_desc_t);
    vq->desc = (virtq_desc_t *)hal_dma_alloc(desc_size, &vq->desc_phys);
    if (!vq->desc)
        return HAL_NO_MEMORY;
    memset(vq->desc, 0, desc_size);

    /* Allocate available ring: 6 + 2*qsize bytes (with padding to align) */
    uint64_t avail_size = 6 + 2 * (uint64_t)qsize;
    vq->avail = (virtq_avail_t *)hal_dma_alloc(avail_size, &vq->avail_phys);
    if (!vq->avail)
        return HAL_NO_MEMORY;
    memset(vq->avail, 0, avail_size);

    /* Allocate used ring: 6 + 8*qsize bytes */
    uint64_t used_size = 6 + 8 * (uint64_t)qsize;
    vq->used = (virtq_used_t *)hal_dma_alloc(used_size, &vq->used_phys);
    if (!vq->used)
        return HAL_NO_MEMORY;
    memset(vq->used, 0, used_size);

    /* Initialize free descriptor chain */
    for (uint16_t i = 0; i < qsize - 1; i++) {
        vq->desc[i].next = i + 1;
        vq->desc[i].flags = VRING_DESC_F_NEXT;
    }
    vq->desc[qsize - 1].next = 0;
    vq->desc[qsize - 1].flags = 0;
    vq->free_head = 0;
    vq->last_used_idx = 0;

    /* Tell the device about the queue addresses */
    if (dev->transport == VIRTIO_TRANSPORT_MMIO) {
        vblk_mmio_write(dev, VIRTIO_MMIO_QUEUE_SEL, 0);
        vblk_mmio_write(dev, VIRTIO_MMIO_QUEUE_NUM, qsize);
        vblk_mmio_write(dev, VIRTIO_MMIO_QUEUE_DESC_LO,
                         (uint32_t)(vq->desc_phys & 0xFFFFFFFF));
        vblk_mmio_write(dev, VIRTIO_MMIO_QUEUE_DESC_HI,
                         (uint32_t)(vq->desc_phys >> 32));
        vblk_mmio_write(dev, VIRTIO_MMIO_QUEUE_AVAIL_LO,
                         (uint32_t)(vq->avail_phys & 0xFFFFFFFF));
        vblk_mmio_write(dev, VIRTIO_MMIO_QUEUE_AVAIL_HI,
                         (uint32_t)(vq->avail_phys >> 32));
        vblk_mmio_write(dev, VIRTIO_MMIO_QUEUE_USED_LO,
                         (uint32_t)(vq->used_phys & 0xFFFFFFFF));
        vblk_mmio_write(dev, VIRTIO_MMIO_QUEUE_USED_HI,
                         (uint32_t)(vq->used_phys >> 32));
        vblk_mmio_write(dev, VIRTIO_MMIO_QUEUE_READY, 1);
    } else {
        vblk_pci_write16(dev->common_cfg, VIRTIO_COMMON_QSELECT, 0);
        vblk_pci_write16(dev->common_cfg, VIRTIO_COMMON_QSIZE, qsize);
        vblk_pci_write32(dev->common_cfg, VIRTIO_COMMON_QDESCLO,
                          (uint32_t)(vq->desc_phys & 0xFFFFFFFF));
        vblk_pci_write32(dev->common_cfg, VIRTIO_COMMON_QDESCHI,
                          (uint32_t)(vq->desc_phys >> 32));
        vblk_pci_write32(dev->common_cfg, VIRTIO_COMMON_QDRIVERLO,
                          (uint32_t)(vq->avail_phys & 0xFFFFFFFF));
        vblk_pci_write32(dev->common_cfg, VIRTIO_COMMON_QDRIVERHI,
                          (uint32_t)(vq->avail_phys >> 32));
        vblk_pci_write32(dev->common_cfg, VIRTIO_COMMON_QDEVICELO,
                          (uint32_t)(vq->used_phys & 0xFFFFFFFF));
        vblk_pci_write32(dev->common_cfg, VIRTIO_COMMON_QDEVICEHI,
                          (uint32_t)(vq->used_phys >> 32));
        vblk_pci_write16(dev->common_cfg, VIRTIO_COMMON_QENABLE, 1);
    }

    return HAL_OK;
}

/* Allocate a descriptor from the free list. Returns index or 0xFFFF on empty. */
static uint16_t vblk_alloc_desc(virtqueue_t *vq)
{
    if (vq->free_head >= vq->size)
        return 0xFFFF;
    uint16_t idx = vq->free_head;
    vq->free_head = vq->desc[idx].next;
    return idx;
}

/* Free a descriptor chain back to the free list */
static void vblk_free_desc_chain(virtqueue_t *vq, uint16_t head)
{
    uint16_t idx = head;
    while (vq->desc[idx].flags & VRING_DESC_F_NEXT) {
        uint16_t next = vq->desc[idx].next;
        vq->desc[idx].next = vq->free_head;
        vq->desc[idx].flags = VRING_DESC_F_NEXT;
        vq->free_head = idx;
        idx = next;
    }
    vq->desc[idx].next = vq->free_head;
    vq->desc[idx].flags = VRING_DESC_F_NEXT;
    vq->free_head = idx;
}

/* Notify the device about queue activity */
static void vblk_notify(virtio_blk_dev_t *dev)
{
    hal_mmio_barrier();
    if (dev->transport == VIRTIO_TRANSPORT_MMIO) {
        vblk_mmio_write(dev, VIRTIO_MMIO_QUEUE_NOTIFY, 0);
    } else {
        /* PCI: read notify offset for queue 0 */
        uint16_t notify_off = vblk_pci_read16(dev->common_cfg,
                                               VIRTIO_COMMON_QNOTIFYOFF);
        uint32_t offset = (uint32_t)notify_off * dev->notify_off_mul;
        hal_mmio_write16((volatile void *)((uint8_t *)dev->notify_base + offset), 0);
    }
}

/* ── Block I/O ── */

static hal_status_t vblk_do_io(virtio_blk_dev_t *dev, uint64_t sector,
                                uint32_t count, uint64_t buf_phys, bool write)
{
    if (!dev->initialized)
        return HAL_ERROR;
    if (write && dev->readonly)
        return HAL_NOT_SUPPORTED;

    virtqueue_t *vq = &dev->vq;

    /* We need 3 descriptors per request: header, data, status */
    uint16_t d_hdr = vblk_alloc_desc(vq);
    uint16_t d_data = vblk_alloc_desc(vq);
    uint16_t d_status = vblk_alloc_desc(vq);
    if (d_hdr == 0xFFFF || d_data == 0xFFFF || d_status == 0xFFFF)
        return HAL_BUSY;

    /* Allocate DMA buffers for request header and status byte */
    uint64_t hdr_phys, status_phys;
    virtio_blk_req_t *req = (virtio_blk_req_t *)hal_dma_alloc(
        sizeof(virtio_blk_req_t), &hdr_phys);
    if (!req)
        return HAL_NO_MEMORY;

    uint8_t *status_buf = (uint8_t *)hal_dma_alloc(1, &status_phys);
    if (!status_buf) {
        hal_dma_free(req, sizeof(virtio_blk_req_t));
        return HAL_NO_MEMORY;
    }

    /* Fill request header */
    req->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req->reserved = 0;
    req->sector = sector;
    *status_buf = 0xFF;  /* Sentinel */

    uint32_t data_bytes = count * 512;

    /* Descriptor 0: request header (device reads) */
    vq->desc[d_hdr].addr = hdr_phys;
    vq->desc[d_hdr].len = sizeof(virtio_blk_req_t);
    vq->desc[d_hdr].flags = VRING_DESC_F_NEXT;
    vq->desc[d_hdr].next = d_data;

    /* Descriptor 1: data buffer */
    vq->desc[d_data].addr = buf_phys;
    vq->desc[d_data].len = data_bytes;
    vq->desc[d_data].flags = VRING_DESC_F_NEXT;
    if (!write)
        vq->desc[d_data].flags |= VRING_DESC_F_WRITE;  /* Device writes to buffer */
    vq->desc[d_data].next = d_status;

    /* Descriptor 2: status byte (device writes) */
    vq->desc[d_status].addr = status_phys;
    vq->desc[d_status].len = 1;
    vq->desc[d_status].flags = VRING_DESC_F_WRITE;
    vq->desc[d_status].next = 0;

    /* Add to available ring */
    uint16_t avail_idx = vq->avail->idx;
    vq->avail->ring[avail_idx % vq->size] = d_hdr;
    hal_mmio_barrier();
    vq->avail->idx = avail_idx + 1;

    /* Notify device */
    vblk_notify(dev);

    /* Poll used ring for completion */
    uint64_t deadline = hal_timer_ms() + VIRTIO_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        hal_mmio_barrier();
        if (vq->used->idx != vq->last_used_idx) {
            vq->last_used_idx++;
            break;
        }
        hal_timer_delay_us(VIRTIO_POLL_US);
    }

    if (vq->used->idx == (uint16_t)(vq->last_used_idx - 1)) {
        /* Timed out — still try to free descriptors */
        vblk_free_desc_chain(vq, d_hdr);
        hal_dma_free(req, sizeof(virtio_blk_req_t));
        hal_dma_free(status_buf, 1);
        return HAL_TIMEOUT;
    }

    /* Check status */
    hal_mmio_barrier();
    uint8_t io_status = *status_buf;

    /* Free resources */
    vblk_free_desc_chain(vq, d_hdr);
    hal_dma_free(req, sizeof(virtio_blk_req_t));
    hal_dma_free(status_buf, 1);

    return (io_status == VIRTIO_BLK_S_OK) ? HAL_OK : HAL_ERROR;
}

/* ── PCI capability discovery ── */

static hal_status_t vblk_find_pci_caps(virtio_blk_dev_t *dev, hal_device_t *hal_dev)
{
    /* Walk PCI capability list to find VirtIO-specific caps.
     * Capability pointer at config offset 0x34. */
    uint32_t bdf = ((uint32_t)hal_dev->bus << 8) |
                   ((uint32_t)hal_dev->dev << 3) | hal_dev->func;

    uint32_t status_cmd = hal_bus_pci_read32(bdf, 0x04);
    if (!(status_cmd & (1u << 20)))  /* Capabilities List bit in Status */
        return HAL_NOT_SUPPORTED;

    uint8_t cap_ptr = (uint8_t)(hal_bus_pci_read32(bdf, 0x34) & 0xFF);

    volatile void *bar_mapped[6] = {0};

    while (cap_ptr != 0) {
        uint32_t cap_hdr = hal_bus_pci_read32(bdf, cap_ptr);
        uint8_t cap_id = (uint8_t)(cap_hdr & 0xFF);
        uint8_t cap_next = (uint8_t)((cap_hdr >> 8) & 0xFF);

        if (cap_id == 0x09) {  /* Vendor-specific (VirtIO) */
            /* VirtIO PCI cap layout:
             * dword 0: cap_vndr(8) | cap_next(8) | cap_len(8) | cfg_type(8)
             * dword 1: bar(8) | id(8) | padding(16)
             * dword 2: offset(32)
             * dword 3: length(32) */
            uint8_t cfg_type = (uint8_t)((cap_hdr >> 24) & 0xFF);
            uint32_t dw1 = hal_bus_pci_read32(bdf, cap_ptr + 4);
            uint8_t bar_idx  = (uint8_t)(dw1 & 0xFF);
            uint32_t offset  = hal_bus_pci_read32(bdf, cap_ptr + 8);

            /* Map the BAR if not already done */
            if (!bar_mapped[bar_idx] && bar_idx < HAL_BUS_MAX_BARS) {
                bar_mapped[bar_idx] = hal_bus_map_bar(hal_dev, bar_idx);
            }

            if (!bar_mapped[bar_idx]) {
                cap_ptr = cap_next;
                continue;
            }

            volatile void *base = (volatile void *)(
                (uint8_t *)bar_mapped[bar_idx] + offset);

            switch (cfg_type) {
            case VIRTIO_PCI_CAP_COMMON_CFG:
                dev->common_cfg = base;
                break;
            case VIRTIO_PCI_CAP_NOTIFY_CFG:
                dev->notify_base = base;
                /* Notify offset multiplier at cap_ptr + 16 */
                dev->notify_off_mul = hal_bus_pci_read32(bdf, cap_ptr + 16);
                break;
            case VIRTIO_PCI_CAP_DEVICE_CFG:
                dev->device_cfg = base;
                break;
            case VIRTIO_PCI_CAP_ISR_CFG:
                /* ISR status — not needed for polling mode */
                break;
            }
        }

        cap_ptr = cap_next;
    }

    if (!dev->common_cfg)
        return HAL_ERROR;

    return HAL_OK;
}

/* ── Read block device config (capacity, sector size) ── */

static void vblk_read_config(virtio_blk_dev_t *dev)
{
    if (dev->transport == VIRTIO_TRANSPORT_MMIO) {
        /* Capacity at device config offset 0x100 (MMIO transport) */
        /* Actually for MMIO v2, device-specific config starts at 0x100 */
        volatile void *cfg = (volatile void *)((uint8_t *)dev->common_cfg + 0x100);
        uint32_t cap_lo = hal_mmio_read32(cfg);
        uint32_t cap_hi = hal_mmio_read32((volatile void *)((uint8_t *)cfg + 4));
        dev->capacity = ((uint64_t)cap_hi << 32) | cap_lo;
        dev->sector_size = 512;
    } else if (dev->device_cfg) {
        /* PCI: device config has capacity at offset 0 (8 bytes) */
        uint32_t cap_lo = hal_mmio_read32(dev->device_cfg);
        uint32_t cap_hi = hal_mmio_read32(
            (volatile void *)((uint8_t *)dev->device_cfg + 4));
        dev->capacity = ((uint64_t)cap_hi << 32) | cap_lo;
        dev->sector_size = 512;
    }
}

/* ── Public API ── */

hal_status_t virtio_blk_init(virtio_blk_dev_t *dev, hal_device_t *hal_dev)
{
    dev->dev = *hal_dev;
    dev->transport = VIRTIO_TRANSPORT_PCI;
    dev->initialized = false;
    dev->readonly = false;
    dev->common_cfg = NULL;
    dev->notify_base = NULL;
    dev->device_cfg = NULL;
    dev->notify_off_mul = 0;

    /* Enable bus mastering */
    hal_bus_pci_enable(hal_dev);

    /* Find VirtIO PCI capabilities */
    hal_status_t st = vblk_find_pci_caps(dev, hal_dev);
    if (st != HAL_OK)
        return st;

    /* Reset device */
    vblk_set_status(dev, 0);
    hal_timer_delay_us(1000);

    /* Acknowledge + Driver */
    vblk_set_status(dev, VIRTIO_STATUS_ACK);
    vblk_set_status(dev, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    /* Negotiate features — accept SIZE_MAX, SEG_MAX, BLK_SIZE; check RO */
    vblk_pci_write32(dev->common_cfg, VIRTIO_COMMON_DFSELECT, 0);
    uint32_t dev_features = vblk_pci_read32(dev->common_cfg, VIRTIO_COMMON_DF);

    if (dev_features & VIRTIO_BLK_F_RO)
        dev->readonly = true;

    uint32_t drv_features = dev_features & (VIRTIO_BLK_F_SIZE_MAX |
                                             VIRTIO_BLK_F_SEG_MAX |
                                             VIRTIO_BLK_F_BLK_SIZE |
                                             VIRTIO_BLK_F_FLUSH);
    vblk_pci_write32(dev->common_cfg, VIRTIO_COMMON_GFSELECT, 0);
    vblk_pci_write32(dev->common_cfg, VIRTIO_COMMON_GF, drv_features);

    /* Features OK */
    vblk_set_status(dev, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                         VIRTIO_STATUS_FEATURES_OK);

    /* Verify FEATURES_OK was accepted */
    if (!(vblk_get_status(dev) & VIRTIO_STATUS_FEATURES_OK))
        return HAL_ERROR;

    /* Set up virtqueue */
    st = vblk_setup_vq(dev);
    if (st != HAL_OK)
        return st;

    /* Read config */
    vblk_read_config(dev);

    /* DRIVER_OK */
    vblk_set_status(dev, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                         VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    dev->initialized = true;
    return HAL_OK;
}

hal_status_t virtio_blk_init_mmio(virtio_blk_dev_t *dev, volatile void *base)
{
    dev->transport = VIRTIO_TRANSPORT_MMIO;
    dev->initialized = false;
    dev->readonly = false;
    dev->common_cfg = base;
    dev->notify_base = NULL;
    dev->device_cfg = NULL;
    dev->notify_off_mul = 0;

    /* Verify magic */
    uint32_t magic = vblk_mmio_read(dev, VIRTIO_MMIO_MAGIC);
    if (magic != 0x74726976)
        return HAL_NO_DEVICE;

    /* Check version (must be 2 for modern) */
    uint32_t version = vblk_mmio_read(dev, VIRTIO_MMIO_VERSION);
    if (version != 2)
        return HAL_NOT_SUPPORTED;

    /* Check device ID (2 = block device) */
    uint32_t device_id = vblk_mmio_read(dev, VIRTIO_MMIO_DEVICE_ID);
    if (device_id != 2)
        return HAL_NO_DEVICE;

    /* Reset */
    vblk_set_status(dev, 0);
    hal_timer_delay_us(1000);

    /* Acknowledge + Driver */
    vblk_set_status(dev, VIRTIO_STATUS_ACK);
    vblk_set_status(dev, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    /* Negotiate features */
    vblk_mmio_write(dev, VIRTIO_MMIO_DEV_FEAT_SEL, 0);
    uint32_t dev_features = vblk_mmio_read(dev, VIRTIO_MMIO_DEV_FEAT);

    if (dev_features & VIRTIO_BLK_F_RO)
        dev->readonly = true;

    uint32_t drv_features = dev_features & (VIRTIO_BLK_F_SIZE_MAX |
                                             VIRTIO_BLK_F_SEG_MAX |
                                             VIRTIO_BLK_F_BLK_SIZE);
    vblk_mmio_write(dev, VIRTIO_MMIO_DRV_FEAT_SEL, 0);
    vblk_mmio_write(dev, VIRTIO_MMIO_DRV_FEAT, drv_features);

    /* Features OK */
    vblk_set_status(dev, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                         VIRTIO_STATUS_FEATURES_OK);

    if (!(vblk_get_status(dev) & VIRTIO_STATUS_FEATURES_OK))
        return HAL_ERROR;

    /* Set up virtqueue */
    hal_status_t st = vblk_setup_vq(dev);
    if (st != HAL_OK)
        return st;

    /* Read config */
    vblk_read_config(dev);

    /* DRIVER_OK */
    vblk_set_status(dev, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                         VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    dev->initialized = true;
    return HAL_OK;
}

hal_status_t virtio_blk_read(virtio_blk_dev_t *dev, uint64_t sector,
                             uint32_t count, void *buf, uint64_t buf_phys)
{
    (void)buf;
    return vblk_do_io(dev, sector, count, buf_phys, false);
}

hal_status_t virtio_blk_write(virtio_blk_dev_t *dev, uint64_t sector,
                              uint32_t count, const void *buf, uint64_t buf_phys)
{
    (void)buf;
    return vblk_do_io(dev, sector, count, buf_phys, true);
}

uint64_t virtio_blk_capacity(virtio_blk_dev_t *dev)
{
    return dev->initialized ? dev->capacity : 0;
}

/* ── driver_ops_t wrapper for built-in driver registration ── */
#include "../../kernel/driver_loader.h"

static virtio_blk_dev_t g_virtio_blk;

static hal_status_t virtio_blk_drv_init(hal_device_t *dev)
{
    return virtio_blk_init(&g_virtio_blk, dev);
}

static int64_t virtio_blk_drv_read(void *buf, uint64_t lba, uint32_t count)
{
    /* Bare-metal identity mapping: virtual address == physical address */
    hal_status_t rc = virtio_blk_read(&g_virtio_blk, lba, count,
                                       buf, (uint64_t)buf);
    return (rc == HAL_OK) ? (int64_t)count : -1;
}

static int64_t virtio_blk_drv_write(const void *buf, uint64_t lba, uint32_t count)
{
    hal_status_t rc = virtio_blk_write(&g_virtio_blk, lba, count,
                                        buf, (uint64_t)buf);
    return (rc == HAL_OK) ? (int64_t)count : -1;
}

static const driver_ops_t virtio_blk_driver_ops = {
    .name       = "virtio-blk",
    .category   = DRIVER_CAT_STORAGE,
    .init       = virtio_blk_drv_init,
    .shutdown   = NULL,
    .read       = virtio_blk_drv_read,
    .write      = virtio_blk_drv_write,
};

void virtio_blk_register(void)
{
    driver_register_builtin(&virtio_blk_driver_ops);
}
