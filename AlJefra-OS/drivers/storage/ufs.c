/* SPDX-License-Identifier: MIT */
/* AlJefra OS — UFS (Universal Flash Storage) Driver Implementation
 * Architecture-independent UFSHCI driver.
 * Uses HAL for all MMIO, DMA, and bus operations.
 *
 * Initialization sequence:
 *   1. Host Controller Enable (HCE)
 *   2. UIC Link Startup (DME_LINK_STARTUP)
 *   3. NOP OUT to verify transport layer
 *   4. Set fDeviceInit flag (triggers device internal init)
 *   5. Read Device Descriptor
 *   6. Read LUN 0 capacity via SCSI READ CAPACITY
 *   7. Configure power mode (HS-G3 or HS-G4 if available)
 *
 * Data path:
 *   SCSI CDB -> UPIU -> UTRD -> UFSHCI doorbell -> completion poll
 */

#include "ufs.h"
#include "../../lib/string.h"

/* ── Internal helpers ── */

#define UFS_TIMEOUT_MS     5000
#define UFS_POLL_US        100
#define UFS_LINK_TIMEOUT   3000
#define UFS_MAX_DATA_BUF   65536  /* 64KB data buffer */

/* Byte-swap helpers for big-endian UFS protocol fields */
static inline uint16_t ufs_be16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

static inline uint32_t ufs_be32(uint32_t v)
{
    return ((v & 0xFF000000) >> 24) |
           ((v & 0x00FF0000) >> 8)  |
           ((v & 0x0000FF00) << 8)  |
           ((v & 0x000000FF) << 24);
}

/* MMIO register access */
static inline uint32_t ufs_reg32(ufs_controller_t *ctrl, uint32_t off)
{
    return hal_mmio_read32((volatile void *)((uint8_t *)ctrl->regs + off));
}

static inline void ufs_wreg32(ufs_controller_t *ctrl, uint32_t off, uint32_t val)
{
    hal_mmio_write32((volatile void *)((uint8_t *)ctrl->regs + off), val);
}

/* Get next task tag (wraps at 255) */
static uint8_t ufs_next_tag(ufs_controller_t *ctrl)
{
    uint8_t tag = ctrl->task_tag;
    ctrl->task_tag++;
    if (ctrl->task_tag == 0)
        ctrl->task_tag = 1;  /* Tag 0 is sometimes reserved */
    return tag;
}

/* ── Host Controller Enable / Disable ── */

static hal_status_t ufs_hce_enable(ufs_controller_t *ctrl)
{
    /* Write HCE = 1 */
    ufs_wreg32(ctrl, UFSHCI_HCE, UFSHCI_HCE_HCE);

    /* Wait for HCE to be confirmed */
    uint64_t deadline = hal_timer_ms() + UFS_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t hce = ufs_reg32(ctrl, UFSHCI_HCE);
        if (hce & UFSHCI_HCE_HCE)
            return HAL_OK;
        hal_timer_delay_us(UFS_POLL_US);
    }

    return HAL_TIMEOUT;
}

static hal_status_t ufs_hce_disable(ufs_controller_t *ctrl)
{
    /* Write HCE = 0 */
    ufs_wreg32(ctrl, UFSHCI_HCE, 0);

    /* Wait for HCE to clear */
    uint64_t deadline = hal_timer_ms() + UFS_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t hce = ufs_reg32(ctrl, UFSHCI_HCE);
        if (!(hce & UFSHCI_HCE_HCE))
            return HAL_OK;
        hal_timer_delay_us(UFS_POLL_US);
    }

    return HAL_TIMEOUT;
}

/* ── UIC Command Interface ── */

/* Wait for UIC command ready */
static hal_status_t ufs_wait_uic_ready(ufs_controller_t *ctrl)
{
    uint64_t deadline = hal_timer_ms() + UFS_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t hcs = ufs_reg32(ctrl, UFSHCI_HCS);
        if (hcs & UFSHCI_HCS_UCRDY)
            return HAL_OK;
        hal_timer_delay_us(UFS_POLL_US);
    }
    return HAL_TIMEOUT;
}

/* Issue a UIC command and wait for completion */
static hal_status_t ufs_uic_cmd(ufs_controller_t *ctrl, uint32_t cmd,
                                  uint32_t arg1, uint32_t arg2, uint32_t arg3,
                                  uint32_t *result)
{
    hal_status_t st;

    /* Wait for UIC ready */
    st = ufs_wait_uic_ready(ctrl);
    if (st != HAL_OK)
        return st;

    /* Clear UIC completion status */
    ufs_wreg32(ctrl, UFSHCI_IS, UFSHCI_IS_UCCS);

    /* Write arguments */
    ufs_wreg32(ctrl, UFSHCI_UCMDARG1, arg1);
    ufs_wreg32(ctrl, UFSHCI_UCMDARG2, arg2);
    ufs_wreg32(ctrl, UFSHCI_UCMDARG3, arg3);

    /* Memory barrier */
    hal_mmio_barrier();

    /* Issue command */
    ufs_wreg32(ctrl, UFSHCI_UICCMD, cmd);

    /* Wait for completion */
    uint64_t deadline = hal_timer_ms() + UFS_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t is = ufs_reg32(ctrl, UFSHCI_IS);
        if (is & UFSHCI_IS_UCCS) {
            /* Clear the status */
            ufs_wreg32(ctrl, UFSHCI_IS, UFSHCI_IS_UCCS);

            /* Read result from arg2 (GenSelectorIndex for DME_GET) */
            if (result)
                *result = ufs_reg32(ctrl, UFSHCI_UCMDARG3);

            /* Check for error: arg2 bit 8 = GenericErrorCode */
            uint32_t arg2_out = ufs_reg32(ctrl, UFSHCI_UCMDARG2);
            if (arg2_out & 0xFF)
                return HAL_ERROR;

            return HAL_OK;
        }

        /* Check for errors */
        if (is & UFSHCI_IS_UE) {
            ufs_wreg32(ctrl, UFSHCI_IS, UFSHCI_IS_UE);
            return HAL_ERROR;
        }

        hal_timer_delay_us(UFS_POLL_US);
    }

    return HAL_TIMEOUT;
}

/* DME GET local attribute */
static hal_status_t ufs_dme_get(ufs_controller_t *ctrl, uint16_t attr,
                                  uint16_t gen_sel_idx, uint32_t *value)
{
    uint32_t arg1 = ((uint32_t)attr << 16) | gen_sel_idx;
    return ufs_uic_cmd(ctrl, UIC_CMD_DME_GET, arg1, 0, 0, value);
}

/* DME SET local attribute */
static hal_status_t ufs_dme_set(ufs_controller_t *ctrl, uint16_t attr,
                                  uint16_t gen_sel_idx, uint32_t value)
{
    uint32_t arg1 = ((uint32_t)attr << 16) | gen_sel_idx;
    return ufs_uic_cmd(ctrl, UIC_CMD_DME_SET, arg1, 0, value, NULL);
}

/* ── Link Startup ── */

static hal_status_t ufs_link_startup(ufs_controller_t *ctrl)
{
    hal_console_puts("[ufs] Starting UIC link...\n");

    /* Clear link startup status */
    ufs_wreg32(ctrl, UFSHCI_IS, UFSHCI_IS_ULSS);

    /* Issue DME_LINK_STARTUP */
    hal_status_t st = ufs_uic_cmd(ctrl, UIC_CMD_DME_LINK_STARTUP,
                                    0, 0, 0, NULL);
    if (st != HAL_OK) {
        hal_console_puts("[ufs] Link startup command failed\n");
        return st;
    }

    /* Wait for link startup status or device present */
    uint64_t deadline = hal_timer_ms() + UFS_LINK_TIMEOUT;
    while (hal_timer_ms() < deadline) {
        uint32_t is = ufs_reg32(ctrl, UFSHCI_IS);
        if (is & UFSHCI_IS_ULSS) {
            ufs_wreg32(ctrl, UFSHCI_IS, UFSHCI_IS_ULSS);

            /* Check if device is present */
            uint32_t hcs = ufs_reg32(ctrl, UFSHCI_HCS);
            if (hcs & UFSHCI_HCS_DP) {
                hal_console_puts("[ufs] Link up, device present\n");
                return HAL_OK;
            }
        }
        hal_timer_delay_us(UFS_POLL_US);
    }

    /* Check once more */
    uint32_t hcs = ufs_reg32(ctrl, UFSHCI_HCS);
    if (hcs & UFSHCI_HCS_DP) {
        hal_console_puts("[ufs] Link up (late), device present\n");
        return HAL_OK;
    }

    hal_console_puts("[ufs] Link startup timed out\n");
    return HAL_TIMEOUT;
}

/* ── Transfer Request List Setup ── */

static hal_status_t ufs_setup_transfer_list(ufs_controller_t *ctrl)
{
    /* Allocate UTRD array (32 entries * 32 bytes = 1024 bytes) */
    uint64_t utrl_size = (uint64_t)ctrl->nutrs * sizeof(ufs_utrd_t);
    ctrl->utrl = (ufs_utrd_t *)hal_dma_alloc(utrl_size, &ctrl->utrl_phys);
    if (!ctrl->utrl)
        return HAL_NO_MEMORY;
    memset(ctrl->utrl, 0, utrl_size);

    /* Allocate UCD array (one per slot, 128-byte aligned) */
    uint64_t ucds_size = (uint64_t)ctrl->nutrs * sizeof(ufs_ucd_t);
    ctrl->ucds = (ufs_ucd_t *)hal_dma_alloc(ucds_size, &ctrl->ucds_phys);
    if (!ctrl->ucds)
        return HAL_NO_MEMORY;
    memset(ctrl->ucds, 0, ucds_size);

    /* Write UTRL base address */
    ufs_wreg32(ctrl, UFSHCI_UTRLBA, (uint32_t)(ctrl->utrl_phys & 0xFFFFFFFF));
    ufs_wreg32(ctrl, UFSHCI_UTRLBAU, (uint32_t)(ctrl->utrl_phys >> 32));

    /* Enable the transfer request list (run) */
    ufs_wreg32(ctrl, UFSHCI_UTRLRSR, 1);

    /* Wait for UTRL to be ready */
    uint64_t deadline = hal_timer_ms() + UFS_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t hcs = ufs_reg32(ctrl, UFSHCI_HCS);
        if (hcs & UFSHCI_HCS_UTRLRDY)
            return HAL_OK;
        hal_timer_delay_us(UFS_POLL_US);
    }

    return HAL_TIMEOUT;
}

/* ── Task Management List Setup ── */

static hal_status_t ufs_setup_tm_list(ufs_controller_t *ctrl)
{
    uint64_t utmrl_size = (uint64_t)ctrl->nutmrs * sizeof(ufs_utmrd_t);
    ctrl->utmrl = (ufs_utmrd_t *)hal_dma_alloc(utmrl_size, &ctrl->utmrl_phys);
    if (!ctrl->utmrl)
        return HAL_NO_MEMORY;
    memset(ctrl->utmrl, 0, utmrl_size);

    /* Write UTMRL base address */
    ufs_wreg32(ctrl, UFSHCI_UTMRLBA, (uint32_t)(ctrl->utmrl_phys & 0xFFFFFFFF));
    ufs_wreg32(ctrl, UFSHCI_UTMRLBAU, (uint32_t)(ctrl->utmrl_phys >> 32));

    /* Enable the task management request list */
    ufs_wreg32(ctrl, UFSHCI_UTMRLRSR, 1);

    /* Wait for UTMRL to be ready */
    uint64_t deadline = hal_timer_ms() + UFS_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t hcs = ufs_reg32(ctrl, UFSHCI_HCS);
        if (hcs & UFSHCI_HCS_UTMRLRDY)
            return HAL_OK;
        hal_timer_delay_us(UFS_POLL_US);
    }

    return HAL_TIMEOUT;
}

/* ── Build and Submit a SCSI Command ── */

/* Prepare a UTRD + UCD for a SCSI command, ring doorbell, poll completion.
 * slot: transfer request slot (0 to nutrs-1).
 * lun: logical unit number.
 * cdb: SCSI CDB (16 bytes).
 * data_dir: UTP_NO_DATA_TRANSFER, UTP_HOST_TO_DEVICE, UTP_DEVICE_TO_HOST.
 * data_phys: physical address of data buffer.
 * data_len: data buffer size.
 * Returns HAL_OK on success. */
static hal_status_t ufs_scsi_cmd(ufs_controller_t *ctrl, uint8_t slot,
                                   uint8_t lun, const uint8_t *cdb,
                                   uint32_t data_dir,
                                   uint64_t data_phys, uint32_t data_len)
{
    if (slot >= ctrl->nutrs)
        return HAL_ERROR;

    ufs_utrd_t *utrd = &ctrl->utrl[slot];
    ufs_ucd_t  *ucd  = &ctrl->ucds[slot];

    memset(utrd, 0, sizeof(*utrd));
    memset(ucd, 0, sizeof(*ucd));

    uint8_t tag = ufs_next_tag(ctrl);

    /* ── Build Command UPIU ── */
    upiu_cmd_t *cmd = &ucd->cmd_upiu;
    cmd->hdr.trans_type = UPIU_TRANS_COMMAND;
    cmd->hdr.flags = 0;

    /* Set data direction flag in UPIU header */
    if (data_dir == UTP_DEVICE_TO_HOST)
        cmd->hdr.flags = 0x40;  /* Read */
    else if (data_dir == UTP_HOST_TO_DEVICE)
        cmd->hdr.flags = 0x20;  /* Write */

    cmd->hdr.lun = lun;
    cmd->hdr.task_tag = tag;
    cmd->hdr.cmd_set_type = UPIU_COMMAND_SET_SCSI;
    cmd->exp_data_xfer_len = ufs_be32(data_len);

    /* Copy CDB */
    for (int i = 0; i < 16; i++)
        cmd->cdb[i] = cdb[i];

    /* ── Build UTRD ── */

    /* DW0: Command Type = SCSI (1), Data Direction, Interrupt */
    uint32_t dw0 = 0;
    dw0 |= (1u << 28);        /* CT = UTP_CMD_TYPE_SCSI */
    dw0 |= (data_dir << 25);  /* Data direction */
    dw0 |= (1u << 24);        /* Interrupt on completion */
    utrd->dw0 = dw0;

    /* OCS = invalid until completion */
    utrd->ocs = 0x0F;  /* OCS_INVALID_COMMAND (initial) */

    /* UCD base address */
    uint64_t ucd_phys = ctrl->ucds_phys + (uint64_t)slot * sizeof(ufs_ucd_t);
    utrd->ucdba  = (uint32_t)(ucd_phys & 0xFFFFFFFF);
    utrd->ucdbau = (uint32_t)(ucd_phys >> 32);

    /* Response UPIU offset (in DWORDs from UCD base): after cmd_upiu + padding = 64 bytes = 16 DWORDs */
    utrd->resp_upiu_offset = 16;
    utrd->resp_upiu_length = 8;  /* 32 bytes = 8 DWORDs */

    /* PRDT offset (in DWORDs from UCD base): after cmd(64) + rsp(64) = 128 bytes = 32 DWORDs */
    utrd->prdt_offset = 32;

    /* ── Build PRDT ── */
    uint16_t prdt_entries = 0;
    if (data_len > 0 && data_phys != 0) {
        /* Fill PRDT entries. Each entry can describe up to 256KB
         * (bit 17:0 of size field = byte count - 1, max 0x3FFFF = 256KB-1). */
        uint32_t remaining = data_len;
        uint64_t offset = 0;

        while (remaining > 0 && prdt_entries < UFS_MAX_PRDT_ENTRIES) {
            uint32_t chunk = remaining;
            if (chunk > 0x40000)  /* 256KB max per PRDT entry */
                chunk = 0x40000;

            ufs_prdt_entry_t *prdt = &ucd->prdt[prdt_entries];
            uint64_t addr = data_phys + offset;
            prdt->base_addr       = (uint32_t)(addr & 0xFFFFFFFF);
            prdt->base_addr_upper = (uint32_t)(addr >> 32);
            prdt->size = chunk - 1;  /* 0-based byte count */

            remaining -= chunk;
            offset += chunk;
            prdt_entries++;
        }
    }
    utrd->prdt_length = prdt_entries;

    /* Memory barrier before ringing doorbell */
    hal_mmio_barrier();

    /* Ring doorbell for this slot */
    ufs_wreg32(ctrl, UFSHCI_UTRLDBR, 1u << slot);

    /* ── Poll for completion ── */
    uint64_t deadline = hal_timer_ms() + UFS_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        /* Check if doorbell has been cleared (transfer complete) */
        uint32_t dbr = ufs_reg32(ctrl, UFSHCI_UTRLDBR);
        if (!(dbr & (1u << slot))) {
            /* Check OCS */
            uint32_t ocs = utrd->ocs & 0x0F;
            if (ocs != OCS_SUCCESS) {
                hal_console_printf("[ufs] SCSI cmd OCS error: 0x%x\n", ocs);
                return HAL_ERROR;
            }

            /* Check response UPIU status */
            upiu_response_t *rsp = &ucd->rsp_upiu;
            if (rsp->hdr.status != 0) {
                hal_console_printf("[ufs] SCSI status: 0x%02x\n", rsp->hdr.status);
                return HAL_ERROR;
            }

            return HAL_OK;
        }

        /* Check for fatal errors */
        uint32_t is = ufs_reg32(ctrl, UFSHCI_IS);
        if (is & (UFSHCI_IS_HCFES | UFSHCI_IS_SBFES | UFSHCI_IS_DFES)) {
            hal_console_printf("[ufs] Fatal error: IS=0x%08x\n", is);
            ufs_wreg32(ctrl, UFSHCI_IS, is);
            return HAL_ERROR;
        }

        hal_timer_delay_us(UFS_POLL_US);
    }

    hal_console_puts("[ufs] SCSI command timed out\n");
    return HAL_TIMEOUT;
}

/* ── Build and Submit a NOP OUT ── */

static hal_status_t ufs_send_nop_out(ufs_controller_t *ctrl)
{
    uint8_t slot = 0;
    ufs_utrd_t *utrd = &ctrl->utrl[slot];
    ufs_ucd_t  *ucd  = &ctrl->ucds[slot];

    memset(utrd, 0, sizeof(*utrd));
    memset(ucd, 0, sizeof(*ucd));

    uint8_t tag = ufs_next_tag(ctrl);

    /* Build NOP OUT UPIU */
    upiu_cmd_t *cmd = &ucd->cmd_upiu;
    cmd->hdr.trans_type = UPIU_TRANS_NOP_OUT;
    cmd->hdr.task_tag = tag;

    /* Build UTRD */
    utrd->dw0 = (2u << 28) | (1u << 24);  /* CT=NOP_OUT(2), Interrupt */
    utrd->ocs = 0x0F;

    uint64_t ucd_phys = ctrl->ucds_phys + (uint64_t)slot * sizeof(ufs_ucd_t);
    utrd->ucdba  = (uint32_t)(ucd_phys & 0xFFFFFFFF);
    utrd->ucdbau = (uint32_t)(ucd_phys >> 32);
    utrd->resp_upiu_offset = 16;
    utrd->resp_upiu_length = 8;
    utrd->prdt_offset = 32;
    utrd->prdt_length = 0;

    hal_mmio_barrier();
    ufs_wreg32(ctrl, UFSHCI_UTRLDBR, 1u << slot);

    /* Poll for completion */
    uint64_t deadline = hal_timer_ms() + UFS_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t dbr = ufs_reg32(ctrl, UFSHCI_UTRLDBR);
        if (!(dbr & (1u << slot))) {
            uint32_t ocs = utrd->ocs & 0x0F;
            if (ocs != OCS_SUCCESS) {
                hal_console_printf("[ufs] NOP OUT OCS error: 0x%x\n", ocs);
                return HAL_ERROR;
            }

            /* Verify NOP IN response */
            upiu_response_t *rsp = &ucd->rsp_upiu;
            if (rsp->hdr.trans_type != UPIU_TRANS_NOP_IN) {
                hal_console_printf("[ufs] Expected NOP_IN, got 0x%02x\n",
                                   rsp->hdr.trans_type);
                return HAL_ERROR;
            }

            return HAL_OK;
        }
        hal_timer_delay_us(UFS_POLL_US);
    }

    return HAL_TIMEOUT;
}

/* ── Query Request (Descriptor Read/Write, Flag, Attribute) ── */

static hal_status_t ufs_query_request(ufs_controller_t *ctrl,
                                        uint8_t opcode, uint8_t idn,
                                        uint8_t index, uint8_t selector,
                                        void *buf, uint16_t buf_len,
                                        uint32_t *attr_value)
{
    uint8_t slot = 0;
    ufs_utrd_t *utrd = &ctrl->utrl[slot];
    ufs_ucd_t  *ucd  = &ctrl->ucds[slot];

    memset(utrd, 0, sizeof(*utrd));
    memset(ucd, 0, sizeof(*ucd));

    uint8_t tag = ufs_next_tag(ctrl);

    /* Build Query Request UPIU.
     * We reuse the cmd_upiu space for the query request layout. */
    upiu_query_t *query = (upiu_query_t *)&ucd->cmd_upiu;
    query->hdr.trans_type = UPIU_TRANS_QUERY_REQ;
    query->hdr.task_tag = tag;
    query->hdr.query_func = 0x01;  /* Standard read request */
    query->opcode = opcode;
    query->idn = idn;
    query->index = index;
    query->selector = selector;

    /* Data direction depends on opcode */
    uint32_t data_dir = UTP_NO_DATA_TRANSFER;

    if (opcode == UPIU_QUERY_OP_READ_DESC) {
        query->length = ufs_be16(buf_len);
        data_dir = UTP_DEVICE_TO_HOST;
        query->hdr.data_segment_len = ufs_be16(buf_len);
    } else if (opcode == UPIU_QUERY_OP_WRITE_DESC) {
        query->length = ufs_be16(buf_len);
        data_dir = UTP_HOST_TO_DEVICE;
        query->hdr.data_segment_len = ufs_be16(buf_len);
    } else if (opcode == UPIU_QUERY_OP_READ_ATTR) {
        /* No data transfer, result in UPIU response */
    } else if (opcode == UPIU_QUERY_OP_WRITE_ATTR) {
        if (attr_value)
            query->value = ufs_be32(*attr_value);
    } else if (opcode == UPIU_QUERY_OP_SET_FLAG ||
               opcode == UPIU_QUERY_OP_CLEAR_FLAG ||
               opcode == UPIU_QUERY_OP_TOGGLE_FLAG) {
        /* No data transfer */
    } else if (opcode == UPIU_QUERY_OP_READ_FLAG) {
        /* Result in response */
    }

    /* Build UTRD */
    utrd->dw0 = (6u << 28) | (data_dir << 25) | (1u << 24);  /* CT=QUERY(6), data_dir, Interrupt */
    utrd->ocs = 0x0F;

    uint64_t ucd_phys = ctrl->ucds_phys + (uint64_t)slot * sizeof(ufs_ucd_t);
    utrd->ucdba  = (uint32_t)(ucd_phys & 0xFFFFFFFF);
    utrd->ucdbau = (uint32_t)(ucd_phys >> 32);
    utrd->resp_upiu_offset = 16;
    utrd->resp_upiu_length = 8;
    utrd->prdt_offset = 32;
    utrd->prdt_length = 0;  /* Query requests don't use PRDT */

    hal_mmio_barrier();
    ufs_wreg32(ctrl, UFSHCI_UTRLDBR, 1u << slot);

    /* Poll for completion */
    uint64_t deadline = hal_timer_ms() + UFS_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t dbr = ufs_reg32(ctrl, UFSHCI_UTRLDBR);
        if (!(dbr & (1u << slot))) {
            uint32_t ocs = utrd->ocs & 0x0F;
            if (ocs != OCS_SUCCESS) {
                hal_console_printf("[ufs] Query OCS error: 0x%x\n", ocs);
                return HAL_ERROR;
            }

            /* Parse response UPIU (also a query UPIU in the response area) */
            upiu_query_t *rsp = (upiu_query_t *)&ucd->rsp_upiu;

            /* Check query response code */
            if (rsp->hdr.response != 0) {
                hal_console_printf("[ufs] Query response error: 0x%02x\n",
                                   rsp->hdr.response);
                return HAL_ERROR;
            }

            /* Copy descriptor data if this was a read descriptor */
            if (opcode == UPIU_QUERY_OP_READ_DESC && buf && buf_len > 0) {
                /* Descriptor data follows the response UPIU header.
                 * It starts at offset 32 from the response UPIU base
                 * (after the 32-byte query response UPIU). */
                uint16_t resp_data_len = ufs_be16(rsp->length);
                if (resp_data_len > buf_len)
                    resp_data_len = buf_len;

                /* The data is in the response area after the header */
                uint8_t *data_ptr = (uint8_t *)rsp + 32;
                memcpy(buf, data_ptr, resp_data_len);
            }

            /* Read attribute value from response */
            if ((opcode == UPIU_QUERY_OP_READ_ATTR ||
                 opcode == UPIU_QUERY_OP_READ_FLAG) && attr_value) {
                *attr_value = ufs_be32(rsp->value);
            }

            return HAL_OK;
        }
        hal_timer_delay_us(UFS_POLL_US);
    }

    return HAL_TIMEOUT;
}

/* ── Device Initialization ── */

/* Set the fDeviceInit flag to trigger internal device initialization */
static hal_status_t ufs_device_init(ufs_controller_t *ctrl)
{
    hal_console_puts("[ufs] Setting fDeviceInit flag...\n");

    /* Set fDeviceInit flag */
    hal_status_t st = ufs_query_request(ctrl, UPIU_QUERY_OP_SET_FLAG,
                                          UFS_FLAG_DEVICE_INIT,
                                          0, 0, NULL, 0, NULL);
    if (st != HAL_OK) {
        hal_console_puts("[ufs] Failed to set fDeviceInit\n");
        return st;
    }

    /* Poll fDeviceInit until it clears (device initialization complete) */
    uint64_t deadline = hal_timer_ms() + 10000;  /* 10 second timeout */
    while (hal_timer_ms() < deadline) {
        uint32_t flag_val = 1;
        st = ufs_query_request(ctrl, UPIU_QUERY_OP_READ_FLAG,
                                UFS_FLAG_DEVICE_INIT,
                                0, 0, NULL, 0, &flag_val);
        if (st != HAL_OK)
            return st;

        if (flag_val == 0) {
            hal_console_puts("[ufs] Device initialization complete\n");
            return HAL_OK;
        }

        hal_timer_delay_ms(10);
    }

    hal_console_puts("[ufs] Device init timeout (proceeding anyway)\n");
    return HAL_OK;
}

/* Read the device descriptor */
static hal_status_t ufs_read_device_desc(ufs_controller_t *ctrl)
{
    hal_console_puts("[ufs] Reading device descriptor...\n");

    uint8_t buf[96];
    memset(buf, 0, sizeof(buf));

    hal_status_t st = ufs_query_request(ctrl, UPIU_QUERY_OP_READ_DESC,
                                          UFS_DESC_IDN_DEVICE,
                                          0, 0, buf, sizeof(buf), NULL);
    if (st != HAL_OK) {
        hal_console_puts("[ufs] Device descriptor read failed\n");
        return st;
    }

    /* Copy into controller state */
    memcpy(&ctrl->dev_desc, buf, sizeof(ctrl->dev_desc));
    ctrl->num_luns = ctrl->dev_desc.bNumberLU;
    ctrl->spec_version = ufs_be16(ctrl->dev_desc.wSpecVersion);

    hal_console_printf("[ufs] UFS spec version: %u.%u\n",
                       ctrl->spec_version >> 8,
                       (ctrl->spec_version >> 4) & 0xF);
    hal_console_printf("[ufs] Number of LUNs: %u\n", ctrl->num_luns);
    hal_console_printf("[ufs] Queue depth: %u\n", ctrl->dev_desc.bQueueDepth);

    return HAL_OK;
}

/* Read LUN 0 capacity using SCSI READ CAPACITY(10) */
static hal_status_t ufs_read_capacity(ufs_controller_t *ctrl)
{
    hal_console_puts("[ufs] Reading LUN 0 capacity...\n");

    /* READ CAPACITY(10) CDB */
    uint8_t cdb[16];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_OP_READ_CAPACITY_10;

    /* Response is 8 bytes: 4 bytes LBA + 4 bytes block size */
    hal_status_t st = ufs_scsi_cmd(ctrl, 0, 0, cdb,
                                     UTP_DEVICE_TO_HOST,
                                     ctrl->data_buf_phys, 8);
    if (st != HAL_OK) {
        hal_console_puts("[ufs] READ CAPACITY failed\n");
        return st;
    }

    uint8_t *data = (uint8_t *)ctrl->data_buf;

    /* Parse response (big-endian) */
    uint32_t last_lba = ((uint32_t)data[0] << 24) |
                         ((uint32_t)data[1] << 16) |
                         ((uint32_t)data[2] << 8)  |
                         (uint32_t)data[3];
    uint32_t block_size = ((uint32_t)data[4] << 24) |
                           ((uint32_t)data[5] << 16) |
                           ((uint32_t)data[6] << 8)  |
                           (uint32_t)data[7];

    if (last_lba == 0xFFFFFFFF) {
        /* Need READ CAPACITY(16) for >2TB */
        memset(cdb, 0, sizeof(cdb));
        cdb[0] = SCSI_OP_READ_CAPACITY_16;
        cdb[1] = 0x10;  /* Service action = READ CAPACITY(16) */
        cdb[13] = 32;   /* Allocation length */

        st = ufs_scsi_cmd(ctrl, 0, 0, cdb,
                           UTP_DEVICE_TO_HOST,
                           ctrl->data_buf_phys, 32);
        if (st != HAL_OK) {
            hal_console_puts("[ufs] READ CAPACITY(16) failed\n");
            return st;
        }

        data = (uint8_t *)ctrl->data_buf;
        uint64_t last_lba64 = ((uint64_t)data[0] << 56) |
                               ((uint64_t)data[1] << 48) |
                               ((uint64_t)data[2] << 40) |
                               ((uint64_t)data[3] << 32) |
                               ((uint64_t)data[4] << 24) |
                               ((uint64_t)data[5] << 16) |
                               ((uint64_t)data[6] << 8)  |
                               (uint64_t)data[7];
        block_size = ((uint32_t)data[8] << 24) |
                     ((uint32_t)data[9] << 16) |
                     ((uint32_t)data[10] << 8) |
                     (uint32_t)data[11];

        ctrl->lun0_blocks = last_lba64 + 1;
    } else {
        ctrl->lun0_blocks = (uint64_t)last_lba + 1;
    }

    ctrl->lun0_block_size = block_size;

    uint64_t size_mb = (ctrl->lun0_blocks * ctrl->lun0_block_size) / (1024 * 1024);
    hal_console_printf("[ufs] LUN 0: %u blocks x %u bytes = %u MB\n",
                       (uint32_t)ctrl->lun0_blocks,
                       ctrl->lun0_block_size,
                       (uint32_t)size_mb);

    return HAL_OK;
}

/* Test Unit Ready */
static hal_status_t ufs_test_unit_ready(ufs_controller_t *ctrl, uint8_t lun)
{
    uint8_t cdb[16];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_OP_TEST_UNIT_READY;

    return ufs_scsi_cmd(ctrl, 0, lun, cdb,
                         UTP_NO_DATA_TRANSFER, 0, 0);
}

/* ── Configure Power Mode (HS Gear) ── */

static hal_status_t ufs_configure_power_mode(ufs_controller_t *ctrl)
{
    hal_console_puts("[ufs] Configuring power mode...\n");

    /* Read available TX/RX lanes */
    uint32_t tx_lanes = 0, rx_lanes = 0;
    ufs_dme_get(ctrl, PA_AVAIL_TX_DATA_LANES, 0, &tx_lanes);
    ufs_dme_get(ctrl, PA_AVAIL_RX_DATA_LANES, 0, &rx_lanes);

    /* Read max RX HS gear */
    uint32_t max_rx_gear = 0;
    ufs_dme_get(ctrl, PA_MAX_RX_HS_GEAR, 0, &max_rx_gear);

    /* Use the highest available gear (up to HS-G4) */
    uint32_t gear = max_rx_gear;
    if (gear > 4) gear = 4;
    if (gear == 0) gear = 1;  /* Minimum HS-G1 */

    hal_console_printf("[ufs] Lanes: TX=%u RX=%u, Max HS gear: G%u\n",
                       tx_lanes, rx_lanes, gear);

    /* Configure TX */
    ufs_dme_set(ctrl, PA_TX_GEAR, 0, gear);
    ufs_dme_set(ctrl, PA_TX_TERMINATION, 0, 1);  /* Enable termination */
    ufs_dme_set(ctrl, PA_ACTIVE_TX_DATA_LANES, 0, tx_lanes);

    /* Configure RX */
    ufs_dme_set(ctrl, PA_RX_GEAR, 0, gear);
    ufs_dme_set(ctrl, PA_RX_TERMINATION, 0, 1);
    ufs_dme_set(ctrl, PA_ACTIVE_RX_DATA_LANES, 0, rx_lanes);

    /* Set HS Series A */
    ufs_dme_set(ctrl, PA_HS_SERIES, 0, 1);  /* Series A */

    /* Trigger power mode change (Fast Auto mode) */
    ufs_dme_set(ctrl, PA_PWR_MODE, 0, 0x11);  /* Fast mode TX+RX */

    /* Wait for power mode change status */
    uint64_t deadline = hal_timer_ms() + UFS_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t is = ufs_reg32(ctrl, UFSHCI_IS);
        if (is & UFSHCI_IS_UPMS) {
            ufs_wreg32(ctrl, UFSHCI_IS, UFSHCI_IS_UPMS);

            /* Check HCS for power mode change result */
            uint32_t hcs = ufs_reg32(ctrl, UFSHCI_HCS);
            uint32_t upmcrs = (hcs & UFSHCI_HCS_UPMCRS_MASK) >> 8;
            if (upmcrs == 0x01) {
                hal_console_printf("[ufs] Power mode: HS-G%u\n", gear);
                return HAL_OK;
            } else {
                hal_console_printf("[ufs] Power mode change failed: %u\n", upmcrs);
                return HAL_ERROR;
            }
        }
        hal_timer_delay_us(UFS_POLL_US);
    }

    hal_console_puts("[ufs] Power mode change timed out (non-fatal)\n");
    return HAL_OK;  /* Non-fatal: device works at whatever gear */
}

/* ── Public API ── */

hal_status_t ufs_init(ufs_controller_t *ctrl, hal_device_t *dev)
{
    hal_status_t st;

    ctrl->dev = *dev;
    ctrl->initialized = false;
    ctrl->task_tag = 1;

    /* Enable bus mastering + memory space */
    hal_bus_pci_enable(dev);

    /* Map BAR0 (MMIO registers) */
    ctrl->regs = hal_bus_map_bar(dev, 0);
    if (!ctrl->regs) {
        hal_console_puts("[ufs] Failed to map BAR0\n");
        return HAL_ERROR;
    }

    /* Read capabilities */
    uint32_t cap = ufs_reg32(ctrl, UFSHCI_CAP);
    ctrl->nutrs = (cap & UFSHCI_CAP_NUTRS_MASK) + 1;
    ctrl->nutmrs = ((cap & UFSHCI_CAP_NUTMRS_MASK) >> UFSHCI_CAP_NUTMRS_SHIFT) + 1;
    ctrl->addr_64bit = (cap & UFSHCI_CAP_64AS) != 0;

    /* Limit to our maximums */
    if (ctrl->nutrs > UFS_MAX_TRANSFER_SLOTS)
        ctrl->nutrs = UFS_MAX_TRANSFER_SLOTS;
    if (ctrl->nutmrs > UFS_MAX_TASK_MGMT_SLOTS)
        ctrl->nutmrs = UFS_MAX_TASK_MGMT_SLOTS;

    uint32_t ver = ufs_reg32(ctrl, UFSHCI_VER);
    hal_console_printf("[ufs] UFSHCI version: %u.%u.%u\n",
                       (ver >> 8) & 0xF, (ver >> 4) & 0xF, ver & 0xF);
    hal_console_printf("[ufs] Slots: UTRS=%u, UTMRS=%u, 64bit=%u\n",
                       ctrl->nutrs, ctrl->nutmrs, ctrl->addr_64bit);

    /* Step 1: Disable controller (clean state) */
    st = ufs_hce_disable(ctrl);
    /* Ignore error -- might already be disabled */

    /* Step 2: Enable controller */
    st = ufs_hce_enable(ctrl);
    if (st != HAL_OK) {
        hal_console_puts("[ufs] Host controller enable failed\n");
        return st;
    }

    /* Step 3: Setup transfer request list */
    st = ufs_setup_transfer_list(ctrl);
    if (st != HAL_OK) {
        hal_console_puts("[ufs] Transfer list setup failed\n");
        return st;
    }

    /* Step 4: Setup task management list */
    st = ufs_setup_tm_list(ctrl);
    if (st != HAL_OK) {
        hal_console_puts("[ufs] Task management list setup failed\n");
        return st;
    }

    /* Allocate query buffer (4KB) */
    ctrl->query_buf = hal_dma_alloc(4096, &ctrl->query_buf_phys);
    if (!ctrl->query_buf)
        return HAL_NO_MEMORY;

    /* Allocate data transfer buffer (64KB) */
    ctrl->data_buf = hal_dma_alloc(UFS_MAX_DATA_BUF, &ctrl->data_buf_phys);
    if (!ctrl->data_buf)
        return HAL_NO_MEMORY;

    /* Enable interrupts for completion status */
    ufs_wreg32(ctrl, UFSHCI_IE,
               UFSHCI_IS_UTRCS | UFSHCI_IS_UCCS | UFSHCI_IS_ULSS |
               UFSHCI_IS_UTMRCS | UFSHCI_IS_UPMS |
               UFSHCI_IS_HCFES | UFSHCI_IS_SBFES | UFSHCI_IS_DFES);

    /* Step 5: Link Startup */
    st = ufs_link_startup(ctrl);
    if (st != HAL_OK)
        return st;

    /* Step 6: NOP OUT to verify transport */
    st = ufs_send_nop_out(ctrl);
    if (st != HAL_OK) {
        hal_console_puts("[ufs] NOP OUT failed\n");
        return st;
    }
    hal_console_puts("[ufs] NOP OUT/IN verified\n");

    /* Step 7: Device initialization (set fDeviceInit flag) */
    st = ufs_device_init(ctrl);
    if (st != HAL_OK)
        return st;

    /* Step 8: Read device descriptor */
    st = ufs_read_device_desc(ctrl);
    if (st != HAL_OK)
        return st;

    /* Step 9: Test Unit Ready on LUN 0 */
    st = ufs_test_unit_ready(ctrl, 0);
    if (st != HAL_OK) {
        hal_console_puts("[ufs] Test Unit Ready failed (retrying...)\n");
        hal_timer_delay_ms(100);
        st = ufs_test_unit_ready(ctrl, 0);
        if (st != HAL_OK) {
            hal_console_puts("[ufs] LUN 0 not ready\n");
            return st;
        }
    }

    /* Step 10: Read LUN 0 capacity */
    st = ufs_read_capacity(ctrl);
    if (st != HAL_OK)
        return st;

    /* Step 11: Configure power mode (best-effort) */
    ufs_configure_power_mode(ctrl);

    ctrl->initialized = true;
    hal_console_puts("[ufs] UFS controller initialized\n");

    return HAL_OK;
}

hal_status_t ufs_read(ufs_controller_t *ctrl, uint64_t lba,
                       uint32_t count, void *buf, uint64_t buf_phys)
{
    if (!ctrl->initialized)
        return HAL_ERROR;
    if (count == 0)
        return HAL_OK;

    uint32_t block_size = ctrl->lun0_block_size;
    if (block_size == 0)
        block_size = UFS_BLOCK_SIZE;

    /* Maximum blocks per READ(10) is 65535 */
    uint32_t remaining = count;
    uint64_t offset = 0;
    uint64_t cur_lba = lba;

    while (remaining > 0) {
        uint32_t chunk = remaining;
        if (chunk > 65535)
            chunk = 65535;

        /* Limit by PRDT capacity: UFS_MAX_PRDT_ENTRIES * 256KB */
        uint64_t xfer_bytes = (uint64_t)chunk * block_size;
        uint64_t max_xfer = (uint64_t)UFS_MAX_PRDT_ENTRIES * 0x40000ULL;
        if (xfer_bytes > max_xfer) {
            chunk = (uint32_t)(max_xfer / block_size);
            xfer_bytes = (uint64_t)chunk * block_size;
        }

        /* Build SCSI READ(10) CDB */
        uint8_t cdb[16];
        memset(cdb, 0, sizeof(cdb));
        cdb[0] = SCSI_OP_READ_10;
        /* LBA (big-endian, bytes 2-5) */
        cdb[2] = (uint8_t)((cur_lba >> 24) & 0xFF);
        cdb[3] = (uint8_t)((cur_lba >> 16) & 0xFF);
        cdb[4] = (uint8_t)((cur_lba >> 8) & 0xFF);
        cdb[5] = (uint8_t)(cur_lba & 0xFF);
        /* Transfer length (big-endian, bytes 7-8) */
        cdb[7] = (uint8_t)((chunk >> 8) & 0xFF);
        cdb[8] = (uint8_t)(chunk & 0xFF);

        hal_status_t st = ufs_scsi_cmd(ctrl, 0, 0, cdb,
                                         UTP_DEVICE_TO_HOST,
                                         buf_phys + offset,
                                         (uint32_t)xfer_bytes);
        if (st != HAL_OK)
            return st;

        remaining -= chunk;
        cur_lba += chunk;
        offset += xfer_bytes;
    }

    return HAL_OK;
}

hal_status_t ufs_write(ufs_controller_t *ctrl, uint64_t lba,
                        uint32_t count, const void *buf, uint64_t buf_phys)
{
    if (!ctrl->initialized)
        return HAL_ERROR;
    if (count == 0)
        return HAL_OK;

    uint32_t block_size = ctrl->lun0_block_size;
    if (block_size == 0)
        block_size = UFS_BLOCK_SIZE;

    uint32_t remaining = count;
    uint64_t offset = 0;
    uint64_t cur_lba = lba;

    while (remaining > 0) {
        uint32_t chunk = remaining;
        if (chunk > 65535)
            chunk = 65535;

        uint64_t xfer_bytes = (uint64_t)chunk * block_size;
        uint64_t max_xfer = (uint64_t)UFS_MAX_PRDT_ENTRIES * 0x40000ULL;
        if (xfer_bytes > max_xfer) {
            chunk = (uint32_t)(max_xfer / block_size);
            xfer_bytes = (uint64_t)chunk * block_size;
        }

        /* Build SCSI WRITE(10) CDB */
        uint8_t cdb[16];
        memset(cdb, 0, sizeof(cdb));
        cdb[0] = SCSI_OP_WRITE_10;
        cdb[2] = (uint8_t)((cur_lba >> 24) & 0xFF);
        cdb[3] = (uint8_t)((cur_lba >> 16) & 0xFF);
        cdb[4] = (uint8_t)((cur_lba >> 8) & 0xFF);
        cdb[5] = (uint8_t)(cur_lba & 0xFF);
        cdb[7] = (uint8_t)((chunk >> 8) & 0xFF);
        cdb[8] = (uint8_t)(chunk & 0xFF);

        hal_status_t st = ufs_scsi_cmd(ctrl, 0, 0, cdb,
                                         UTP_HOST_TO_DEVICE,
                                         buf_phys + offset,
                                         (uint32_t)xfer_bytes);
        if (st != HAL_OK)
            return st;

        remaining -= chunk;
        cur_lba += chunk;
        offset += xfer_bytes;
    }

    return HAL_OK;
}

hal_status_t ufs_query_descriptor(ufs_controller_t *ctrl, uint8_t idn,
                                   uint8_t index, void *buf, uint16_t buf_len)
{
    if (!ctrl->initialized)
        return HAL_ERROR;
    return ufs_query_request(ctrl, UPIU_QUERY_OP_READ_DESC,
                              idn, index, 0, buf, buf_len, NULL);
}

hal_status_t ufs_read_attribute(ufs_controller_t *ctrl, uint8_t idn,
                                 uint32_t *value)
{
    if (!ctrl->initialized)
        return HAL_ERROR;
    return ufs_query_request(ctrl, UPIU_QUERY_OP_READ_ATTR,
                              idn, 0, 0, NULL, 0, value);
}

hal_status_t ufs_get_lun_info(ufs_controller_t *ctrl,
                               uint64_t *total_blocks, uint32_t *block_size)
{
    if (!ctrl->initialized)
        return HAL_ERROR;
    if (total_blocks)
        *total_blocks = ctrl->lun0_blocks;
    if (block_size)
        *block_size = ctrl->lun0_block_size;
    return HAL_OK;
}

hal_status_t ufs_shutdown(ufs_controller_t *ctrl)
{
    if (!ctrl->initialized)
        return HAL_OK;

    hal_console_puts("[ufs] Shutting down...\n");

    /* Sync cache on LUN 0 */
    uint8_t cdb[16];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_OP_SYNCHRONIZE_CACHE;
    ufs_scsi_cmd(ctrl, 0, 0, cdb, UTP_NO_DATA_TRANSFER, 0, 0);

    /* Stop transfer request list */
    ufs_wreg32(ctrl, UFSHCI_UTRLRSR, 0);

    /* Stop task management list */
    ufs_wreg32(ctrl, UFSHCI_UTMRLRSR, 0);

    /* Disable controller */
    ufs_hce_disable(ctrl);

    ctrl->initialized = false;
    hal_console_puts("[ufs] Shutdown complete\n");

    return HAL_OK;
}

/* ── driver_ops_t wrapper for driver_loader registration ── */

#include "../../kernel/driver_loader.h"

static ufs_controller_t g_ufs_ctrl;

static hal_status_t ufs_drv_init(hal_device_t *dev)
{
    return ufs_init(&g_ufs_ctrl, dev);
}

static void ufs_drv_shutdown(void)
{
    ufs_shutdown(&g_ufs_ctrl);
}

static int64_t ufs_drv_read(void *buf, uint64_t lba, uint32_t count)
{
    /* For the driver_ops_t interface, we use the internal data buffer
     * as a bounce buffer since the caller's buf may not be DMA-capable. */
    uint32_t block_size = g_ufs_ctrl.lun0_block_size;
    if (block_size == 0) block_size = UFS_BLOCK_SIZE;

    uint32_t remaining = count;
    uint64_t offset = 0;
    uint64_t cur_lba = lba;

    while (remaining > 0) {
        /* How many blocks fit in the 64KB data buffer? */
        uint32_t chunk = remaining;
        uint32_t max_blocks = UFS_MAX_DATA_BUF / block_size;
        if (max_blocks == 0) max_blocks = 1;
        if (chunk > max_blocks) chunk = max_blocks;

        uint64_t xfer_bytes = (uint64_t)chunk * block_size;

        hal_status_t st = ufs_read(&g_ufs_ctrl, cur_lba, chunk,
                                     g_ufs_ctrl.data_buf,
                                     g_ufs_ctrl.data_buf_phys);
        if (st != HAL_OK)
            return -1;

        /* Copy to caller's buffer */
        uint8_t *dst = (uint8_t *)buf + offset;
        memcpy(dst, g_ufs_ctrl.data_buf, xfer_bytes);

        remaining -= chunk;
        cur_lba += chunk;
        offset += xfer_bytes;
    }

    return (int64_t)count;
}

static int64_t ufs_drv_write(const void *buf, uint64_t lba, uint32_t count)
{
    uint32_t block_size = g_ufs_ctrl.lun0_block_size;
    if (block_size == 0) block_size = UFS_BLOCK_SIZE;

    uint32_t remaining = count;
    uint64_t offset = 0;
    uint64_t cur_lba = lba;

    while (remaining > 0) {
        uint32_t chunk = remaining;
        uint32_t max_blocks = UFS_MAX_DATA_BUF / block_size;
        if (max_blocks == 0) max_blocks = 1;
        if (chunk > max_blocks) chunk = max_blocks;

        uint64_t xfer_bytes = (uint64_t)chunk * block_size;

        /* Copy to DMA-capable bounce buffer */
        const uint8_t *src = (const uint8_t *)buf + offset;
        memcpy(g_ufs_ctrl.data_buf, src, xfer_bytes);

        hal_status_t st = ufs_write(&g_ufs_ctrl, cur_lba, chunk,
                                      g_ufs_ctrl.data_buf,
                                      g_ufs_ctrl.data_buf_phys);
        if (st != HAL_OK)
            return -1;

        remaining -= chunk;
        cur_lba += chunk;
        offset += xfer_bytes;
    }

    return (int64_t)count;
}

static const driver_ops_t ufs_driver_ops = {
    .name       = "ufs",
    .category   = DRIVER_CAT_STORAGE,
    .init       = ufs_drv_init,
    .shutdown   = ufs_drv_shutdown,
    .read       = ufs_drv_read,
    .write      = ufs_drv_write,
};

void ufs_register(void)
{
    driver_register_builtin(&ufs_driver_ops);
}
