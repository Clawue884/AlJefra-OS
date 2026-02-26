/* SPDX-License-Identifier: MIT */
/* AlJefra OS — VirtIO Network Driver Implementation
 * Architecture-independent VirtIO 1.x network device driver.
 * Supports PCI and MMIO transports via HAL.
 */

#include "virtio_net.h"
#include "../../lib/string.h"

/* ── Constants ── */
#define VNET_TIMEOUT_MS   3000
#define VNET_POLL_US      50

/* ── Transport abstraction ── */

static inline uint8_t vnet_cfg_read8(volatile void *base, uint32_t off)
{
    return hal_mmio_read8((volatile void *)((uint8_t *)base + off));
}

static inline uint16_t vnet_cfg_read16(volatile void *base, uint32_t off)
{
    return hal_mmio_read16((volatile void *)((uint8_t *)base + off));
}

static inline uint32_t vnet_cfg_read32(volatile void *base, uint32_t off)
{
    return hal_mmio_read32((volatile void *)((uint8_t *)base + off));
}

static inline void vnet_cfg_write8(volatile void *base, uint32_t off, uint8_t v)
{
    hal_mmio_write8((volatile void *)((uint8_t *)base + off), v);
}

static inline void vnet_cfg_write16(volatile void *base, uint32_t off, uint16_t v)
{
    hal_mmio_write16((volatile void *)((uint8_t *)base + off), v);
}

static inline void vnet_cfg_write32(volatile void *base, uint32_t off, uint32_t v)
{
    hal_mmio_write32((volatile void *)((uint8_t *)base + off), v);
}

static void vnet_set_status(virtio_net_dev_t *dev, uint8_t status)
{
    if (dev->transport == VNET_TRANSPORT_MMIO)
        vnet_cfg_write32(dev->common_cfg, VNET_MMIO_STATUS, status);
    else
        vnet_cfg_write8(dev->common_cfg, VNET_COMMON_STATUS, status);
}

static uint8_t vnet_get_status(virtio_net_dev_t *dev)
{
    if (dev->transport == VNET_TRANSPORT_MMIO)
        return (uint8_t)vnet_cfg_read32(dev->common_cfg, VNET_MMIO_STATUS);
    else
        return vnet_cfg_read8(dev->common_cfg, VNET_COMMON_STATUS);
}

/* ── Queue setup ── */

static hal_status_t vnet_setup_queue(virtio_net_dev_t *dev, vnet_queue_t *q,
                                      uint16_t qidx, uint16_t max_size)
{
    uint16_t qsize;

    if (dev->transport == VNET_TRANSPORT_MMIO) {
        vnet_cfg_write32(dev->common_cfg, VNET_MMIO_QUEUE_SEL, qidx);
        qsize = (uint16_t)vnet_cfg_read32(dev->common_cfg, VNET_MMIO_QUEUE_NUM_MAX);
    } else {
        vnet_cfg_write16(dev->common_cfg, VNET_COMMON_QSELECT, qidx);
        qsize = vnet_cfg_read16(dev->common_cfg, VNET_COMMON_QSIZE);
    }

    if (qsize == 0)
        return HAL_ERROR;
    if (qsize > max_size)
        qsize = max_size;

    q->size = qsize;

    /* Allocate descriptor table */
    uint64_t desc_sz = (uint64_t)qsize * sizeof(vnet_desc_t);
    q->desc = (vnet_desc_t *)hal_dma_alloc(desc_sz, &q->desc_phys);
    if (!q->desc)
        return HAL_NO_MEMORY;
    memset(q->desc, 0, desc_sz);

    /* Allocate available ring */
    uint64_t avail_sz = 6 + 2 * (uint64_t)qsize;
    q->avail = (vnet_avail_t *)hal_dma_alloc(avail_sz, &q->avail_phys);
    if (!q->avail)
        return HAL_NO_MEMORY;
    memset(q->avail, 0, avail_sz);

    /* Allocate used ring */
    uint64_t used_sz = 6 + 8 * (uint64_t)qsize;
    q->used = (vnet_used_t *)hal_dma_alloc(used_sz, &q->used_phys);
    if (!q->used)
        return HAL_NO_MEMORY;
    memset(q->used, 0, used_sz);

    /* Initialize free list */
    for (uint16_t i = 0; i < qsize - 1; i++) {
        q->desc[i].next = i + 1;
        q->desc[i].flags = VNET_DESC_F_NEXT;
    }
    q->desc[qsize - 1].next = 0;
    q->desc[qsize - 1].flags = 0;
    q->free_head = 0;
    q->last_used_idx = 0;

    /* Tell device about queue addresses */
    if (dev->transport == VNET_TRANSPORT_MMIO) {
        vnet_cfg_write32(dev->common_cfg, VNET_MMIO_QUEUE_SEL, qidx);
        vnet_cfg_write32(dev->common_cfg, VNET_MMIO_QUEUE_NUM, qsize);
        vnet_cfg_write32(dev->common_cfg, VNET_MMIO_QUEUE_DESC_LO,
                          (uint32_t)(q->desc_phys & 0xFFFFFFFF));
        vnet_cfg_write32(dev->common_cfg, VNET_MMIO_QUEUE_DESC_HI,
                          (uint32_t)(q->desc_phys >> 32));
        vnet_cfg_write32(dev->common_cfg, VNET_MMIO_QUEUE_AVAIL_LO,
                          (uint32_t)(q->avail_phys & 0xFFFFFFFF));
        vnet_cfg_write32(dev->common_cfg, VNET_MMIO_QUEUE_AVAIL_HI,
                          (uint32_t)(q->avail_phys >> 32));
        vnet_cfg_write32(dev->common_cfg, VNET_MMIO_QUEUE_USED_LO,
                          (uint32_t)(q->used_phys & 0xFFFFFFFF));
        vnet_cfg_write32(dev->common_cfg, VNET_MMIO_QUEUE_USED_HI,
                          (uint32_t)(q->used_phys >> 32));
        vnet_cfg_write32(dev->common_cfg, VNET_MMIO_QUEUE_READY, 1);
    } else {
        vnet_cfg_write16(dev->common_cfg, VNET_COMMON_QSELECT, qidx);
        vnet_cfg_write16(dev->common_cfg, VNET_COMMON_QSIZE, qsize);
        vnet_cfg_write32(dev->common_cfg, VNET_COMMON_QDESCLO,
                          (uint32_t)(q->desc_phys & 0xFFFFFFFF));
        vnet_cfg_write32(dev->common_cfg, VNET_COMMON_QDESCHI,
                          (uint32_t)(q->desc_phys >> 32));
        vnet_cfg_write32(dev->common_cfg, VNET_COMMON_QDRIVERLO,
                          (uint32_t)(q->avail_phys & 0xFFFFFFFF));
        vnet_cfg_write32(dev->common_cfg, VNET_COMMON_QDRIVERHI,
                          (uint32_t)(q->avail_phys >> 32));
        vnet_cfg_write32(dev->common_cfg, VNET_COMMON_QDEVICELO,
                          (uint32_t)(q->used_phys & 0xFFFFFFFF));
        vnet_cfg_write32(dev->common_cfg, VNET_COMMON_QDEVICEHI,
                          (uint32_t)(q->used_phys >> 32));
        /* Read notify offset */
        q->notify_off = vnet_cfg_read16(dev->common_cfg, VNET_COMMON_QNOTIFYOFF);
        vnet_cfg_write16(dev->common_cfg, VNET_COMMON_QENABLE, 1);
    }

    return HAL_OK;
}

/* Allocate a descriptor */
static uint16_t vnet_alloc_desc(vnet_queue_t *q)
{
    if (q->free_head >= q->size)
        return 0xFFFF;
    uint16_t idx = q->free_head;
    q->free_head = q->desc[idx].next;
    return idx;
}

/* Free a single descriptor back to free list */
static void vnet_free_desc(vnet_queue_t *q, uint16_t idx)
{
    q->desc[idx].next = q->free_head;
    q->desc[idx].flags = VNET_DESC_F_NEXT;
    q->free_head = idx;
}

/* Notify device */
static void vnet_notify_queue(virtio_net_dev_t *dev, vnet_queue_t *q,
                               uint16_t qidx)
{
    hal_mmio_barrier();
    if (dev->transport == VNET_TRANSPORT_MMIO) {
        vnet_cfg_write32(dev->common_cfg, VNET_MMIO_QUEUE_NOTIFY, qidx);
    } else {
        uint32_t off = (uint32_t)q->notify_off * dev->notify_off_mul;
        hal_mmio_write16((volatile void *)((uint8_t *)dev->notify_base + off), qidx);
    }
}

/* ── RX buffer population ── */

static hal_status_t vnet_populate_rx(virtio_net_dev_t *dev)
{
    vnet_queue_t *q = &dev->rxq;
    uint16_t count = 0;

    for (uint16_t i = 0; i < q->size && i < VNET_RX_QUEUE_SIZE; i++) {
        /* Allocate RX buffer (net header + frame data) */
        dev->rx_bufs[i] = hal_dma_alloc(VNET_RX_BUF_SIZE, &dev->rx_bufs_phys[i]);
        if (!dev->rx_bufs[i])
            break;
        memset(dev->rx_bufs[i], 0, VNET_RX_BUF_SIZE);

        uint16_t desc_idx = vnet_alloc_desc(q);
        if (desc_idx == 0xFFFF)
            break;

        /* Set up descriptor: device writes to this buffer */
        q->desc[desc_idx].addr = dev->rx_bufs_phys[i];
        q->desc[desc_idx].len = VNET_RX_BUF_SIZE;
        q->desc[desc_idx].flags = VNET_DESC_F_WRITE;
        q->desc[desc_idx].next = 0;

        /* Add to available ring */
        uint16_t avail_idx = q->avail->idx;
        q->avail->ring[avail_idx % q->size] = desc_idx;
        hal_mmio_barrier();
        q->avail->idx = avail_idx + 1;

        count++;
    }

    if (count > 0)
        vnet_notify_queue(dev, q, 0);

    return (count > 0) ? HAL_OK : HAL_NO_MEMORY;
}

/* ── PCI capability discovery ── */

static hal_status_t vnet_find_pci_caps(virtio_net_dev_t *dev, hal_device_t *hal_dev)
{
    uint32_t bdf = ((uint32_t)hal_dev->bus << 8) |
                   ((uint32_t)hal_dev->dev << 3) | hal_dev->func;

    uint32_t status_cmd = hal_bus_pci_read32(bdf, 0x04);
    if (!(status_cmd & (1u << 20)))
        return HAL_NOT_SUPPORTED;

    uint8_t cap_ptr = (uint8_t)(hal_bus_pci_read32(bdf, 0x34) & 0xFF);
    volatile void *bar_mapped[6] = {0};

    while (cap_ptr != 0) {
        uint32_t cap_hdr = hal_bus_pci_read32(bdf, cap_ptr);
        uint8_t cap_id = (uint8_t)(cap_hdr & 0xFF);
        uint8_t cap_next = (uint8_t)((cap_hdr >> 8) & 0xFF);

        if (cap_id == 0x09) {
            /* VirtIO PCI cap layout:
             * dword 0: cap_vndr(8) | cap_next(8) | cap_len(8) | cfg_type(8)
             * dword 1: bar(8) | id(8) | padding(16)
             * dword 2: offset(32) */
            uint8_t cfg_type = (uint8_t)((cap_hdr >> 24) & 0xFF);
            uint32_t dw1 = hal_bus_pci_read32(bdf, cap_ptr + 4);
            uint8_t bar_idx  = (uint8_t)(dw1 & 0xFF);
            uint32_t offset  = hal_bus_pci_read32(bdf, cap_ptr + 8);

            if (!bar_mapped[bar_idx] && bar_idx < HAL_BUS_MAX_BARS)
                bar_mapped[bar_idx] = hal_bus_map_bar(hal_dev, bar_idx);

            volatile void *base = (volatile void *)(
                (uint8_t *)bar_mapped[bar_idx] + offset);

            switch (cfg_type) {
            case VNET_PCI_CAP_COMMON:
                dev->common_cfg = base;
                break;
            case VNET_PCI_CAP_NOTIFY:
                dev->notify_base = base;
                dev->notify_off_mul = hal_bus_pci_read32(bdf, cap_ptr + 16);
                break;
            case VNET_PCI_CAP_DEVICE:
                dev->device_cfg = base;
                break;
            }
        }

        cap_ptr = cap_next;
    }

    return dev->common_cfg ? HAL_OK : HAL_ERROR;
}

/* ── Read MAC from device config ── */

static void vnet_read_mac(virtio_net_dev_t *dev)
{
    if (dev->device_cfg) {
        for (int i = 0; i < 6; i++) {
            dev->mac[i] = hal_mmio_read8(
                (volatile void *)((uint8_t *)dev->device_cfg + i));
        }
    } else if (dev->transport == VNET_TRANSPORT_MMIO) {
        /* MMIO: device config at offset 0x100 */
        volatile void *cfg = (volatile void *)((uint8_t *)dev->common_cfg + 0x100);
        for (int i = 0; i < 6; i++) {
            dev->mac[i] = hal_mmio_read8(
                (volatile void *)((uint8_t *)cfg + i));
        }
    }
}

/* ── Public API ── */

hal_status_t virtio_net_init(virtio_net_dev_t *dev, hal_device_t *hal_dev)
{
    dev->dev = *hal_dev;
    dev->transport = VNET_TRANSPORT_PCI;
    dev->initialized = false;
    dev->common_cfg = NULL;
    dev->notify_base = NULL;
    dev->device_cfg = NULL;

    hal_bus_pci_enable(hal_dev);

    hal_status_t st = vnet_find_pci_caps(dev, hal_dev);
    if (st != HAL_OK)
        return st;

    /* Reset */
    vnet_set_status(dev, 0);
    hal_timer_delay_us(1000);

    /* ACK + DRIVER */
    vnet_set_status(dev, VNET_STATUS_ACK);
    vnet_set_status(dev, VNET_STATUS_ACK | VNET_STATUS_DRIVER);

    /* Negotiate features: accept MAC, STATUS */
    vnet_cfg_write32(dev->common_cfg, VNET_COMMON_DFSELECT, 0);
    uint32_t dev_features = vnet_cfg_read32(dev->common_cfg, VNET_COMMON_DF);

    uint32_t drv_features = dev_features & (VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS);
    vnet_cfg_write32(dev->common_cfg, VNET_COMMON_GFSELECT, 0);
    vnet_cfg_write32(dev->common_cfg, VNET_COMMON_GF, drv_features);

    vnet_set_status(dev, VNET_STATUS_ACK | VNET_STATUS_DRIVER |
                         VNET_STATUS_FEATURES_OK);
    if (!(vnet_get_status(dev) & VNET_STATUS_FEATURES_OK))
        return HAL_ERROR;

    /* Read MAC */
    vnet_read_mac(dev);

    /* Set up RX queue (index 0) */
    st = vnet_setup_queue(dev, &dev->rxq, 0, VNET_RX_QUEUE_SIZE);
    if (st != HAL_OK)
        return st;

    /* Set up TX queue (index 1) */
    st = vnet_setup_queue(dev, &dev->txq, 1, VNET_TX_QUEUE_SIZE);
    if (st != HAL_OK)
        return st;

    /* Allocate TX header buffers (virtio_net_hdr per slot) */
    for (uint16_t i = 0; i < dev->txq.size && i < VNET_TX_QUEUE_SIZE; i++) {
        dev->tx_hdrs[i] = hal_dma_alloc(sizeof(virtio_net_hdr_t),
                                         &dev->tx_hdrs_phys[i]);
        if (!dev->tx_hdrs[i])
            return HAL_NO_MEMORY;
        memset(dev->tx_hdrs[i], 0, sizeof(virtio_net_hdr_t));
    }

    /* DRIVER_OK */
    vnet_set_status(dev, VNET_STATUS_ACK | VNET_STATUS_DRIVER |
                         VNET_STATUS_FEATURES_OK | VNET_STATUS_DRIVER_OK);

    /* Populate RX queue with buffers */
    st = vnet_populate_rx(dev);
    if (st != HAL_OK)
        return st;

    dev->initialized = true;
    return HAL_OK;
}

hal_status_t virtio_net_init_mmio(virtio_net_dev_t *dev, volatile void *base)
{
    dev->transport = VNET_TRANSPORT_MMIO;
    dev->initialized = false;
    dev->common_cfg = base;
    dev->notify_base = NULL;
    dev->device_cfg = NULL;

    /* Verify magic and device type */
    uint32_t magic = vnet_cfg_read32(base, VNET_MMIO_MAGIC);
    if (magic != 0x74726976)
        return HAL_NO_DEVICE;
    uint32_t device_id = vnet_cfg_read32(base, VNET_MMIO_DEVICE_ID);
    if (device_id != 1)  /* 1 = network device */
        return HAL_NO_DEVICE;

    /* Reset */
    vnet_set_status(dev, 0);
    hal_timer_delay_us(1000);

    vnet_set_status(dev, VNET_STATUS_ACK);
    vnet_set_status(dev, VNET_STATUS_ACK | VNET_STATUS_DRIVER);

    /* Features */
    vnet_cfg_write32(base, VNET_MMIO_DEV_FEAT_SEL, 0);
    uint32_t dev_features = vnet_cfg_read32(base, VNET_MMIO_DEV_FEAT);
    uint32_t drv_features = dev_features & (VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS);
    vnet_cfg_write32(base, VNET_MMIO_DRV_FEAT_SEL, 0);
    vnet_cfg_write32(base, VNET_MMIO_DRV_FEAT, drv_features);

    vnet_set_status(dev, VNET_STATUS_ACK | VNET_STATUS_DRIVER |
                         VNET_STATUS_FEATURES_OK);
    if (!(vnet_get_status(dev) & VNET_STATUS_FEATURES_OK))
        return HAL_ERROR;

    vnet_read_mac(dev);

    hal_status_t st = vnet_setup_queue(dev, &dev->rxq, 0, VNET_RX_QUEUE_SIZE);
    if (st != HAL_OK) return st;
    st = vnet_setup_queue(dev, &dev->txq, 1, VNET_TX_QUEUE_SIZE);
    if (st != HAL_OK) return st;

    for (uint16_t i = 0; i < dev->txq.size && i < VNET_TX_QUEUE_SIZE; i++) {
        dev->tx_hdrs[i] = hal_dma_alloc(sizeof(virtio_net_hdr_t),
                                         &dev->tx_hdrs_phys[i]);
        if (!dev->tx_hdrs[i]) return HAL_NO_MEMORY;
        memset(dev->tx_hdrs[i], 0, sizeof(virtio_net_hdr_t));
    }

    vnet_set_status(dev, VNET_STATUS_ACK | VNET_STATUS_DRIVER |
                         VNET_STATUS_FEATURES_OK | VNET_STATUS_DRIVER_OK);

    st = vnet_populate_rx(dev);
    if (st != HAL_OK) return st;

    dev->initialized = true;
    return HAL_OK;
}

hal_status_t virtio_net_send(virtio_net_dev_t *dev, const void *frame,
                             uint16_t length)
{
    if (!dev->initialized)
        return HAL_ERROR;
    if (length > VNET_RX_BUF_SIZE - sizeof(virtio_net_hdr_t))
        return HAL_ERROR;

    vnet_queue_t *q = &dev->txq;

    /* Need 2 descriptors: header + data */
    uint16_t d_hdr = vnet_alloc_desc(q);
    uint16_t d_data = vnet_alloc_desc(q);
    if (d_hdr == 0xFFFF || d_data == 0xFFFF) {
        if (d_hdr != 0xFFFF) vnet_free_desc(q, d_hdr);
        return HAL_BUSY;
    }

    /* Allocate a temporary DMA buffer for the frame data */
    uint64_t frame_phys;
    void *frame_buf = hal_dma_alloc(length, &frame_phys);
    if (!frame_buf) {
        vnet_free_desc(q, d_hdr);
        vnet_free_desc(q, d_data);
        return HAL_NO_MEMORY;
    }
    memcpy(frame_buf, frame, length);

    /* Prepare virtio_net_hdr (all zeros = no offload) */
    /* Use a pre-allocated header slot based on d_hdr index */
    uint16_t hdr_slot = d_hdr % VNET_TX_QUEUE_SIZE;
    memset(dev->tx_hdrs[hdr_slot], 0, sizeof(virtio_net_hdr_t));

    /* Descriptor 0: virtio_net_hdr (device reads) */
    q->desc[d_hdr].addr = dev->tx_hdrs_phys[hdr_slot];
    q->desc[d_hdr].len = sizeof(virtio_net_hdr_t);
    q->desc[d_hdr].flags = VNET_DESC_F_NEXT;
    q->desc[d_hdr].next = d_data;

    /* Descriptor 1: frame data (device reads) */
    q->desc[d_data].addr = frame_phys;
    q->desc[d_data].len = length;
    q->desc[d_data].flags = 0;
    q->desc[d_data].next = 0;

    /* Add to available ring */
    uint16_t avail_idx = q->avail->idx;
    q->avail->ring[avail_idx % q->size] = d_hdr;
    hal_mmio_barrier();
    q->avail->idx = avail_idx + 1;

    /* Notify device */
    vnet_notify_queue(dev, q, 1);

    /* Poll for completion */
    uint64_t deadline = hal_timer_ms() + VNET_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        hal_mmio_barrier();
        if (q->used->idx != q->last_used_idx) {
            q->last_used_idx++;
            break;
        }
        hal_timer_delay_us(VNET_POLL_US);
    }

    /* Free resources */
    vnet_free_desc(q, d_data);
    vnet_free_desc(q, d_hdr);
    hal_dma_free(frame_buf, length);

    return HAL_OK;
}

hal_status_t virtio_net_recv(virtio_net_dev_t *dev, void *buf, uint16_t *length)
{
    if (!dev->initialized)
        return HAL_ERROR;

    vnet_queue_t *q = &dev->rxq;

    /* Check for used entries */
    hal_mmio_barrier();
    if (q->used->idx == q->last_used_idx)
        return HAL_NO_DEVICE;

    /* Get used element */
    uint16_t used_idx = q->last_used_idx % q->size;
    uint32_t desc_idx = q->used->ring[used_idx].id;
    uint32_t total_len = q->used->ring[used_idx].len;
    q->last_used_idx++;

    /* The received data includes virtio_net_hdr at the beginning.
     * Copy everything after the header. */
    uint32_t hdr_size = sizeof(virtio_net_hdr_t);
    if (total_len <= hdr_size) {
        /* No payload — re-post buffer and return */
        *length = 0;
    } else {
        uint32_t payload_len = total_len - hdr_size;
        if (payload_len > VNET_RX_BUF_SIZE - hdr_size)
            payload_len = VNET_RX_BUF_SIZE - hdr_size;

        /* Find the RX buffer corresponding to this descriptor */
        uint16_t buf_idx = (uint16_t)(desc_idx % VNET_RX_QUEUE_SIZE);
        uint8_t *rx_data = (uint8_t *)dev->rx_bufs[buf_idx] + hdr_size;
        memcpy(buf, rx_data, payload_len);
        *length = (uint16_t)payload_len;
    }

    /* Re-post the buffer to the RX queue */
    uint16_t buf_idx = (uint16_t)(desc_idx % VNET_RX_QUEUE_SIZE);
    memset(dev->rx_bufs[buf_idx], 0, VNET_RX_BUF_SIZE);

    q->desc[desc_idx].addr = dev->rx_bufs_phys[buf_idx];
    q->desc[desc_idx].len = VNET_RX_BUF_SIZE;
    q->desc[desc_idx].flags = VNET_DESC_F_WRITE;
    q->desc[desc_idx].next = 0;

    uint16_t avail_idx = q->avail->idx;
    q->avail->ring[avail_idx % q->size] = (uint16_t)desc_idx;
    hal_mmio_barrier();
    q->avail->idx = avail_idx + 1;

    vnet_notify_queue(dev, q, 0);

    return HAL_OK;
}

void virtio_net_get_mac(virtio_net_dev_t *dev, uint8_t mac[6])
{
    for (int i = 0; i < 6; i++)
        mac[i] = dev->mac[i];
}

/* ── driver_ops_t wrapper for built-in driver registration ── */
#include "../../kernel/driver_loader.h"

static virtio_net_dev_t g_virtio_net;

static hal_status_t virtio_net_drv_init(hal_device_t *dev)
{
    return virtio_net_init(&g_virtio_net, dev);
}

static int64_t virtio_net_drv_tx(const void *frame, uint64_t len)
{
    return virtio_net_send(&g_virtio_net, frame, (uint16_t)len);
}

static int64_t virtio_net_drv_rx(void *frame, uint64_t max_len)
{
    uint16_t len = 0;
    hal_status_t rc = virtio_net_recv(&g_virtio_net, frame, &len);
    if (rc != HAL_OK) return -1;
    return (int64_t)len;
}

static void virtio_net_drv_get_mac(uint8_t mac[6])
{
    virtio_net_get_mac(&g_virtio_net, mac);
}

static const driver_ops_t virtio_net_driver_ops = {
    .name       = "virtio-net",
    .category   = DRIVER_CAT_NETWORK,
    .init       = virtio_net_drv_init,
    .shutdown   = NULL,
    .net_tx     = virtio_net_drv_tx,
    .net_rx     = virtio_net_drv_rx,
    .net_get_mac = virtio_net_drv_get_mac,
};

void virtio_net_register(void)
{
    driver_register_builtin(&virtio_net_driver_ops);
}
