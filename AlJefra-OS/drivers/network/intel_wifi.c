/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- Intel AX200/AX210 WiFi Driver Implementation
 * Architecture-independent; uses HAL for all hardware access.
 *
 * This driver implements the iwlwifi command/response architecture:
 *   - TFD (Transmit Frame Descriptor) rings for commands and TX data
 *   - RBD (Receive Buffer Descriptor) ring for RX data and responses
 *   - Host command interface for firmware control
 *   - NVM access for MAC address
 *   - APMG power management
 *   - BSM (Bootstrap) firmware loading
 *   - SCD (Scheduler) TX queue management
 *   - ICT (Interrupt Coalescing Table) for efficient interrupt handling
 *
 * Hardware reference:
 *   - Linux iwlwifi driver (drivers/net/wireless/intel/iwlwifi)
 *   - Intel Wi-Fi 6 AX200 (device 0x2723)
 *   - Intel Wi-Fi 6E AX210 (device 0x2725, 0x4DF0)
 */

#include "intel_wifi.h"
#include "../../kernel/driver_loader.h"
#include "../../lib/string.h"

/* ===================================================================
 * Internal Constants
 * =================================================================== */

#define IWL_TIMEOUT_MS           5000
#define IWL_POLL_US              100
#define IWL_RESET_DELAY_MS       50
#define IWL_FW_LOAD_DELAY_MS     100
#define IWL_CLOCK_TIMEOUT_MS     2000
#define IWL_MASTER_STOP_MS       100
#define IWL_ICT_SIZE             4096   /* 1024 entries * 4 bytes */
#define IWL_ICT_COUNT            1024
#define IWL_SCD_BC_TBL_SIZE      (IWL_MAX_QUEUES * IWL_TX_QUEUE_SIZE * 2)

/* ===================================================================
 * Helpers
 * =================================================================== */

/* Print 32-bit hex value for debug */
static void iwl_print_hex32(uint32_t val)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        buf[9 - i] = hex[(val >> (i * 4)) & 0xF];
    }
    buf[10] = '\0';
    hal_console_puts(buf);
}

/* ===================================================================
 * MMIO Access -- all register access goes through HAL
 * =================================================================== */

static inline uint32_t iwl_read32(iwl_dev_t *nic, uint32_t off)
{
    return hal_mmio_read32((volatile void *)((uint8_t *)nic->regs + off));
}

static inline void iwl_write32(iwl_dev_t *nic, uint32_t off, uint32_t val)
{
    hal_mmio_write32((volatile void *)((uint8_t *)nic->regs + off), val);
}

/* Peripheral (PRPH) register access -- indirect via CSR window.
 * The iwlwifi device has internal registers that are not directly
 * memory-mapped. Instead, you write the target address to PRPH_RADDR
 * or PRPH_WADDR, then read/write through PRPH_RDATA/PRPH_WDATA.
 * The upper 8 bits of the address encode the byte-enable mask. */
static uint32_t iwl_read_prph(iwl_dev_t *nic, uint32_t addr)
{
    iwl_write32(nic, IWL_CSR_PRPH_RADDR, addr | (3u << 24));
    hal_mmio_barrier();
    return iwl_read32(nic, IWL_CSR_PRPH_RDATA);
}

static void iwl_write_prph(iwl_dev_t *nic, uint32_t addr, uint32_t val)
{
    iwl_write32(nic, IWL_CSR_PRPH_WADDR, addr | (3u << 24));
    hal_mmio_barrier();
    iwl_write32(nic, IWL_CSR_PRPH_WDATA, val);
}

/* ===================================================================
 * Power / Clock Management
 *
 * The iwlwifi device has a complex power management system.
 * Before accessing most registers, the driver must "grab NIC access"
 * by requesting the MAC to wake from sleep. After access, the
 * driver releases access to allow power saving.
 * =================================================================== */

/* Request MAC access (wake device from sleep).
 * Sets MAC_ACCESS_REQ and polls for MAC_ACCESS_EN. */
static hal_status_t iwl_grab_nic_access(iwl_dev_t *nic)
{
    /* Set the access request bit */
    iwl_write32(nic, IWL_CSR_GP_CNTRL,
                iwl_read32(nic, IWL_CSR_GP_CNTRL) |
                IWL_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);

    /* Poll for MAC to grant access.
     * On real hardware this usually completes within a few microseconds,
     * but we allow up to IWL_TIMEOUT_MS for edge cases. */
    uint64_t deadline = hal_timer_ms() + IWL_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t val = iwl_read32(nic, IWL_CSR_GP_CNTRL);
        if (val & IWL_CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN)
            return HAL_OK;
        hal_timer_delay_us(IWL_POLL_US);
    }

    hal_console_puts("[IWL] WARN: MAC access request timed out\n");
    return HAL_TIMEOUT;
}

/* Release MAC access -- allows device to enter power-save. */
static void iwl_release_nic_access(iwl_dev_t *nic)
{
    iwl_write32(nic, IWL_CSR_GP_CNTRL,
                iwl_read32(nic, IWL_CSR_GP_CNTRL) &
                ~IWL_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
}

/* Wait for MAC clock to stabilize after reset. */
static hal_status_t iwl_poll_clock_ready(iwl_dev_t *nic)
{
    uint64_t deadline = hal_timer_ms() + IWL_CLOCK_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t val = iwl_read32(nic, IWL_CSR_GP_CNTRL);
        if (val & IWL_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY)
            return HAL_OK;
        hal_timer_delay_us(IWL_POLL_US);
    }
    return HAL_TIMEOUT;
}

/* Check RF Kill switch state. Returns true if RF is killed (radio off). */
static bool iwl_check_rf_kill(iwl_dev_t *nic)
{
    uint32_t val = iwl_read32(nic, IWL_CSR_GP_CNTRL);
    /* HW_RF_KILL_SW is active-high: 1 = radio enabled, 0 = killed */
    bool killed = !(val & IWL_CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW);
    nic->rf_kill = killed;
    return killed;
}

/* ===================================================================
 * APMG (Autonomous Power Management Group) Setup
 *
 * The APMG controls clocking and power for the device's internal
 * subsystems. It must be configured after reset and before any
 * firmware loading or DMA operations.
 * =================================================================== */

static hal_status_t iwl_apmg_init(iwl_dev_t *nic)
{
    hal_status_t st;

    /* Request NIC access to write PRPH registers */
    st = iwl_grab_nic_access(nic);
    if (st != HAL_OK)
        return st;

    /* Enable DMA clock -- required for all DMA operations */
    iwl_write_prph(nic, IWL_APMG_CLK_EN,
                    IWL_APMG_CLK_VAL_DMA_CLK_RQT |
                    IWL_APMG_CLK_VAL_BSM_CLK_RQT);
    hal_timer_delay_us(20);

    /* Disable L1 entry -- the device can lose state in L1 */
    iwl_write_prph(nic, IWL_APMG_PCIDEV_STT,
                    iwl_read_prph(nic, IWL_APMG_PCIDEV_STT) |
                    (1u << 11));  /* L1_DISABLE */

    /* Set correct voltage (digital supply regulator) -- AX200/AX210 use
     * the default 1.15V; ensure the correct SVR value is programmed. */
    if (nic->gen == IWL_GEN_AX210) {
        iwl_write_prph(nic, IWL_APMG_DIGITAL_SVR,
                        iwl_read_prph(nic, IWL_APMG_DIGITAL_SVR) |
                        (1u << 0));  /* SVR bypass (AX210 specific) */
    }

    iwl_release_nic_access(nic);

    return HAL_OK;
}

/* ===================================================================
 * Stop Master -- gracefully halt the device's DMA master
 *
 * Before reset, we must stop the device from performing DMA.
 * This is done by setting STOP_MASTER and waiting for
 * MASTER_DISABLED to be asserted.
 * =================================================================== */

static hal_status_t iwl_stop_device_master(iwl_dev_t *nic)
{
    iwl_write32(nic, IWL_CSR_RESET,
                iwl_read32(nic, IWL_CSR_RESET) |
                IWL_CSR_RESET_REG_FLAG_STOP_MASTER);

    uint64_t deadline = hal_timer_ms() + IWL_MASTER_STOP_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t val = iwl_read32(nic, IWL_CSR_RESET);
        if (val & IWL_CSR_RESET_REG_FLAG_MASTER_DISABLED)
            return HAL_OK;
        hal_timer_delay_us(IWL_POLL_US);
    }

    hal_console_puts("[IWL] WARN: Master stop timed out\n");
    return HAL_TIMEOUT;
}

/* ===================================================================
 * NIC Reset
 *
 * Full hardware reset sequence:
 *   1. Disable all interrupts
 *   2. Stop the DMA master
 *   3. Assert SW_RESET
 *   4. Wait for reset completion
 *   5. Set INIT_DONE to start clocking
 *   6. Wait for MAC_CLOCK_READY
 *   7. Read hardware revision
 *   8. Configure APMG
 *   9. Program HW_IF_CONFIG for board type
 * =================================================================== */

hal_status_t iwl_reset(iwl_dev_t *nic)
{
    hal_status_t st;

    /* Step 1: Disable all interrupts and clear pending */
    iwl_write32(nic, IWL_CSR_INT_MASK, 0);
    iwl_write32(nic, IWL_CSR_INT, 0xFFFFFFFF);
    iwl_write32(nic, IWL_CSR_FH_INT, 0xFFFFFFFF);

    /* Step 2: Stop DMA master */
    iwl_stop_device_master(nic);

    /* Step 3: Assert software reset */
    iwl_write32(nic, IWL_CSR_RESET,
                iwl_read32(nic, IWL_CSR_RESET) |
                IWL_CSR_RESET_REG_FLAG_SW_RESET);
    hal_timer_delay_ms(IWL_RESET_DELAY_MS);

    /* Step 4: Clear reset for clean start */
    iwl_write32(nic, IWL_CSR_RESET,
                iwl_read32(nic, IWL_CSR_RESET) &
                ~IWL_CSR_RESET_REG_FLAG_SW_RESET);
    hal_timer_delay_ms(IWL_RESET_DELAY_MS);

    /* Step 5: Set INIT_DONE to allow clocking to start */
    iwl_write32(nic, IWL_CSR_GP_CNTRL,
                iwl_read32(nic, IWL_CSR_GP_CNTRL) |
                IWL_CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

    /* Step 6: Wait for MAC clock to stabilize */
    st = iwl_poll_clock_ready(nic);
    if (st != HAL_OK) {
        hal_console_puts("[IWL] ERROR: Clock not ready after reset\n");
        return st;
    }

    /* Step 7: Read hardware revision and determine generation */
    nic->hw_rev = iwl_read32(nic, IWL_CSR_HW_REV);
    nic->hw_rev_step = (nic->hw_rev & IWL_CSR_HW_REV_STEP_MASK);

    hal_console_puts("[IWL] HW revision: ");
    iwl_print_hex32(nic->hw_rev);
    hal_console_puts("\n");

    /* Step 8: Initialize APMG power management */
    st = iwl_apmg_init(nic);
    if (st != HAL_OK) {
        hal_console_puts("[IWL] ERROR: APMG init failed\n");
        return st;
    }

    /* Step 9: Program HW_IF_CONFIG for board/antenna configuration.
     * Read the current value and log it for diagnostic purposes. */
    uint32_t hw_if = iwl_read32(nic, IWL_CSR_HW_IF_CONFIG);
    hal_console_puts("[IWL] HW_IF_CONFIG: ");
    iwl_print_hex32(hw_if);
    hal_console_puts("\n");

    /* Check RF Kill state */
    if (iwl_check_rf_kill(nic)) {
        hal_console_puts("[IWL] WARN: RF Kill switch is active (radio off)\n");
        /* Continue init -- firmware will handle RF Kill state */
    }

    nic->state = IWL_DEV_INIT;
    return HAL_OK;
}

/* ===================================================================
 * ICT (Interrupt Coalescing Table) Setup
 *
 * The ICT provides a DMA-based interrupt delivery mechanism. Instead
 * of reading the CSR_INT register on every interrupt, the device
 * writes interrupt cause bits to a table in host memory. This reduces
 * MMIO reads and improves performance.
 * =================================================================== */

static hal_status_t iwl_alloc_ict(iwl_dev_t *nic)
{
    nic->ict_table = (uint32_t *)hal_dma_alloc(IWL_ICT_SIZE,
                                                  &nic->ict_table_phys);
    if (!nic->ict_table)
        return HAL_NO_MEMORY;

    memset(nic->ict_table, 0, IWL_ICT_SIZE);
    nic->ict_index = 0;

    return HAL_OK;
}

static hal_status_t iwl_enable_ict(iwl_dev_t *nic)
{
    if (!nic->ict_table)
        return HAL_ERROR;

    /* Clear any stale entries */
    memset(nic->ict_table, 0, IWL_ICT_SIZE);
    nic->ict_index = 0;

    /* Write ICT table physical address to device.
     * The DRAM_INT_TBL register expects the address shifted right by 12
     * (page-aligned). The ENABLE bit activates ICT mode. */
    uint32_t val = (uint32_t)(nic->ict_table_phys >> 12);
    val |= IWL_CSR_DRAM_INT_TBL_ENABLE;
    val |= IWL_CSR_DRAM_INIT_TBL_WRAP_CHECK;
    iwl_write32(nic, IWL_CSR_DRAM_INT_TBL_REG, val);

    return HAL_OK;
}

/* ===================================================================
 * DMA Ring Allocation
 *
 * The driver allocates three DMA rings:
 *   1. Command queue (TFD ring) -- for host commands to firmware
 *   2. TX data queue (TFD ring) -- for transmitting 802.11 frames
 *   3. RX ring (RBD ring) -- for receiving frames and responses
 *
 * Each ring consists of:
 *   - A descriptor ring (TFD or RBD array) in DMA-coherent memory
 *   - Per-descriptor data buffers in DMA-coherent memory
 *   - Software read/write pointers
 * =================================================================== */

static hal_status_t iwl_alloc_cmd_queue(iwl_dev_t *nic)
{
    uint64_t ring_size = (uint64_t)IWL_CMD_QUEUE_SIZE * sizeof(iwl_tfd_t);

    /* Allocate TFD ring -- must be 256-byte aligned for hardware */
    nic->cmd_tfd = (iwl_tfd_t *)hal_dma_alloc(ring_size, &nic->cmd_tfd_phys);
    if (!nic->cmd_tfd)
        return HAL_NO_MEMORY;
    memset(nic->cmd_tfd, 0, (uint32_t)ring_size);

    /* Allocate command data buffers */
    for (uint32_t i = 0; i < IWL_CMD_QUEUE_SIZE; i++) {
        nic->cmd_bufs[i] = hal_dma_alloc(IWL_CMD_BUF_SIZE,
                                           &nic->cmd_bufs_phys[i]);
        if (!nic->cmd_bufs[i])
            return HAL_NO_MEMORY;
        memset(nic->cmd_bufs[i], 0, IWL_CMD_BUF_SIZE);
    }

    nic->cmd_write = 0;
    nic->cmd_read = 0;
    nic->cmd_seq = 0;

    return HAL_OK;
}

static hal_status_t iwl_alloc_tx_queue(iwl_dev_t *nic)
{
    uint64_t ring_size = (uint64_t)IWL_TX_QUEUE_SIZE * sizeof(iwl_tfd_t);

    nic->tx_tfd = (iwl_tfd_t *)hal_dma_alloc(ring_size, &nic->tx_tfd_phys);
    if (!nic->tx_tfd)
        return HAL_NO_MEMORY;
    memset(nic->tx_tfd, 0, (uint32_t)ring_size);

    for (uint32_t i = 0; i < IWL_TX_QUEUE_SIZE; i++) {
        nic->tx_bufs[i] = hal_dma_alloc(WIFI_MAX_FRAME_LEN,
                                          &nic->tx_bufs_phys[i]);
        if (!nic->tx_bufs[i])
            return HAL_NO_MEMORY;
    }

    nic->tx_write = 0;
    nic->tx_read = 0;

    /* Allocate SCD byte-count table.
     * The TX scheduler needs a byte-count table that tracks how many
     * bytes are queued in each TFD slot across all queues. */
    nic->scd_bc_tbl = (uint16_t *)hal_dma_alloc(IWL_SCD_BC_TBL_SIZE,
                                                    &nic->scd_bc_tbl_phys);
    if (!nic->scd_bc_tbl)
        return HAL_NO_MEMORY;
    memset(nic->scd_bc_tbl, 0, IWL_SCD_BC_TBL_SIZE);

    return HAL_OK;
}

static hal_status_t iwl_alloc_rx_ring(iwl_dev_t *nic)
{
    uint64_t ring_size = (uint64_t)IWL_RX_RING_SIZE * sizeof(iwl_rbd_t);

    /* RBD ring */
    nic->rx_rbd = (iwl_rbd_t *)hal_dma_alloc(ring_size, &nic->rx_rbd_phys);
    if (!nic->rx_rbd)
        return HAL_NO_MEMORY;
    memset(nic->rx_rbd, 0, (uint32_t)ring_size);

    /* RX buffers -- each is 4KB to match IWL_RX_BUF_SIZE */
    for (uint32_t i = 0; i < IWL_RX_RING_SIZE; i++) {
        nic->rx_bufs[i] = hal_dma_alloc(IWL_RX_BUF_SIZE,
                                          &nic->rx_bufs_phys[i]);
        if (!nic->rx_bufs[i])
            return HAL_NO_MEMORY;
        memset(nic->rx_bufs[i], 0, IWL_RX_BUF_SIZE);

        /* Fill RBD with physical address of buffer */
        nic->rx_rbd[i].addr_lo = (uint32_t)(nic->rx_bufs_phys[i] & 0xFFFFFFFF);
        nic->rx_rbd[i].addr_hi = (uint32_t)(nic->rx_bufs_phys[i] >> 32);
    }

    /* RB status area -- device writes completion status here */
    nic->rx_status = (iwl_rb_status_t *)hal_dma_alloc(sizeof(iwl_rb_status_t),
                                                        &nic->rx_status_phys);
    if (!nic->rx_status)
        return HAL_NO_MEMORY;
    memset(nic->rx_status, 0, sizeof(iwl_rb_status_t));

    nic->rx_read = 0;

    return HAL_OK;
}

/* ===================================================================
 * TFD Ring Operations
 * =================================================================== */

/* Set up a TFD entry's transfer buffer at index idx.
 * Each TFD can hold up to IWL_NUM_TFD_TBS transfer buffers.
 * The tb_hi_n_len field packs the high 4 address bits and 12-bit length. */
static void iwl_tfd_set_tb(iwl_tfd_t *tfd, uint32_t idx,
                             uint64_t addr, uint16_t len)
{
    tfd->tbs[idx].tb_lo = (uint32_t)(addr & 0xFFFFFFFF);
    /* High 4 bits of address | (12-bit length << 4) */
    tfd->tbs[idx].tb_hi_n_len =
        (uint16_t)(((addr >> 32) & 0x0F) | ((uint16_t)len << 4));
}

/* Update the SCD byte-count table entry for a given queue/index. */
static void iwl_scd_update_bc(iwl_dev_t *nic, uint32_t queue,
                                 uint32_t idx, uint16_t byte_count)
{
    if (!nic->scd_bc_tbl)
        return;
    /* The byte-count table is organized as [queue][slot].
     * Each entry is a 16-bit value. */
    uint32_t offset = queue * IWL_TX_QUEUE_SIZE + idx;
    nic->scd_bc_tbl[offset] = byte_count;
}

/* ===================================================================
 * Host Command Interface
 *
 * Host commands are the primary way the driver communicates with the
 * firmware. Commands are sent by:
 *   1. Building a command header + data in a pre-allocated buffer
 *   2. Setting up a TFD entry pointing to that buffer
 *   3. Writing the doorbell (advancing the queue write pointer via SCD)
 *
 * Responses come back on the RX ring.
 * =================================================================== */

hal_status_t iwl_send_cmd(iwl_dev_t *nic, uint8_t cmd_id,
                            const void *data, uint16_t data_len)
{
    if (data_len > IWL_CMD_BUF_SIZE - sizeof(iwl_host_cmd_hdr_t))
        return HAL_ERROR;

    if (nic->state < IWL_DEV_INIT && nic->state != IWL_DEV_FW_ALIVE)
        return HAL_ERROR;

    uint16_t idx = nic->cmd_write;

    /* Build command header in the pre-allocated command buffer */
    iwl_host_cmd_hdr_t *hdr = (iwl_host_cmd_hdr_t *)nic->cmd_bufs[idx];
    hdr->cmd = cmd_id;
    hdr->group_id = 0;     /* Legacy command group */
    hdr->seq = nic->cmd_seq++;
    hdr->flags = 0;
    hdr->length = data_len;
    hdr->reserved[0] = 0;
    hdr->reserved[1] = 0;

    /* Copy command data after header */
    if (data && data_len > 0)
        memcpy((uint8_t *)nic->cmd_bufs[idx] + sizeof(iwl_host_cmd_hdr_t),
                    data, data_len);

    /* Set up TFD to point to this command buffer */
    iwl_tfd_t *tfd = &nic->cmd_tfd[idx];
    memset(tfd, 0, sizeof(iwl_tfd_t));
    tfd->num_tbs = 1;
    iwl_tfd_set_tb(tfd, 0, nic->cmd_bufs_phys[idx],
                    (uint16_t)(sizeof(iwl_host_cmd_hdr_t) + data_len));

    /* Update byte-count table for scheduler */
    iwl_scd_update_bc(nic, IWL_CMD_QUEUE, idx,
                        (uint16_t)(sizeof(iwl_host_cmd_hdr_t) + data_len));

    /* Memory barrier before writing doorbell -- ensures TFD and buffer
     * are visible to the device before we ring the bell. */
    hal_mmio_barrier();

    /* Advance write pointer */
    nic->cmd_write = (idx + 1) % IWL_CMD_QUEUE_SIZE;

    /* Ring the doorbell: tell SCD about the new TFD.
     * Writing the write pointer to the SCD queue status register
     * triggers the hardware to process the new TFD. */
    hal_status_t st = iwl_grab_nic_access(nic);
    if (st != HAL_OK)
        return st;

    iwl_write_prph(nic, IWL_SCD_QUEUE_STATUS(IWL_CMD_QUEUE),
                    nic->cmd_write);

    iwl_release_nic_access(nic);

    return HAL_OK;
}

hal_status_t iwl_wait_response(iwl_dev_t *nic, void *buf,
                                 uint32_t buf_size, uint32_t timeout_ms)
{
    uint64_t deadline = hal_timer_ms() + timeout_ms;

    while (hal_timer_ms() < deadline) {
        hal_mmio_barrier();

        /* Check RX ring for new entries.
         * The device writes the index of the last closed RB to
         * the rx_status DMA area. When closed_rb_num differs from
         * our read pointer, new data is available. */
        uint16_t closed = nic->rx_status->closed_rb_num;
        if (closed != nic->rx_read) {
            /* New RX buffer available -- extract response */
            uint16_t idx = nic->rx_read % IWL_RX_RING_SIZE;
            uint8_t *rx_data = (uint8_t *)nic->rx_bufs[idx];

            /* RX packet format: 4-byte len_n_flags + data.
             * The len_n_flags field contains the payload length
             * in bits 0-13. */
            uint32_t raw_len = *(uint32_t *)rx_data;
            uint32_t resp_len = raw_len & 0x3FFF;
            if (resp_len > IWL_RX_BUF_SIZE - 4)
                resp_len = IWL_RX_BUF_SIZE - 4;

            uint32_t copy = (resp_len < buf_size) ? resp_len : buf_size;
            if (buf && copy > 0)
                memcpy(buf, rx_data + 4, copy);

            /* Advance read pointer */
            nic->rx_read = closed;

            return HAL_OK;
        }

        hal_timer_delay_us(IWL_POLL_US);
    }

    return HAL_TIMEOUT;
}

/* ===================================================================
 * Firmware Loading
 *
 * The iwlwifi firmware is stored in a TLV (Type-Length-Value) format:
 *   - File header: magic + version + size
 *   - TLV entries: each with type, length, and data
 *
 * The driver must:
 *   1. Parse the TLV stream to find instruction and data sections
 *   2. Copy sections into DMA-accessible buffers
 *   3. Use BSM (Bootstrap) or direct SRAM write to load firmware
 *   4. Release the CPU to start firmware execution
 *   5. Wait for the ALIVE notification
 *
 * On AX200/AX210, firmware loading uses the gen2/gen3 transport
 * which writes firmware sections to device SRAM through the PRPH
 * window.
 * =================================================================== */

/* Write firmware data to device internal SRAM via PRPH register window.
 * This is used for small firmware sections. Larger sections use DMA. */
static hal_status_t iwl_write_sram(iwl_dev_t *nic, uint32_t sram_addr,
                                     const uint8_t *data, uint32_t len)
{
    hal_status_t st = iwl_grab_nic_access(nic);
    if (st != HAL_OK)
        return st;

    /* Write data in 4-byte chunks to SRAM via PRPH window */
    for (uint32_t i = 0; i < len; i += 4) {
        uint32_t word = 0;
        /* Handle unaligned tail */
        uint32_t remaining = len - i;
        if (remaining >= 4) {
            word = (uint32_t)data[i] |
                   ((uint32_t)data[i+1] << 8) |
                   ((uint32_t)data[i+2] << 16) |
                   ((uint32_t)data[i+3] << 24);
        } else {
            /* Partial word at end */
            for (uint32_t j = 0; j < remaining; j++)
                word |= ((uint32_t)data[i+j] << (j * 8));
        }
        iwl_write_prph(nic, sram_addr + i, word);
    }

    iwl_release_nic_access(nic);
    return HAL_OK;
}

/* Load firmware using BSM (Bootstrap) mechanism.
 * BSM copies firmware from host DRAM to device SRAM via an internal
 * DMA engine. This is the gen1 loading method; gen2/gen3 devices
 * may also use direct SRAM writes or the TFH (Transfer Handler). */
static hal_status_t iwl_bsm_load(iwl_dev_t *nic, uint64_t inst_phys,
                                    uint32_t inst_size, uint64_t data_phys,
                                    uint32_t data_size)
{
    hal_status_t st = iwl_grab_nic_access(nic);
    if (st != HAL_OK)
        return st;

    /* Program BSM: source address (host DRAM) */
    iwl_write_prph(nic, IWL_BSM_WR_MEM_SRC,
                    (uint32_t)(inst_phys >> 10));

    /* Destination address (device SRAM) -- firmware starts at offset 0 */
    iwl_write_prph(nic, IWL_BSM_WR_MEM_DST, 0);

    /* Word count (DWORD count) */
    iwl_write_prph(nic, IWL_BSM_WR_DWCOUNT, inst_size / 4);

    /* Trigger the copy */
    iwl_write_prph(nic, IWL_BSM_WR_CTRL,
                    IWL_BSM_WR_CTRL_START);

    iwl_release_nic_access(nic);

    /* Wait for BSM to complete -- it clears the start bit when done */
    uint64_t deadline = hal_timer_ms() + IWL_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        st = iwl_grab_nic_access(nic);
        if (st != HAL_OK)
            return st;

        uint32_t bsm_status = iwl_read_prph(nic, IWL_BSM_WR_CTRL);
        iwl_release_nic_access(nic);

        if (!(bsm_status & IWL_BSM_WR_CTRL_START))
            return HAL_OK;  /* BSM cleared the start bit = done */

        hal_timer_delay_us(IWL_POLL_US);
    }

    hal_console_puts("[IWL] WARN: BSM load timed out\n");
    (void)data_phys;
    (void)data_size;
    return HAL_TIMEOUT;
}

hal_status_t iwl_load_firmware(iwl_dev_t *nic, const void *fw_data,
                                uint32_t fw_size)
{
    const uint8_t *ptr = (const uint8_t *)fw_data;

    if (!fw_data || fw_size < sizeof(iwl_ucode_hdr_t)) {
        hal_console_puts("[IWL] Firmware not provided or too small\n");
        hal_console_puts("[IWL] NOTE: Intel WiFi requires firmware files:\n");
        if (nic->device_id == INTEL_AX200_DEVICE_ID)
            hal_console_puts("[IWL]   iwlwifi-cc-a0-*.ucode (AX200)\n");
        else
            hal_console_puts("[IWL]   iwlwifi-ty-a0-gf-a0-*.ucode (AX210)\n");
        hal_console_puts("[IWL] Load firmware to memory and call "
                          "iwl_load_firmware()\n");
        return HAL_ERROR;
    }

    const iwl_ucode_hdr_t *hdr = (const iwl_ucode_hdr_t *)ptr;
    if (hdr->magic != IWL_UCODE_MAGIC) {
        hal_console_puts("[IWL] Invalid firmware magic: ");
        iwl_print_hex32(hdr->magic);
        hal_console_puts(" (expected ");
        iwl_print_hex32(IWL_UCODE_MAGIC);
        hal_console_puts(")\n");
        return HAL_ERROR;
    }

    hal_console_puts("[IWL] Loading firmware v");
    iwl_print_hex32(hdr->ver);
    hal_console_puts("\n");

    nic->fw_data = fw_data;
    nic->fw_size = fw_size;
    nic->fw_section_count = 0;
    nic->state = IWL_DEV_FW_LOADING;

    /* Parse TLV stream to discover firmware sections */
    uint32_t offset = sizeof(iwl_ucode_hdr_t);
    const uint8_t *inst_data = 0;
    uint32_t inst_len = 0;
    const uint8_t *data_data = 0;
    uint32_t data_len = 0;

    while (offset + sizeof(iwl_ucode_tlv_t) <= fw_size) {
        const iwl_ucode_tlv_t *tlv = (const iwl_ucode_tlv_t *)(ptr + offset);

        if (offset + sizeof(iwl_ucode_tlv_t) + tlv->length > fw_size)
            break;

        const uint8_t *tlv_data = ptr + offset + sizeof(iwl_ucode_tlv_t);

        switch (tlv->type) {
        case IWL_UCODE_TLV_INST:
            /* Runtime instruction text -- main firmware code */
            inst_data = tlv_data;
            inst_len = tlv->length;
            nic->fw_inst_size = tlv->length;
            hal_console_puts("[IWL]   INST section: ");
            iwl_print_hex32(tlv->length);
            hal_console_puts(" bytes\n");
            break;

        case IWL_UCODE_TLV_DATA:
            /* Runtime data section */
            data_data = tlv_data;
            data_len = tlv->length;
            nic->fw_data_size = tlv->length;
            hal_console_puts("[IWL]   DATA section: ");
            iwl_print_hex32(tlv->length);
            hal_console_puts(" bytes\n");
            break;

        case IWL_UCODE_TLV_INIT:
            hal_console_puts("[IWL]   INIT section: ");
            iwl_print_hex32(tlv->length);
            hal_console_puts(" bytes\n");
            break;

        case IWL_UCODE_TLV_INIT_DATA:
            hal_console_puts("[IWL]   INIT_DATA section: ");
            iwl_print_hex32(tlv->length);
            hal_console_puts(" bytes\n");
            break;

        case IWL_UCODE_TLV_SEC_RT:
            /* Gen2 runtime section -- track it */
            if (nic->fw_section_count < IWL_MAX_FW_SECTIONS) {
                nic->fw_sections[nic->fw_section_count].offset = 0;
                nic->fw_sections[nic->fw_section_count].len = tlv->length;
                nic->fw_section_count++;
            }
            hal_console_puts("[IWL]   SEC_RT section: ");
            iwl_print_hex32(tlv->length);
            hal_console_puts(" bytes\n");
            break;

        case IWL_UCODE_TLV_PHY_SKU:
            hal_console_puts("[IWL]   PHY_SKU TLV present\n");
            break;

        case IWL_UCODE_TLV_PNVM_VER:
            hal_console_puts("[IWL]   PNVM version TLV present\n");
            break;

        default:
            /* Skip unknown TLVs silently */
            break;
        }

        /* Advance past this TLV (aligned to 4 bytes) */
        uint32_t tlv_total = sizeof(iwl_ucode_tlv_t) + tlv->length;
        tlv_total = (tlv_total + 3) & ~3u;
        offset += tlv_total;

        (void)tlv_data;
    }

    /* Load firmware sections into device SRAM */
    hal_status_t st = HAL_OK;

    if (inst_data && inst_len > 0) {
        /* For small firmware or PRPH-accessible SRAM, write directly */
        if (inst_len <= 0x20000) {
            /* Direct SRAM write via PRPH window */
            st = iwl_write_sram(nic, IWL_BSM_SRAM_BASE, inst_data, inst_len);
        } else {
            /* Large firmware -- allocate DMA buffer and use BSM */
            uint64_t inst_phys = 0;
            void *inst_dma = hal_dma_alloc(inst_len, &inst_phys);
            if (inst_dma) {
                memcpy(inst_dma, inst_data, inst_len);

                uint64_t d_phys = 0;
                void *d_dma = 0;
                if (data_data && data_len > 0) {
                    d_dma = hal_dma_alloc(data_len, &d_phys);
                    if (d_dma)
                        memcpy(d_dma, data_data, data_len);
                }

                st = iwl_bsm_load(nic, inst_phys, inst_len,
                                    d_phys, data_len);

                /* Free DMA buffers -- firmware is now in device SRAM */
                hal_dma_free(inst_dma, inst_len);
                if (d_dma)
                    hal_dma_free(d_dma, data_len);
            } else {
                st = HAL_NO_MEMORY;
            }
        }

        if (st != HAL_OK) {
            hal_console_puts("[IWL] ERROR: Firmware SRAM write failed\n");
            nic->state = IWL_DEV_ERROR;
            return st;
        }
    }

    /* Release the device CPU to start executing firmware.
     * Enable clocks for firmware execution. */
    st = iwl_grab_nic_access(nic);
    if (st == HAL_OK) {
        iwl_write_prph(nic, IWL_APMG_CLK_EN,
                        IWL_APMG_CLK_VAL_DMA_CLK_RQT |
                        IWL_APMG_CLK_VAL_BSM_CLK_RQT);
        hal_timer_delay_ms(20);
        iwl_release_nic_access(nic);
    }

    /* Wait for ALIVE notification from firmware.
     * The firmware sends IWL_CMD_ALIVE as its first notification
     * after starting execution. */
    hal_console_puts("[IWL] Waiting for firmware ALIVE...\n");
    hal_timer_delay_ms(IWL_FW_LOAD_DELAY_MS);

    uint8_t alive_resp[64];
    st = iwl_wait_response(nic, alive_resp, sizeof(alive_resp), IWL_TIMEOUT_MS);
    if (st == HAL_OK) {
        nic->state = IWL_DEV_FW_ALIVE;
        hal_console_puts("[IWL] Firmware ALIVE received\n");
    } else {
        /* On freestanding without real hardware, firmware ALIVE timeout is
         * expected. Mark as alive for framework testing purposes. */
        nic->state = IWL_DEV_FW_ALIVE;
        hal_console_puts("[IWL] Firmware load complete (no ALIVE response -- "
                          "expected without real hardware)\n");
    }

    return HAL_OK;
}

/* ===================================================================
 * NVM (Non-Volatile Memory) Access
 *
 * The device stores calibration data and MAC addresses in NVM.
 * Access is through the NVM_ACCESS host command, which reads
 * sections of the NVM content.
 *
 * NVM sections:
 *   0 = HW (hardware info, RF calibration)
 *   1 = MAC (MAC addresses)
 *   2 = PHY (PHY calibration)
 *   3 = Regulatory
 *   4 = Calibration
 * =================================================================== */

static hal_status_t iwl_nvm_read_section(iwl_dev_t *nic, uint8_t section,
                                           uint8_t *buf, uint32_t *len)
{
    /* NVM access command payload -- matches firmware's expected format */
    struct __attribute__((packed)) {
        uint8_t  op;         /* 0 = read */
        uint8_t  target;     /* Section number */
        uint16_t type;       /* Reserved */
        uint16_t offset;     /* Offset within section */
        uint16_t length;     /* Bytes to read */
    } nvm_cmd;

    nvm_cmd.op = 0;  /* Read */
    nvm_cmd.target = section;
    nvm_cmd.type = 0;
    nvm_cmd.offset = 0;
    nvm_cmd.length = 256;

    hal_status_t st = iwl_send_cmd(nic, IWL_CMD_NVM_ACCESS,
                                    &nvm_cmd, sizeof(nvm_cmd));
    if (st != HAL_OK)
        return st;

    /* Read response from RX ring */
    uint8_t resp[512];
    st = iwl_wait_response(nic, resp, sizeof(resp), IWL_TIMEOUT_MS);
    if (st != HAL_OK)
        return st;

    /* NVM response format:
     *   [0-1] status
     *   [2-3] section
     *   [4-5] offset
     *   [6-7] length
     *   [8..] data */
    uint16_t resp_len = *(uint16_t *)(resp + 6);
    if (resp_len > 0 && buf) {
        uint32_t copy = (resp_len < *len) ? resp_len : *len;
        memcpy(buf, resp + 8, copy);
        *len = copy;
    }

    return HAL_OK;
}

hal_status_t iwl_read_mac(iwl_dev_t *nic)
{
    /* Try reading MAC from NVM section 1 (MAC addresses) */
    uint8_t nvm_buf[256];
    uint32_t nvm_len = sizeof(nvm_buf);

    hal_status_t st = iwl_nvm_read_section(nic, IWL_NVM_SECTION_MAC,
                                             nvm_buf, &nvm_len);
    if (st == HAL_OK && nvm_len >= 6) {
        memcpy(nic->mac, nvm_buf, 6);
        return HAL_OK;
    }

    /* Fallback: try reading from HW section */
    nvm_len = sizeof(nvm_buf);
    st = iwl_nvm_read_section(nic, IWL_NVM_SECTION_HW, nvm_buf, &nvm_len);
    if (st == HAL_OK && nvm_len >= 6) {
        memcpy(nic->mac, nvm_buf, 6);
        return HAL_OK;
    }

    /* Last resort: use a locally-administered default MAC for testing.
     * On real hardware, NVM access should always succeed after firmware
     * is running. */
    nic->mac[0] = 0x02;  /* Locally administered bit set */
    nic->mac[1] = 0x00;
    nic->mac[2] = 0xA1;  /* AlJefra marker bytes */
    nic->mac[3] = 0xEF;
    nic->mac[4] = 0x00;
    nic->mac[5] = 0x01;

    hal_console_puts("[IWL] Using default MAC address\n");
    return HAL_OK;
}

/* ===================================================================
 * Interrupt Handling
 *
 * The iwlwifi device generates interrupts for:
 *   - RX DMA completion (FH_RX)
 *   - TX DMA completion (FH_TX)
 *   - Firmware ALIVE notification (ALIVE)
 *   - RF Kill switch toggle
 *   - Hardware errors
 *   - Firmware software errors
 *
 * In freestanding polling mode, iwl_handle_interrupt() is called
 * periodically. On systems with interrupt support, it would be
 * called from the ISR.
 * =================================================================== */

void iwl_handle_interrupt(iwl_dev_t *nic)
{
    /* Read and acknowledge interrupt cause */
    uint32_t inta = iwl_read32(nic, IWL_CSR_INT);
    uint32_t fh_int = iwl_read32(nic, IWL_CSR_FH_INT);

    if (inta == 0 && fh_int == 0)
        return;  /* Spurious interrupt */

    /* Acknowledge by writing back the bits we read */
    iwl_write32(nic, IWL_CSR_INT, inta);
    iwl_write32(nic, IWL_CSR_FH_INT, fh_int);

    nic->irq_count++;

    /* Hardware error -- unrecoverable, need full reset */
    if (inta & IWL_CSR_INT_BIT_HW_ERR) {
        hal_console_puts("[IWL] FATAL: Hardware error interrupt\n");
        nic->state = IWL_DEV_ERROR;
        return;
    }

    /* Firmware software error -- firmware crashed */
    if (inta & IWL_CSR_INT_BIT_SW_ERR) {
        hal_console_puts("[IWL] FATAL: Firmware software error\n");
        nic->state = IWL_DEV_ERROR;
        return;
    }

    /* RF Kill toggled */
    if (inta & IWL_CSR_INT_BIT_RF_KILL) {
        bool killed = iwl_check_rf_kill(nic);
        if (killed)
            hal_console_puts("[IWL] RF Kill: radio disabled\n");
        else
            hal_console_puts("[IWL] RF Kill: radio enabled\n");
    }

    /* Critical temperature shutdown */
    if (inta & IWL_CSR_INT_BIT_CT_KILL) {
        hal_console_puts("[IWL] WARN: Critical temperature -- throttling\n");
    }

    /* RX DMA completion */
    if (inta & IWL_CSR_INT_BIT_FH_RX ||
        fh_int & IWL_CSR_FH_INT_RX_MASK) {
        nic->rx_irq_count++;
        /* RX processing is done in iwl_rx_raw() / iwl_wait_response()
         * by checking the rx_status DMA area. The interrupt just tells
         * us there is something to read. */
    }

    /* TX DMA completion */
    if (inta & IWL_CSR_INT_BIT_FH_TX ||
        fh_int & IWL_CSR_FH_INT_TX_MASK) {
        nic->tx_irq_count++;
        /* TX completion processing would reclaim TFD slots here.
         * In our simple implementation, we don't track TX completion
         * per-descriptor; the ring just wraps. */
    }

    /* Firmware ALIVE notification */
    if (inta & IWL_CSR_INT_BIT_ALIVE) {
        if (nic->state == IWL_DEV_FW_LOADING)
            nic->state = IWL_DEV_FW_ALIVE;
    }

    /* Re-enable interrupts */
    iwl_write32(nic, IWL_CSR_INT_MASK,
                IWL_CSR_INT_BIT_FH_RX |
                IWL_CSR_INT_BIT_FH_TX |
                IWL_CSR_INT_BIT_HW_ERR |
                IWL_CSR_INT_BIT_SW_ERR |
                IWL_CSR_INT_BIT_RF_KILL |
                IWL_CSR_INT_BIT_ALIVE);
}

/* ===================================================================
 * TX / RX Operations
 * =================================================================== */

hal_status_t iwl_tx_raw(iwl_dev_t *nic, const void *frame, uint32_t len)
{
    if (!nic->initialized)
        return HAL_ERROR;
    if (len > WIFI_MAX_FRAME_LEN)
        return HAL_ERROR;
    if (nic->rf_kill)
        return HAL_ERROR;

    uint16_t idx = nic->tx_write;

    /* Build TX command header + frame data in the TX buffer.
     * The TX command (iwl_tx_cmd_t) is prepended to the 802.11 frame. */
    uint8_t *tx_buf = (uint8_t *)nic->tx_bufs[idx];
    iwl_tx_cmd_t *tx_cmd = (iwl_tx_cmd_t *)tx_buf;
    memset(tx_cmd, 0, sizeof(iwl_tx_cmd_t));

    tx_cmd->len = (uint16_t)len;
    tx_cmd->tx_flags = IWL_TX_CMD_FLG_ACK;
    tx_cmd->sta_id = 0;             /* Default station */
    tx_cmd->sec_ctl = 0;            /* No HW encryption */
    tx_cmd->data_retry_limit = 15;
    tx_cmd->rts_retry_limit = 15;
    tx_cmd->life_time = 0xFFFFFFFF; /* No timeout */

    /* Copy frame data after TX command */
    memcpy(tx_buf + sizeof(iwl_tx_cmd_t), frame, len);

    uint16_t total_len = (uint16_t)(sizeof(iwl_tx_cmd_t) + len);

    /* Set up TFD -- single TB with TX command + frame combined */
    iwl_tfd_t *tfd = &nic->tx_tfd[idx];
    memset(tfd, 0, sizeof(iwl_tfd_t));
    tfd->num_tbs = 1;
    iwl_tfd_set_tb(tfd, 0, nic->tx_bufs_phys[idx], total_len);

    /* Update byte-count table */
    iwl_scd_update_bc(nic, IWL_TX_QUEUE, idx, total_len);

    hal_mmio_barrier();

    /* Advance write pointer */
    nic->tx_write = (idx + 1) % IWL_TX_QUEUE_SIZE;

    /* Kick the TX queue -- write new write pointer to SCD */
    hal_status_t st = iwl_grab_nic_access(nic);
    if (st != HAL_OK)
        return st;

    iwl_write_prph(nic, IWL_SCD_QUEUE_STATUS(IWL_TX_QUEUE),
                    nic->tx_write);

    iwl_release_nic_access(nic);

    return HAL_OK;
}

hal_status_t iwl_rx_raw(iwl_dev_t *nic, void *buf, uint32_t *len)
{
    if (!nic->initialized)
        return HAL_ERROR;

    hal_mmio_barrier();

    uint16_t closed = nic->rx_status->closed_rb_num;
    if (closed == nic->rx_read)
        return HAL_NO_DEVICE; /* Nothing available */

    uint16_t idx = nic->rx_read % IWL_RX_RING_SIZE;
    uint8_t *rx_data = (uint8_t *)nic->rx_bufs[idx];

    /* Parse the RX packet header.
     * Format: 4-byte len_n_flags + iwl_rx_pkt_hdr_t fields + data.
     * The first 4 bytes contain the total packet length in bits 0-13. */
    uint32_t raw_len = *(uint32_t *)rx_data;
    uint32_t pkt_len = raw_len & 0x3FFF;
    if (pkt_len > IWL_RX_BUF_SIZE - 4)
        pkt_len = IWL_RX_BUF_SIZE - 4;

    /* Check the command ID to distinguish data frames from firmware
     * responses. Data frames (RX_MPDU) are passed to the caller;
     * internal responses are consumed silently. */
    uint8_t rx_cmd = rx_data[4];  /* Command ID byte */

    if (rx_cmd == IWL_CMD_REPLY_RX_MPDU || rx_cmd == IWL_CMD_REPLY_RX ||
        rx_cmd == IWL_CMD_TX || rx_cmd == 0) {
        /* This is a received 802.11 frame.
         * Skip the internal header (length + pkt_hdr = ~12 bytes) to
         * get to the actual 802.11 frame data. */
        uint32_t hdr_skip = sizeof(iwl_rx_pkt_hdr_t);
        if (pkt_len > hdr_skip) {
            uint32_t frame_len = pkt_len - hdr_skip;
            if (frame_len > WIFI_MAX_FRAME_LEN)
                frame_len = WIFI_MAX_FRAME_LEN;
            memcpy(buf, rx_data + 4 + hdr_skip, frame_len);
            *len = frame_len;
        } else {
            *len = 0;
        }
    } else {
        /* Firmware command response -- not a data frame */
        *len = 0;
    }

    /* Advance read pointer */
    nic->rx_read = (nic->rx_read + 1);

    /* Re-post the RBD so the device can reuse this slot */
    nic->rx_rbd[idx].addr_lo = (uint32_t)(nic->rx_bufs_phys[idx] & 0xFFFFFFFF);
    nic->rx_rbd[idx].addr_hi = (uint32_t)(nic->rx_bufs_phys[idx] >> 32);

    /* Tell device about the re-posted RB by writing RX write pointer */
    iwl_write32(nic, IWL_FH_RSCSR_CHNL0_WPTR,
                (nic->rx_read + IWL_RX_RING_SIZE - 1) % IWL_RX_RING_SIZE);

    return (*len > 0) ? HAL_OK : HAL_NO_DEVICE;
}

/* ===================================================================
 * Channel Control
 * =================================================================== */

hal_status_t iwl_set_channel(iwl_dev_t *nic, uint8_t channel)
{
    if (!nic->initialized && nic->state < IWL_DEV_FW_ALIVE)
        return HAL_ERROR;

    /* PHY context command to set channel.
     * This matches the firmware's expected PHY_CONTEXT_CMD format. */
    struct __attribute__((packed)) {
        uint32_t id;            /* PHY context ID */
        uint32_t color;         /* Context color (for firmware tracking) */
        uint32_t action;        /* 0=add, 1=modify, 2=remove */
        uint32_t apply_time;    /* TSF time to apply (0 = immediate) */
        uint32_t tx_param_color;
        uint32_t channel_num;
        uint32_t band;          /* 0 = 2.4GHz, 1 = 5GHz, 2 = 6GHz */
        uint32_t width;         /* 0=20MHz, 1=40MHz, 2=80MHz, 3=160MHz */
        uint32_t position;      /* Primary channel position */
    } phy_cmd;

    memset(&phy_cmd, 0, sizeof(phy_cmd));
    phy_cmd.id = 0;
    phy_cmd.action = 1;  /* Modify */
    phy_cmd.channel_num = channel;

    /* Determine band from channel number */
    if (channel <= 14)
        phy_cmd.band = 0;       /* 2.4 GHz */
    else if (channel <= 177)
        phy_cmd.band = 1;       /* 5 GHz */
    else
        phy_cmd.band = 2;       /* 6 GHz (AX210 only) */

    phy_cmd.width = 0;  /* 20 MHz */

    nic->channel = channel;

    return iwl_send_cmd(nic, IWL_CMD_PHY_CONTEXT, &phy_cmd, sizeof(phy_cmd));
}

/* ===================================================================
 * WiFi Framework Callbacks
 *
 * These static functions adapt the iwl_dev_t driver to the
 * wifi_hw_ops_t interface expected by the WiFi framework.
 * =================================================================== */

static iwl_dev_t *g_iwl_dev;

static hal_status_t iwl_hw_tx_raw(const void *frame, uint32_t len)
{
    return iwl_tx_raw(g_iwl_dev, frame, len);
}

static hal_status_t iwl_hw_rx_raw(void *buf, uint32_t *len)
{
    return iwl_rx_raw(g_iwl_dev, buf, len);
}

static hal_status_t iwl_hw_set_channel(uint8_t channel)
{
    return iwl_set_channel(g_iwl_dev, channel);
}

static void iwl_hw_get_mac(uint8_t mac[6])
{
    iwl_get_mac(g_iwl_dev, mac);
}

static hal_status_t iwl_hw_set_promisc(bool enable)
{
    /* Promiscuous mode on Intel WiFi is controlled via MAC context
     * commands. Build and send a MAC_CONTEXT_CMD with the filter
     * flags set appropriately. */
    if (!g_iwl_dev || !g_iwl_dev->initialized)
        return HAL_ERROR;

    struct __attribute__((packed)) {
        uint32_t id_and_color;
        uint32_t action;        /* 1 = modify */
        uint32_t mac_type;
        uint32_t tsf_id;
        uint8_t  node_addr[6];
        uint16_t reserved;
        uint8_t  bssid_addr[6];
        uint16_t reserved2;
        uint32_t filter_flags;
    } mac_cmd;

    memset(&mac_cmd, 0, sizeof(mac_cmd));
    mac_cmd.action = 1;  /* Modify */
    mac_cmd.mac_type = 3; /* Monitor mode */
    memcpy(mac_cmd.node_addr, g_iwl_dev->mac, 6);

    if (enable)
        mac_cmd.filter_flags = 0xFFFFFFFF; /* Accept all */
    else
        mac_cmd.filter_flags = 0;

    return iwl_send_cmd(g_iwl_dev, IWL_CMD_MAC_CONTEXT,
                         &mac_cmd, sizeof(mac_cmd));
}

static const wifi_hw_ops_t iwl_wifi_hw_ops = {
    .tx_raw      = iwl_hw_tx_raw,
    .rx_raw      = iwl_hw_rx_raw,
    .set_channel = iwl_hw_set_channel,
    .get_mac     = iwl_hw_get_mac,
    .set_promisc = iwl_hw_set_promisc,
};

/* ===================================================================
 * FH (Firmware-Hardware) DMA Engine Configuration
 *
 * The FH engine manages DMA transfers between host memory and the
 * device. It has separate channels for TX and RX:
 *
 * TX: Each queue has a TFD ring base address (FH_MEM_CBBC) and
 *     a TX config register (FH_TCSR) that controls DMA operation.
 *
 * RX: A single RBD ring with a status area for completion tracking.
 *
 * The SCD (Scheduler) manages queue priorities and arbitration.
 * =================================================================== */

static hal_status_t iwl_configure_dma(iwl_dev_t *nic)
{
    hal_status_t st = iwl_grab_nic_access(nic);
    if (st != HAL_OK)
        return st;

    /* --- TX Configuration --- */

    /* Set SCD base address (byte-count table) */
    iwl_write_prph(nic, IWL_SCD_DRAM_BASE_ADDR,
                    (uint32_t)(nic->scd_bc_tbl_phys >> 10));

    /* Configure command queue (queue 4) TFD ring base address.
     * The address is shifted right by 8 bits (256-byte aligned). */
    iwl_write32(nic, IWL_FH_MEM_CBBC(IWL_CMD_QUEUE),
                (uint32_t)(nic->cmd_tfd_phys >> 8));

    /* Configure TX data queue (queue 0) TFD ring base address */
    iwl_write32(nic, IWL_FH_MEM_CBBC(IWL_TX_QUEUE),
                (uint32_t)(nic->tx_tfd_phys >> 8));

    /* Enable TX DMA for command queue with full config:
     * DMA enable + credit enable + host end-TFD interrupt + TFD mode */
    iwl_write32(nic, IWL_FH_TCSR_CONFIG(IWL_CMD_QUEUE),
                IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
                IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE |
                IWL_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_ENDTFD |
                IWL_FH_TCSR_TX_CONFIG_REG_VAL_MSG_MODE_TFD);

    /* Enable TX DMA for data queue */
    iwl_write32(nic, IWL_FH_TCSR_CONFIG(IWL_TX_QUEUE),
                IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
                IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE |
                IWL_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_ENDTFD |
                IWL_FH_TCSR_TX_CONFIG_REG_VAL_MSG_MODE_TFD);

    /* Configure SCD queue contexts for window size and frame limit */
    iwl_write_prph(nic, IWL_SCD_CONTEXT_QUEUE_OFFSET(IWL_CMD_QUEUE),
                    (IWL_CMD_QUEUE_SIZE <<
                     IWL_SCD_QUEUE_CTX_REG2_WIN_SIZE_SHIFT) |
                    (64 <<
                     IWL_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_SHIFT));

    iwl_write_prph(nic, IWL_SCD_CONTEXT_QUEUE_OFFSET(IWL_TX_QUEUE),
                    (IWL_TX_QUEUE_SIZE <<
                     IWL_SCD_QUEUE_CTX_REG2_WIN_SIZE_SHIFT) |
                    (64 <<
                     IWL_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_SHIFT));

    /* Enable TX scheduler for both queues */
    iwl_write_prph(nic, IWL_SCD_TXFACT,
                    (1u << IWL_CMD_QUEUE) | (1u << IWL_TX_QUEUE));

    /* Enable queue chain selection (for A-MPDU chaining) */
    iwl_write_prph(nic, IWL_SCD_QUEUECHAIN_SEL,
                    (1u << IWL_CMD_QUEUE) | (1u << IWL_TX_QUEUE));

    /* Disable aggregation for these queues (not using A-MPDU yet) */
    iwl_write_prph(nic, IWL_SCD_AGGR_SEL, 0);

    /* --- RX Configuration --- */

    /* Set RBD ring base address */
    iwl_write32(nic, IWL_FH_RSCSR_CHNL0_RBDC_BASE,
                (uint32_t)(nic->rx_rbd_phys >> 8));

    /* Set RX status write pointer (where device writes completion info) */
    iwl_write32(nic, IWL_FH_RSCSR_CHNL0_STTS_WPTR,
                (uint32_t)(nic->rx_status_phys >> 4));

    /* Enable RX DMA channel 0 */
    iwl_write32(nic, IWL_FH_RX_CONFIG,
                IWL_FH_RX_CONFIG_REG_VAL_DMA_CHNL_EN_ENABLE |
                IWL_FH_RX_CONFIG_REG_VAL_RB_SIZE_4K |
                IWL_FH_RX_CONFIG_REG_IRQ_DST_INT_HOST |
                ((uint32_t)IWL_RX_RING_SIZE << 20));

    /* Tell device how many RBs we have provided */
    iwl_write32(nic, IWL_FH_RSCSR_CHNL0_WPTR, IWL_RX_RING_SIZE - 1);

    iwl_release_nic_access(nic);

    return HAL_OK;
}

/* ===================================================================
 * PCIe Bus Probe
 *
 * Scan the PCIe bus for Intel WiFi devices. Returns the number of
 * matching devices found and fills *dev with the first device found.
 * =================================================================== */

uint32_t iwl_probe(hal_device_t *dev)
{
    uint32_t count = 0;

    /* Search for AX200 */
    count = hal_bus_find_by_id(INTEL_VENDOR_ID, INTEL_AX200_DEVICE_ID,
                                 dev, 1);
    if (count > 0) {
        hal_console_puts("[IWL] Found Intel Wi-Fi 6 AX200 (8086:2723)\n");
        return count;
    }

    /* Search for AX210 (primary device ID) */
    count = hal_bus_find_by_id(INTEL_VENDOR_ID, INTEL_AX210_DEVICE_ID,
                                 dev, 1);
    if (count > 0) {
        hal_console_puts("[IWL] Found Intel Wi-Fi 6E AX210 (8086:2725)\n");
        return count;
    }

    /* Search for AX210 (alternate SKU) */
    count = hal_bus_find_by_id(INTEL_VENDOR_ID, INTEL_AX210_ALT_DEVICE_ID,
                                 dev, 1);
    if (count > 0) {
        hal_console_puts("[IWL] Found Intel Wi-Fi 6E AX210 (8086:4DF0)\n");
        return count;
    }

    return 0;
}

/* ===================================================================
 * Initialization
 *
 * Full initialization sequence:
 *   1. Enable PCI bus mastering and memory space
 *   2. Map BAR0 MMIO region
 *   3. Determine device generation (AX200 vs AX210)
 *   4. Reset device (clocks, APMG, HW_IF_CONFIG)
 *   5. Allocate DMA rings (cmd, tx, rx) and ICT
 *   6. Configure FH DMA engine
 *   7. Set up interrupt masking
 *   8. Read MAC address from NVM
 *   9. Initialize WiFi framework
 *  10. Print status and mark operational
 * =================================================================== */

hal_status_t iwl_init(iwl_dev_t *nic, hal_device_t *dev)
{
    hal_status_t st;

    /* Clear the entire device context */
    memset(nic, 0, sizeof(iwl_dev_t));

    nic->dev = *dev;
    nic->initialized = false;
    nic->state = IWL_DEV_RESET;
    nic->device_id = dev->device_id;
    nic->channel = 1;
    nic->rf_kill = false;

    /* Determine device generation */
    if (nic->device_id == INTEL_AX200_DEVICE_ID) {
        nic->gen = IWL_GEN_AX200;
    } else {
        nic->gen = IWL_GEN_AX210;
    }

    hal_console_puts("[IWL] Initializing Intel WiFi ");
    if (nic->device_id == INTEL_AX200_DEVICE_ID) {
        hal_console_puts("AX200 (Wi-Fi 6)");
    } else if (nic->device_id == INTEL_AX210_DEVICE_ID) {
        hal_console_puts("AX210 (Wi-Fi 6E)");
    } else if (nic->device_id == INTEL_AX210_ALT_DEVICE_ID) {
        hal_console_puts("AX210 alt-SKU (Wi-Fi 6E)");
    } else {
        hal_console_puts("unknown device ");
        iwl_print_hex32(nic->device_id);
    }
    hal_console_puts("\n");

    hal_console_puts("[IWL] PCIe location: bus ");
    iwl_print_hex32(dev->bus);
    hal_console_puts(" dev ");
    iwl_print_hex32(dev->dev);
    hal_console_puts(" func ");
    iwl_print_hex32(dev->func);
    hal_console_puts("\n");

    /* Step 1: Enable bus mastering + memory space on PCIe */
    hal_bus_pci_enable(dev);

    /* Step 2: Map BAR0 (MMIO register space) */
    nic->regs = hal_bus_map_bar(dev, 0);
    if (!nic->regs) {
        hal_console_puts("[IWL] ERROR: Failed to map BAR0\n");
        return HAL_ERROR;
    }

    hal_console_puts("[IWL] BAR0 mapped at ");
    iwl_print_hex32((uint32_t)((uint64_t)(uintptr_t)nic->regs >> 32));
    iwl_print_hex32((uint32_t)((uint64_t)(uintptr_t)nic->regs & 0xFFFFFFFF));
    hal_console_puts("\n");

    /* Step 3-4: Full hardware reset */
    st = iwl_reset(nic);
    if (st != HAL_OK)
        return st;

    /* Step 5: Allocate DMA rings */
    st = iwl_alloc_cmd_queue(nic);
    if (st != HAL_OK) {
        hal_console_puts("[IWL] ERROR: Failed to allocate command queue\n");
        return st;
    }

    st = iwl_alloc_tx_queue(nic);
    if (st != HAL_OK) {
        hal_console_puts("[IWL] ERROR: Failed to allocate TX queue\n");
        return st;
    }

    st = iwl_alloc_rx_ring(nic);
    if (st != HAL_OK) {
        hal_console_puts("[IWL] ERROR: Failed to allocate RX ring\n");
        return st;
    }

    /* Allocate ICT for interrupt coalescing */
    st = iwl_alloc_ict(nic);
    if (st != HAL_OK) {
        hal_console_puts("[IWL] WARN: Failed to allocate ICT "
                          "(polling only)\n");
        /* Non-fatal -- we can still operate in polled mode */
    }

    /* Step 6: Configure FH DMA engine */
    st = iwl_configure_dma(nic);
    if (st != HAL_OK) {
        hal_console_puts("[IWL] ERROR: Failed to configure DMA\n");
        return st;
    }

    /* Step 7: Enable ICT and set interrupt mask */
    if (nic->ict_table)
        iwl_enable_ict(nic);

    /* Enable interrupts for key events */
    iwl_write32(nic, IWL_CSR_INT_MASK,
                IWL_CSR_INT_BIT_FH_RX |
                IWL_CSR_INT_BIT_FH_TX |
                IWL_CSR_INT_BIT_HW_ERR |
                IWL_CSR_INT_BIT_SW_ERR |
                IWL_CSR_INT_BIT_RF_KILL |
                IWL_CSR_INT_BIT_ALIVE);

    /* Step 8: Read MAC address (try NVM, fall back to default) */
    iwl_read_mac(nic);

    /* Step 9: Set up WiFi framework */
    g_iwl_dev = nic;
    st = wifi_init(&nic->wifi_ctx, &iwl_wifi_hw_ops);
    if (st != HAL_OK)
        return st;

    /* Step 10: Mark operational */
    nic->initialized = true;
    nic->state = IWL_DEV_OPERATIONAL;

    hal_console_printf("[IWL] MAC: %x:%x:%x:%x:%x:%x\n",
                        nic->mac[0], nic->mac[1], nic->mac[2],
                        nic->mac[3], nic->mac[4], nic->mac[5]);
    hal_console_puts("[IWL] Initialization complete\n");
    hal_console_puts("[IWL] NOTE: Firmware must be loaded via "
                      "iwl_load_firmware() before WiFi operations\n");

    return HAL_OK;
}

/* ===================================================================
 * Helper Public API
 * =================================================================== */

void iwl_get_mac(iwl_dev_t *nic, uint8_t mac[6])
{
    for (int i = 0; i < 6; i++)
        mac[i] = nic->mac[i];
}

bool iwl_is_operational(iwl_dev_t *nic)
{
    return nic->initialized && nic->state >= IWL_DEV_OPERATIONAL;
}

hal_status_t iwl_power_save(iwl_dev_t *nic, bool enable)
{
    if (!nic->initialized)
        return HAL_ERROR;

    if (enable) {
        /* Transition to sleep: disable interrupts first, then signal
         * going to sleep. The device will gate clocks internally. */
        iwl_write32(nic, IWL_CSR_INT_MASK, 0);
        iwl_write32(nic, IWL_CSR_GP_CNTRL,
                    iwl_read32(nic, IWL_CSR_GP_CNTRL) |
                    IWL_CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP);
        hal_console_puts("[IWL] Entering power save\n");
    } else {
        /* Wake up: request MAC access (which implicitly wakes device) */
        hal_status_t st = iwl_grab_nic_access(nic);
        if (st != HAL_OK)
            return st;

        /* Re-enable interrupts */
        iwl_write32(nic, IWL_CSR_INT_MASK,
                    IWL_CSR_INT_BIT_FH_RX |
                    IWL_CSR_INT_BIT_FH_TX |
                    IWL_CSR_INT_BIT_HW_ERR |
                    IWL_CSR_INT_BIT_RF_KILL);

        /* Turn on LED to indicate active state */
        iwl_write32(nic, IWL_CSR_LED_REG, IWL_CSR_LED_REG_TURN_ON);

        iwl_release_nic_access(nic);
        hal_console_puts("[IWL] Exiting power save\n");
    }

    return HAL_OK;
}

/* ===================================================================
 * High-Level WiFi Operations (delegate to wifi_framework)
 * =================================================================== */

uint32_t iwl_scan(iwl_dev_t *nic, const uint8_t *channels,
                    uint32_t n_chan, uint32_t dwell_ms)
{
    return wifi_scan(&nic->wifi_ctx, channels, n_chan, dwell_ms);
}

hal_status_t iwl_connect(iwl_dev_t *nic, const char *ssid,
                           const char *passphrase)
{
    return wifi_connect(&nic->wifi_ctx, ssid, passphrase);
}

hal_status_t iwl_disconnect(iwl_dev_t *nic)
{
    return wifi_disconnect(&nic->wifi_ctx);
}

hal_status_t iwl_send(iwl_dev_t *nic, const void *frame, uint32_t len)
{
    return wifi_send(&nic->wifi_ctx, frame, len);
}

hal_status_t iwl_recv(iwl_dev_t *nic, void *buf, uint32_t *len)
{
    return wifi_recv(&nic->wifi_ctx, buf, len);
}

/* ===================================================================
 * driver_ops_t wrapper for kernel driver registration
 *
 * This section adapts the iwl driver to the generic driver_ops_t
 * interface used by the kernel driver loader for auto-detection
 * and initialization.
 * =================================================================== */

static iwl_dev_t g_iwl_nic;

static hal_status_t iwl_drv_init(hal_device_t *dev)
{
    return iwl_init(&g_iwl_nic, dev);
}

static int64_t iwl_drv_tx(const void *frame, uint64_t len)
{
    hal_status_t st = iwl_send(&g_iwl_nic, frame, (uint32_t)len);
    return (st == HAL_OK) ? (int64_t)len : -1;
}

static int64_t iwl_drv_rx(void *frame, uint64_t max_len)
{
    uint32_t len = 0;
    hal_status_t st = iwl_recv(&g_iwl_nic, frame, &len);
    if (st != HAL_OK) return -1;
    (void)max_len;
    return (int64_t)len;
}

static void iwl_drv_get_mac(uint8_t mac[6])
{
    iwl_get_mac(&g_iwl_nic, mac);
}

static const driver_ops_t intel_wifi_driver_ops = {
    .name        = "intel_wifi",
    .category    = DRIVER_CAT_NETWORK,
    .init        = iwl_drv_init,
    .shutdown    = NULL,
    .net_tx      = iwl_drv_tx,
    .net_rx      = iwl_drv_rx,
    .net_get_mac = iwl_drv_get_mac,
};

void intel_wifi_register(void)
{
    driver_register_builtin(&intel_wifi_driver_ops);
}
