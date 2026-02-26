/* SPDX-License-Identifier: MIT */
/* AlJefra OS — NVMe Storage Driver Implementation
 * Architecture-independent NVM Express controller driver.
 * Uses HAL for all MMIO, DMA, and bus operations.
 */

#include "nvme.h"
#include "../../lib/string.h"

/* ── Internal helpers ── */

/* Timeout in milliseconds for controller operations */
#define NVME_TIMEOUT_MS         5000
#define NVME_POLL_INTERVAL_US   100

/* Minimum of two values */
static inline uint32_t nvme_min(uint32_t a, uint32_t b) { return a < b ? a : b; }

/* Read a 32-bit controller register */
static inline uint32_t nvme_reg32(nvme_controller_t *ctrl, uint32_t off)
{
    return hal_mmio_read32((volatile void *)((uint8_t *)ctrl->regs + off));
}

/* Write a 32-bit controller register */
static inline void nvme_wreg32(nvme_controller_t *ctrl, uint32_t off, uint32_t val)
{
    hal_mmio_write32((volatile void *)((uint8_t *)ctrl->regs + off), val);
}

/* Read a 64-bit controller register (two 32-bit reads) */
static inline uint64_t nvme_reg64(nvme_controller_t *ctrl, uint32_t off)
{
    uint32_t lo = hal_mmio_read32((volatile void *)((uint8_t *)ctrl->regs + off));
    uint32_t hi = hal_mmio_read32((volatile void *)((uint8_t *)ctrl->regs + off + 4));
    return ((uint64_t)hi << 32) | lo;
}

/* Write a 64-bit controller register */
static inline void nvme_wreg64(nvme_controller_t *ctrl, uint32_t off, uint64_t val)
{
    hal_mmio_write32((volatile void *)((uint8_t *)ctrl->regs + off), (uint32_t)val);
    hal_mmio_write32((volatile void *)((uint8_t *)ctrl->regs + off + 4), (uint32_t)(val >> 32));
}

/* Get doorbell register address for a queue.
 * Submission doorbell for queue q = 0x1000 + (2*q) * db_stride
 * Completion doorbell for queue q = 0x1000 + (2*q+1) * db_stride
 */
static volatile uint32_t *nvme_sq_doorbell(nvme_controller_t *ctrl, uint16_t qid)
{
    uint32_t off = 0x1000 + (2 * qid) * ctrl->db_stride;
    return (volatile uint32_t *)((uint8_t *)ctrl->regs + off);
}

static volatile uint32_t *nvme_cq_doorbell(nvme_controller_t *ctrl, uint16_t qid)
{
    uint32_t off = 0x1000 + (2 * qid + 1) * ctrl->db_stride;
    return (volatile uint32_t *)((uint8_t *)ctrl->regs + off);
}

/* Wait for controller ready bit to match expected value */
static hal_status_t nvme_wait_ready(nvme_controller_t *ctrl, bool expected)
{
    uint64_t deadline = hal_timer_ms() + NVME_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t csts = nvme_reg32(ctrl, NVME_REG_CSTS);
        if (csts & NVME_CSTS_CFS)
            return HAL_ERROR;
        if (!!(csts & NVME_CSTS_RDY) == expected)
            return HAL_OK;
        hal_timer_delay_us(NVME_POLL_INTERVAL_US);
    }
    return HAL_TIMEOUT;
}

/* ── Queue management ── */

/* Allocate and initialize a queue pair */
static hal_status_t nvme_alloc_queue(nvme_controller_t *ctrl, nvme_queue_t *q,
                                     uint16_t qid, uint16_t depth)
{
    uint64_t sq_size = (uint64_t)depth * sizeof(nvme_sq_entry_t);
    uint64_t cq_size = (uint64_t)depth * sizeof(nvme_cq_entry_t);

    /* Allocate submission queue — must be physically contiguous */
    q->sq = (volatile nvme_sq_entry_t *)hal_dma_alloc(sq_size, &q->sq_phys);
    if (!q->sq)
        return HAL_NO_MEMORY;
    memset((void *)q->sq, 0, sq_size);

    /* Allocate completion queue */
    q->cq = (volatile nvme_cq_entry_t *)hal_dma_alloc(cq_size, &q->cq_phys);
    if (!q->cq) {
        hal_dma_free((void *)q->sq, sq_size);
        return HAL_NO_MEMORY;
    }
    memset((void *)q->cq, 0, cq_size);

    /* Set up doorbell pointers */
    q->sq_doorbell = nvme_sq_doorbell(ctrl, qid);
    q->cq_doorbell = nvme_cq_doorbell(ctrl, qid);

    q->sq_tail = 0;
    q->cq_head = 0;
    q->depth = depth;
    q->cq_phase = 1;    /* Phase starts at 1 (hardware toggles on wrap) */
    q->cid_counter = 0;

    return HAL_OK;
}

/* Submit a command to a queue and ring the doorbell */
static uint16_t nvme_submit_cmd(nvme_queue_t *q, nvme_sq_entry_t *cmd)
{
    uint16_t cid = q->cid_counter++;
    cmd->cid = cid;

    /* Copy command into SQ slot */
    volatile nvme_sq_entry_t *slot = &q->sq[q->sq_tail];
    const uint32_t *src = (const uint32_t *)cmd;
    volatile uint32_t *dst = (volatile uint32_t *)slot;
    for (int i = 0; i < 16; i++)
        dst[i] = src[i];

    /* Advance tail with wrap */
    q->sq_tail = (q->sq_tail + 1) % q->depth;

    /* Memory barrier before doorbell write */
    hal_mmio_barrier();

    /* Ring the doorbell */
    hal_mmio_write32(q->sq_doorbell, q->sq_tail);

    return cid;
}

/* Poll for completion of a specific command ID.
 * Returns the status field from the CQE. */
static hal_status_t nvme_poll_completion(nvme_queue_t *q, uint16_t cid,
                                         uint32_t *result, uint32_t timeout_ms)
{
    uint64_t deadline = hal_timer_ms() + timeout_ms;

    while (hal_timer_ms() < deadline) {
        volatile nvme_cq_entry_t *cqe = &q->cq[q->cq_head];

        /* Check phase bit — if it matches expected, this entry is new */
        uint16_t status_raw = hal_mmio_read16((volatile void *)&cqe->status);
        uint8_t phase = status_raw & 1;

        if (phase == q->cq_phase) {
            /* Valid completion entry */
            uint16_t comp_cid = hal_mmio_read16((volatile void *)&cqe->cid);
            uint16_t status_code = (status_raw >> 1) & 0x7FFF;

            if (result)
                *result = hal_mmio_read32((volatile void *)&cqe->cdw0);

            /* Advance CQ head */
            q->cq_head = (q->cq_head + 1) % q->depth;
            if (q->cq_head == 0)
                q->cq_phase ^= 1;  /* Toggle phase on wrap */

            /* Update CQ doorbell (head pointer) */
            hal_mmio_write32(q->cq_doorbell, q->cq_head);

            if (comp_cid == cid) {
                return (status_code == 0) ? HAL_OK : HAL_ERROR;
            }
            /* Not our CID — keep polling (shouldn't happen in single-threaded) */
            continue;
        }

        hal_timer_delay_us(NVME_POLL_INTERVAL_US);
    }

    return HAL_TIMEOUT;
}

/* Submit an admin command and wait for completion */
static hal_status_t nvme_admin_cmd(nvme_controller_t *ctrl, nvme_sq_entry_t *cmd,
                                    uint32_t *result)
{
    uint16_t cid = nvme_submit_cmd(&ctrl->admin_q, cmd);
    return nvme_poll_completion(&ctrl->admin_q, cid, result, NVME_TIMEOUT_MS);
}

/* Submit an I/O command and wait for completion */
static hal_status_t nvme_io_cmd(nvme_controller_t *ctrl, nvme_sq_entry_t *cmd,
                                 uint32_t *result)
{
    uint16_t cid = nvme_submit_cmd(&ctrl->io_q, cmd);
    return nvme_poll_completion(&ctrl->io_q, cid, result, NVME_TIMEOUT_MS);
}

/* ── Controller initialization ── */

/* Disable the controller (CC.EN = 0) and wait for CSTS.RDY = 0 */
static hal_status_t nvme_disable_controller(nvme_controller_t *ctrl)
{
    uint32_t cc = nvme_reg32(ctrl, NVME_REG_CC);
    cc &= ~NVME_CC_EN;
    nvme_wreg32(ctrl, NVME_REG_CC, cc);
    return nvme_wait_ready(ctrl, false);
}

/* Enable the controller (CC.EN = 1) and wait for CSTS.RDY = 1 */
static hal_status_t nvme_enable_controller(nvme_controller_t *ctrl)
{
    /* Configure CC: NVM command set, 4KB page size, 64B SQ entry, 16B CQ entry */
    uint32_t cc = 0;
    cc |= NVME_CC_EN;
    cc |= NVME_CC_CSS_NVM;
    cc |= (0u << NVME_CC_MPS_SHIFT);       /* MPS = 0 → 4KB pages */
    cc |= NVME_CC_AMS_RR;
    cc |= (6u << NVME_CC_IOSQES_SHIFT);    /* 2^6 = 64B SQ entries */
    cc |= (4u << NVME_CC_IOCQES_SHIFT);    /* 2^4 = 16B CQ entries */
    nvme_wreg32(ctrl, NVME_REG_CC, cc);
    return nvme_wait_ready(ctrl, true);
}

/* Issue Identify Controller command */
static hal_status_t nvme_identify_controller(nvme_controller_t *ctrl)
{
    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));

    cmd.opc = NVME_ADMIN_IDENTIFY;
    cmd.nsid = 0;
    cmd.prp1 = ctrl->identify_phys;
    cmd.prp2 = 0;
    cmd.cdw10 = NVME_IDENTIFY_CTRL;    /* CNS = 01h: Identify Controller */

    hal_status_t st = nvme_admin_cmd(ctrl, &cmd, NULL);
    if (st != HAL_OK)
        return st;

    /* Parse result */
    nvme_identify_ctrl_t *id = (nvme_identify_ctrl_t *)ctrl->identify_buf;
    ctrl->ns_count = id->nn;
    if (id->mdts > 0)
        ctrl->max_transfer = (1u << id->mdts) * 4096; /* MPS = 4KB */
    else
        ctrl->max_transfer = 1024 * 1024; /* Default 1MB if MDTS=0 (unlimited) */

    return HAL_OK;
}

/* Issue Identify Namespace command for namespace 1 */
static hal_status_t nvme_identify_namespace(nvme_controller_t *ctrl)
{
    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));

    cmd.opc = NVME_ADMIN_IDENTIFY;
    cmd.nsid = 1;
    cmd.prp1 = ctrl->identify_phys;
    cmd.prp2 = 0;
    cmd.cdw10 = NVME_IDENTIFY_NS;      /* CNS = 00h: Identify Namespace */

    hal_status_t st = nvme_admin_cmd(ctrl, &cmd, NULL);
    if (st != HAL_OK)
        return st;

    nvme_identify_ns_t *ns = (nvme_identify_ns_t *)ctrl->identify_buf;
    ctrl->ns1_blocks = ns->nsze;

    /* Get LBA data size from the active format */
    uint8_t fmt_idx = ns->flbas & 0x0F;
    ctrl->ns1_block_size = 1u << ns->lbaf[fmt_idx].lbads;

    return HAL_OK;
}

/* Create I/O Completion Queue (admin command 0x05) */
static hal_status_t nvme_create_io_cq(nvme_controller_t *ctrl, uint16_t qid,
                                       uint16_t depth, uint64_t cq_phys)
{
    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));

    cmd.opc = NVME_ADMIN_CREATE_IO_CQ;
    cmd.prp1 = cq_phys;
    cmd.cdw10 = ((uint32_t)(depth - 1) << 16) | qid;   /* Size (0-based) | CQID */
    cmd.cdw11 = (1u << 0);     /* Physically Contiguous */

    return nvme_admin_cmd(ctrl, &cmd, NULL);
}

/* Create I/O Submission Queue (admin command 0x01) */
static hal_status_t nvme_create_io_sq(nvme_controller_t *ctrl, uint16_t qid,
                                       uint16_t depth, uint64_t sq_phys,
                                       uint16_t cqid)
{
    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));

    cmd.opc = NVME_ADMIN_CREATE_IO_SQ;
    cmd.prp1 = sq_phys;
    cmd.cdw10 = ((uint32_t)(depth - 1) << 16) | qid;   /* Size (0-based) | SQID */
    cmd.cdw11 = ((uint32_t)cqid << 16) | (1u << 0);    /* CQID | Physically Contiguous */

    return nvme_admin_cmd(ctrl, &cmd, NULL);
}

/* ── Public API ── */

hal_status_t nvme_init(nvme_controller_t *ctrl, hal_device_t *dev)
{
    hal_status_t st;

    ctrl->dev = *dev;
    ctrl->initialized = false;

    /* Enable bus mastering and memory space */
    hal_bus_pci_enable(dev);

    /* Map BAR0 (MMIO registers) */
    ctrl->regs = hal_bus_map_bar(dev, 0);
    if (!ctrl->regs)
        return HAL_ERROR;

    /* Read capabilities */
    uint64_t cap = nvme_reg64(ctrl, NVME_REG_CAP);
    uint32_t version = nvme_reg32(ctrl, NVME_REG_VS);
    (void)version; /* informational */

    /* Calculate doorbell stride: 2^(2+DSTRD) bytes */
    uint8_t dstrd = NVME_CAP_DSTRD(cap);
    ctrl->db_stride = 4u << dstrd;   /* 4 * 2^DSTRD = 2^(2+DSTRD) */

    /* Determine queue depth — limited by MQES and our constants */
    uint16_t mqes = NVME_CAP_MQES(cap) + 1;  /* MQES is 0-based */
    uint16_t admin_depth = nvme_min(mqes, NVME_ADMIN_QUEUE_DEPTH);
    uint16_t io_depth = nvme_min(mqes, NVME_IO_QUEUE_DEPTH);

    /* Step 1: Disable controller */
    st = nvme_disable_controller(ctrl);
    if (st != HAL_OK)
        return st;

    /* Step 2: Allocate Admin Queue pair */
    st = nvme_alloc_queue(ctrl, &ctrl->admin_q, 0, admin_depth);
    if (st != HAL_OK)
        return st;

    /* Step 3: Configure Admin Queue Attributes and addresses */
    uint32_t aqa = ((uint32_t)(admin_depth - 1) << 16) | (admin_depth - 1);
    nvme_wreg32(ctrl, NVME_REG_AQA, aqa);
    nvme_wreg64(ctrl, NVME_REG_ASQ, ctrl->admin_q.sq_phys);
    nvme_wreg64(ctrl, NVME_REG_ACQ, ctrl->admin_q.cq_phys);

    /* Step 4: Enable controller */
    st = nvme_enable_controller(ctrl);
    if (st != HAL_OK)
        return st;

    /* Step 5: Allocate a 4KB DMA buffer for Identify commands */
    ctrl->identify_buf = hal_dma_alloc(4096, &ctrl->identify_phys);
    if (!ctrl->identify_buf)
        return HAL_NO_MEMORY;

    /* Step 6: Identify Controller */
    st = nvme_identify_controller(ctrl);
    if (st != HAL_OK)
        return st;

    /* Step 7: Identify Namespace 1 */
    st = nvme_identify_namespace(ctrl);
    if (st != HAL_OK)
        return st;

    /* Step 8: Create I/O Queue pair (CQID=1, SQID=1) */
    /* First allocate the queue memory */
    st = nvme_alloc_queue(ctrl, &ctrl->io_q, 1, io_depth);
    if (st != HAL_OK)
        return st;

    /* Create CQ first (SQ references CQ) */
    st = nvme_create_io_cq(ctrl, 1, io_depth, ctrl->io_q.cq_phys);
    if (st != HAL_OK)
        return st;

    /* Create SQ linked to CQ 1 */
    st = nvme_create_io_sq(ctrl, 1, io_depth, ctrl->io_q.sq_phys, 1);
    if (st != HAL_OK)
        return st;

    ctrl->initialized = true;

    return HAL_OK;
}

hal_status_t nvme_read(nvme_controller_t *ctrl, uint64_t lba,
                       uint32_t count, void *buf, uint64_t buf_phys)
{
    if (!ctrl->initialized)
        return HAL_ERROR;
    if (count == 0)
        return HAL_OK;

    /* NVMe read command — NLB is 0-based (0 = 1 block) */
    uint32_t max_blocks = ctrl->max_transfer / ctrl->ns1_block_size;
    if (max_blocks == 0) max_blocks = 1;

    uint64_t offset = 0;
    uint32_t remaining = count;

    while (remaining > 0) {
        uint32_t chunk = remaining;
        if (chunk > max_blocks) chunk = max_blocks;

        /* For transfers up to 2 pages (8KB with 4KB pages), PRP1+PRP2 suffice.
         * For larger transfers we would need a PRP list — handled below. */
        uint64_t transfer_bytes = (uint64_t)chunk * ctrl->ns1_block_size;
        uint64_t prp1 = buf_phys + offset;
        uint64_t prp2 = 0;

        if (transfer_bytes > 4096) {
            /* PRP2 points to second page if within 2 pages,
             * or to a PRP list for larger transfers.
             * For simplicity, limit single command to 2 pages if no PRP list. */
            if (transfer_bytes <= 8192) {
                prp2 = (prp1 + 4096) & ~0xFFFULL;
                /* Adjust if prp1 is not page-aligned */
                if (prp1 & 0xFFF)
                    prp2 = (prp1 & ~0xFFFULL) + 4096;
            } else {
                /* Limit chunk to fit in 2 PRPs (without PRP list) */
                chunk = 8192 / ctrl->ns1_block_size;
                if (chunk == 0) chunk = 1;
                transfer_bytes = (uint64_t)chunk * ctrl->ns1_block_size;
                if (transfer_bytes > 4096)
                    prp2 = (prp1 & ~0xFFFULL) + 4096;
            }
        }

        nvme_sq_entry_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.opc = NVME_CMD_READ;
        cmd.nsid = 1;
        cmd.prp1 = prp1;
        cmd.prp2 = prp2;
        cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
        cmd.cdw11 = (uint32_t)(lba >> 32);
        cmd.cdw12 = (chunk - 1);   /* NLB is 0-based */

        hal_status_t st = nvme_io_cmd(ctrl, &cmd, NULL);
        if (st != HAL_OK)
            return st;

        remaining -= chunk;
        lba += chunk;
        offset += transfer_bytes;
    }

    return HAL_OK;
}

hal_status_t nvme_write(nvme_controller_t *ctrl, uint64_t lba,
                        uint32_t count, const void *buf, uint64_t buf_phys)
{
    if (!ctrl->initialized)
        return HAL_ERROR;
    if (count == 0)
        return HAL_OK;

    uint32_t max_blocks = ctrl->max_transfer / ctrl->ns1_block_size;
    if (max_blocks == 0) max_blocks = 1;

    uint64_t offset = 0;
    uint32_t remaining = count;

    while (remaining > 0) {
        uint32_t chunk = remaining;
        if (chunk > max_blocks) chunk = max_blocks;

        uint64_t transfer_bytes = (uint64_t)chunk * ctrl->ns1_block_size;
        uint64_t prp1 = buf_phys + offset;
        uint64_t prp2 = 0;

        if (transfer_bytes > 4096) {
            if (transfer_bytes <= 8192) {
                prp2 = (prp1 + 4096) & ~0xFFFULL;
                if (prp1 & 0xFFF)
                    prp2 = (prp1 & ~0xFFFULL) + 4096;
            } else {
                chunk = 8192 / ctrl->ns1_block_size;
                if (chunk == 0) chunk = 1;
                transfer_bytes = (uint64_t)chunk * ctrl->ns1_block_size;
                if (transfer_bytes > 4096)
                    prp2 = (prp1 & ~0xFFFULL) + 4096;
            }
        }

        nvme_sq_entry_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.opc = NVME_CMD_WRITE;
        cmd.nsid = 1;
        cmd.prp1 = prp1;
        cmd.prp2 = prp2;
        cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
        cmd.cdw11 = (uint32_t)(lba >> 32);
        cmd.cdw12 = (chunk - 1);

        hal_status_t st = nvme_io_cmd(ctrl, &cmd, NULL);
        if (st != HAL_OK)
            return st;

        remaining -= chunk;
        lba += chunk;
        offset += transfer_bytes;
    }

    return HAL_OK;
}

hal_status_t nvme_get_ns_info(nvme_controller_t *ctrl,
                              uint64_t *total_blocks, uint32_t *block_size)
{
    if (!ctrl->initialized)
        return HAL_ERROR;
    if (total_blocks)
        *total_blocks = ctrl->ns1_blocks;
    if (block_size)
        *block_size = ctrl->ns1_block_size;
    return HAL_OK;
}

hal_status_t nvme_shutdown(nvme_controller_t *ctrl)
{
    if (!ctrl->initialized)
        return HAL_OK;

    /* Set shutdown notification in CC */
    uint32_t cc = nvme_reg32(ctrl, NVME_REG_CC);
    cc &= ~(3u << 14);             /* Clear SHN */
    cc |= NVME_CC_SHN_NORMAL;      /* Normal shutdown */
    nvme_wreg32(ctrl, NVME_REG_CC, cc);

    /* Wait for SHST = Complete */
    uint64_t deadline = hal_timer_ms() + NVME_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t csts = nvme_reg32(ctrl, NVME_REG_CSTS);
        if ((csts & NVME_CSTS_SHST_MASK) == NVME_CSTS_SHST_COMPLETE) {
            ctrl->initialized = false;
            return HAL_OK;
        }
        hal_timer_delay_us(NVME_POLL_INTERVAL_US);
    }

    ctrl->initialized = false;
    return HAL_TIMEOUT;
}

/* ── driver_ops_t wrapper for built-in driver registration ── */
#include "../../kernel/driver_loader.h"

static nvme_controller_t g_nvme_ctrl;

static hal_status_t nvme_drv_init(hal_device_t *dev)
{
    return nvme_init(&g_nvme_ctrl, dev);
}

static void nvme_drv_shutdown(void)
{
    nvme_shutdown(&g_nvme_ctrl);
}

static int64_t nvme_drv_read(void *buf, uint64_t lba, uint32_t count)
{
    /* Bare-metal identity mapping: virtual address == physical address */
    hal_status_t rc = nvme_read(&g_nvme_ctrl, lba, count,
                                 buf, (uint64_t)buf);
    return (rc == HAL_OK) ? (int64_t)count : -1;
}

static int64_t nvme_drv_write(const void *buf, uint64_t lba, uint32_t count)
{
    hal_status_t rc = nvme_write(&g_nvme_ctrl, lba, count,
                                  buf, (uint64_t)buf);
    return (rc == HAL_OK) ? (int64_t)count : -1;
}

static const driver_ops_t nvme_driver_ops = {
    .name       = "nvme",
    .category   = DRIVER_CAT_STORAGE,
    .init       = nvme_drv_init,
    .shutdown   = nvme_drv_shutdown,
    .read       = nvme_drv_read,
    .write      = nvme_drv_write,
};

void nvme_register(void)
{
    driver_register_builtin(&nvme_driver_ops);
}
