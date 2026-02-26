/* SPDX-License-Identifier: MIT */
/* AlJefra OS — eMMC / SD Card Driver Implementation
 * SDHCI-based driver for SD and eMMC cards.
 * Architecture-independent; uses HAL for all hardware access.
 */

#include "emmc.h"
#include "../../lib/string.h"

/* ── Internal constants ── */
#define EMMC_TIMEOUT_MS       3000
#define EMMC_CMD_TIMEOUT_MS   1000
#define EMMC_POLL_US          100

/* ── Helpers ── */

static inline uint32_t emmc_read32(sdhci_dev_t *dev, uint32_t off)
{
    return hal_mmio_read32((volatile void *)((uint8_t *)dev->regs + off));
}

static inline uint16_t emmc_read16(sdhci_dev_t *dev, uint32_t off)
{
    return hal_mmio_read16((volatile void *)((uint8_t *)dev->regs + off));
}

static inline uint8_t emmc_read8(sdhci_dev_t *dev, uint32_t off)
{
    return hal_mmio_read8((volatile void *)((uint8_t *)dev->regs + off));
}

static inline void emmc_write32(sdhci_dev_t *dev, uint32_t off, uint32_t val)
{
    hal_mmio_write32((volatile void *)((uint8_t *)dev->regs + off), val);
}

static inline void emmc_write16(sdhci_dev_t *dev, uint32_t off, uint16_t val)
{
    hal_mmio_write16((volatile void *)((uint8_t *)dev->regs + off), val);
}

static inline void emmc_write8(sdhci_dev_t *dev, uint32_t off, uint8_t val)
{
    hal_mmio_write8((volatile void *)((uint8_t *)dev->regs + off), val);
}

/* ── Wait for command inhibit to clear ── */

static hal_status_t emmc_wait_cmd_ready(sdhci_dev_t *dev)
{
    uint64_t deadline = hal_timer_ms() + EMMC_CMD_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t state = emmc_read32(dev, SDHCI_PRESENT_STATE);
        if (!(state & SDHCI_PS_CMD_INHIBIT))
            return HAL_OK;
        hal_timer_delay_us(EMMC_POLL_US);
    }
    return HAL_TIMEOUT;
}

static hal_status_t emmc_wait_dat_ready(sdhci_dev_t *dev)
{
    uint64_t deadline = hal_timer_ms() + EMMC_CMD_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t state = emmc_read32(dev, SDHCI_PRESENT_STATE);
        if (!(state & SDHCI_PS_DAT_INHIBIT))
            return HAL_OK;
        hal_timer_delay_us(EMMC_POLL_US);
    }
    return HAL_TIMEOUT;
}

/* ── Wait for interrupt ── */

static hal_status_t emmc_wait_int(sdhci_dev_t *dev, uint16_t mask, uint32_t timeout_ms)
{
    uint64_t deadline = hal_timer_ms() + timeout_ms;
    while (hal_timer_ms() < deadline) {
        uint16_t status = emmc_read16(dev, SDHCI_INT_STATUS);
        if (status & SDHCI_INT_ERROR) {
            uint16_t err = emmc_read16(dev, SDHCI_ERR_STATUS);
            /* Clear error */
            emmc_write16(dev, SDHCI_ERR_STATUS, err);
            emmc_write16(dev, SDHCI_INT_STATUS, status);
            return HAL_ERROR;
        }
        if (status & mask) {
            emmc_write16(dev, SDHCI_INT_STATUS, mask);
            return HAL_OK;
        }
        hal_timer_delay_us(EMMC_POLL_US);
    }
    return HAL_TIMEOUT;
}

/* ── Software reset ── */

static hal_status_t emmc_reset(sdhci_dev_t *dev, uint8_t mask)
{
    emmc_write8(dev, SDHCI_SOFTWARE_RESET, mask);
    uint64_t deadline = hal_timer_ms() + EMMC_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        if (!(emmc_read8(dev, SDHCI_SOFTWARE_RESET) & mask))
            return HAL_OK;
        hal_timer_delay_us(EMMC_POLL_US);
    }
    return HAL_TIMEOUT;
}

/* ── Clock configuration ── */

static hal_status_t emmc_set_clock(sdhci_dev_t *dev, uint32_t freq_hz)
{
    /* Stop clock */
    uint16_t clk = emmc_read16(dev, SDHCI_CLOCK_CONTROL);
    clk &= ~0x04;  /* SD Clock Enable = 0 */
    emmc_write16(dev, SDHCI_CLOCK_CONTROL, clk);

    /* Calculate divisor: SD clock = base_clock / (2 * divisor) */
    uint32_t divisor = 1;
    if (dev->base_clock > 0 && freq_hz > 0) {
        divisor = (dev->base_clock + freq_hz - 1) / freq_hz;
        if (divisor > 1) divisor = (divisor + 1) / 2;
        if (divisor > 2046) divisor = 2046;
        if (divisor == 0) divisor = 1;
    }

    /* Set divisor (10-bit split: bits 7:0 at [15:8], bits 9:8 at [7:6]) */
    uint16_t div_lo = (uint16_t)((divisor & 0xFF) << 8);
    uint16_t div_hi = (uint16_t)(((divisor >> 8) & 0x03) << 6);
    clk = div_lo | div_hi | 0x01;  /* Internal Clock Enable */
    emmc_write16(dev, SDHCI_CLOCK_CONTROL, clk);

    /* Wait for internal clock stable */
    uint64_t deadline = hal_timer_ms() + EMMC_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        clk = emmc_read16(dev, SDHCI_CLOCK_CONTROL);
        if (clk & 0x02)  /* Internal Clock Stable */
            break;
        hal_timer_delay_us(EMMC_POLL_US);
    }

    /* Enable SD clock */
    clk |= 0x04;  /* SD Clock Enable */
    emmc_write16(dev, SDHCI_CLOCK_CONTROL, clk);

    return HAL_OK;
}

/* ── Power on ── */

static void emmc_power_on(sdhci_dev_t *dev)
{
    /* Read capabilities to determine supported voltages */
    uint32_t caps = emmc_read32(dev, SDHCI_CAPABILITIES);

    uint8_t pwr = 0;
    if (caps & (1u << 24))       /* 3.3V support */
        pwr = 0x0E;              /* 3.3V, power on */
    else if (caps & (1u << 25))  /* 3.0V support */
        pwr = 0x0C;
    else if (caps & (1u << 26))  /* 1.8V support */
        pwr = 0x0A;

    pwr |= 0x01;  /* Bus Power on */
    emmc_write8(dev, SDHCI_POWER_CONTROL, pwr);
    hal_timer_delay_ms(10);
}

/* ── Send a command ── */

static hal_status_t emmc_send_cmd(sdhci_dev_t *dev, uint32_t cmd_idx,
                                   uint32_t arg, uint16_t flags, uint32_t *resp)
{
    hal_status_t st;

    /* Wait for CMD line free */
    st = emmc_wait_cmd_ready(dev);
    if (st != HAL_OK)
        return st;

    /* If command uses data, wait for DAT */
    if (flags & SDHCI_CMD_DATA) {
        st = emmc_wait_dat_ready(dev);
        if (st != HAL_OK)
            return st;
    }

    /* Clear all interrupt status */
    emmc_write16(dev, SDHCI_INT_STATUS, 0xFFFF);
    emmc_write16(dev, SDHCI_ERR_STATUS, 0xFFFF);

    /* Write argument */
    emmc_write32(dev, SDHCI_ARGUMENT, arg);

    /* Write command: [15:8] = cmd index, [7:0] = flags */
    uint16_t cmd_reg = (uint16_t)((cmd_idx & 0x3F) << 8) | (flags & 0xFF);
    emmc_write16(dev, SDHCI_COMMAND, cmd_reg);

    /* Wait for command complete */
    st = emmc_wait_int(dev, SDHCI_INT_CMD_DONE, EMMC_CMD_TIMEOUT_MS);
    if (st != HAL_OK) {
        emmc_reset(dev, SDHCI_RESET_CMD);
        return st;
    }

    /* Read response */
    if (resp) {
        resp[0] = emmc_read32(dev, SDHCI_RESPONSE0);
        if ((flags & 0x03) == 0x01) {  /* 136-bit response */
            resp[1] = emmc_read32(dev, SDHCI_RESPONSE1);
            resp[2] = emmc_read32(dev, SDHCI_RESPONSE2);
            resp[3] = emmc_read32(dev, SDHCI_RESPONSE3);
        }
    }

    return HAL_OK;
}

/* Send APP command (CMD55 + ACMD) */
static hal_status_t emmc_send_acmd(sdhci_dev_t *dev, uint32_t acmd_idx,
                                    uint32_t arg, uint16_t flags, uint32_t *resp)
{
    uint32_t r55[4];
    hal_status_t st = emmc_send_cmd(dev, SD_CMD_APP_CMD, dev->card.rca << 16,
                                     SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK |
                                     SDHCI_CMD_IDX_CHECK, r55);
    if (st != HAL_OK)
        return st;
    return emmc_send_cmd(dev, acmd_idx, arg, flags, resp);
}

/* ── Card initialization sequence ── */

static hal_status_t emmc_card_init(sdhci_dev_t *dev)
{
    hal_status_t st;
    uint32_t resp[4];

    memset(&dev->card, 0, sizeof(sd_card_t));
    dev->card.type = SD_CARD_TYPE_NONE;
    dev->card.block_size = 512;

    /* CMD0: GO_IDLE_STATE — no response */
    st = emmc_send_cmd(dev, SD_CMD_GO_IDLE, 0, SDHCI_CMD_RESP_NONE, NULL);
    if (st != HAL_OK)
        return st;
    hal_timer_delay_ms(10);

    /* CMD8: SEND_IF_COND — check voltage support (SD v2+) */
    /* Argument: [11:8] = voltage (1 = 2.7-3.6V), [7:0] = check pattern (0xAA) */
    st = emmc_send_cmd(dev, SD_CMD_SEND_IF_COND, 0x000001AA,
                        SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK |
                        SDHCI_CMD_IDX_CHECK, resp);

    bool v2_card = false;
    if (st == HAL_OK) {
        if ((resp[0] & 0xFFF) == 0x1AA)
            v2_card = true;
    }

    /* ACMD41: SD_SEND_OP_COND — initialize card and read OCR */
    uint32_t ocr_arg = SD_OCR_3V3 | SD_OCR_3V2;
    if (v2_card)
        ocr_arg |= SD_OCR_HCS;  /* Indicate host supports SDHC */

    /* ACMD41 loop — poll until card is ready (busy bit set) */
    uint64_t deadline = hal_timer_ms() + EMMC_TIMEOUT_MS;
    uint32_t ocr = 0;
    while (hal_timer_ms() < deadline) {
        st = emmc_send_acmd(dev, SD_ACMD_SD_SEND_OP, ocr_arg,
                             SDHCI_CMD_RESP_48, resp);
        if (st != HAL_OK)
            return st;
        ocr = resp[0];
        if (ocr & SD_OCR_BUSY)
            break;
        hal_timer_delay_ms(10);
    }
    if (!(ocr & SD_OCR_BUSY))
        return HAL_TIMEOUT;

    /* Determine card type */
    if (v2_card) {
        if (ocr & SD_OCR_HCS) {
            dev->card.type = SD_CARD_TYPE_SDHC;
            dev->card.sdhc = true;
        } else {
            dev->card.type = SD_CARD_TYPE_SD_V2;
        }
    } else {
        dev->card.type = SD_CARD_TYPE_SD_V1;
    }

    /* CMD2: ALL_SEND_CID — get Card Identification */
    st = emmc_send_cmd(dev, SD_CMD_ALL_SEND_CID, 0,
                        SDHCI_CMD_RESP_136 | SDHCI_CMD_CRC_CHECK, resp);
    if (st != HAL_OK)
        return st;
    dev->card.cid[0] = resp[0];
    dev->card.cid[1] = resp[1];
    dev->card.cid[2] = resp[2];
    dev->card.cid[3] = resp[3];

    /* CMD3: SEND_RELATIVE_ADDR — get RCA */
    st = emmc_send_cmd(dev, SD_CMD_SEND_RCA, 0,
                        SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK |
                        SDHCI_CMD_IDX_CHECK, resp);
    if (st != HAL_OK)
        return st;
    dev->card.rca = (resp[0] >> 16) & 0xFFFF;

    /* CMD9: SEND_CSD — get Card Specific Data */
    st = emmc_send_cmd(dev, SD_CMD_SEND_CSD, dev->card.rca << 16,
                        SDHCI_CMD_RESP_136 | SDHCI_CMD_CRC_CHECK, resp);
    if (st != HAL_OK)
        return st;
    dev->card.csd[0] = resp[0];
    dev->card.csd[1] = resp[1];
    dev->card.csd[2] = resp[2];
    dev->card.csd[3] = resp[3];

    /* Parse capacity from CSD */
    if (dev->card.sdhc) {
        /* CSD v2: C_SIZE in bits [69:48] of CSD */
        /* SDHCI returns response shifted: resp[1] bits [21:0] and resp[2] bits [31:30] */
        uint32_t c_size = ((resp[1] >> 16) & 0x3F) | ((resp[2] & 0xFFFF) << 6);
        /* For SDHC: memory capacity = (C_SIZE + 1) * 512K bytes */
        dev->card.capacity = (uint64_t)(c_size + 1) * 512 * 1024;
    } else {
        /* CSD v1: compute from C_SIZE, C_SIZE_MULT, READ_BL_LEN */
        uint32_t read_bl_len = (resp[2] >> 16) & 0x0F;
        uint32_t c_size = ((resp[2] & 0x03) << 10) | ((resp[1] >> 22) & 0x3FF);
        uint32_t c_size_mult = (resp[1] >> 15) & 0x07;
        uint32_t blocknr = (c_size + 1) * (1u << (c_size_mult + 2));
        uint32_t block_len = 1u << read_bl_len;
        dev->card.capacity = (uint64_t)blocknr * block_len;
    }

    dev->card.total_blocks = dev->card.capacity / dev->card.block_size;

    /* CMD7: SELECT_CARD — put card in Transfer state */
    st = emmc_send_cmd(dev, SD_CMD_SELECT_CARD, dev->card.rca << 16,
                        SDHCI_CMD_RESP_48B | SDHCI_CMD_CRC_CHECK |
                        SDHCI_CMD_IDX_CHECK, resp);
    if (st != HAL_OK)
        return st;

    /* Set block length to 512 for SDSC cards */
    if (!dev->card.sdhc) {
        st = emmc_send_cmd(dev, SD_CMD_SET_BLOCKLEN, 512,
                            SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK |
                            SDHCI_CMD_IDX_CHECK, resp);
        if (st != HAL_OK)
            return st;
    }

    /* ACMD6: Set bus width to 4-bit */
    st = emmc_send_acmd(dev, SD_ACMD_SET_BUS_WIDTH, 0x02, /* 4-bit */
                         SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK |
                         SDHCI_CMD_IDX_CHECK, resp);
    if (st == HAL_OK) {
        /* Enable 4-bit mode in host controller */
        uint8_t hc = emmc_read8(dev, SDHCI_HOST_CONTROL);
        hc |= 0x02;  /* 4-bit bus width */
        emmc_write8(dev, SDHCI_HOST_CONTROL, hc);
    }

    /* Increase clock to 25MHz (full speed) */
    emmc_set_clock(dev, 25000000);

    return HAL_OK;
}

/* ── Data transfer (PIO mode) ── */

static hal_status_t emmc_pio_read_blocks(sdhci_dev_t *dev, uint32_t count,
                                          void *buf)
{
    uint32_t *dst = (uint32_t *)buf;
    uint32_t words_per_block = dev->card.block_size / 4;

    for (uint32_t b = 0; b < count; b++) {
        /* Wait for Buffer Read Ready */
        hal_status_t st = emmc_wait_int(dev, SDHCI_INT_BUF_RD, EMMC_TIMEOUT_MS);
        if (st != HAL_OK)
            return st;

        /* Read block data from Data Port */
        for (uint32_t w = 0; w < words_per_block; w++) {
            *dst++ = emmc_read32(dev, SDHCI_DATA_PORT);
        }
    }

    /* Wait for transfer complete */
    return emmc_wait_int(dev, SDHCI_INT_XFER_DONE, EMMC_TIMEOUT_MS);
}

static hal_status_t emmc_pio_write_blocks(sdhci_dev_t *dev, uint32_t count,
                                           const void *buf)
{
    const uint32_t *src = (const uint32_t *)buf;
    uint32_t words_per_block = dev->card.block_size / 4;

    for (uint32_t b = 0; b < count; b++) {
        /* Wait for Buffer Write Ready */
        hal_status_t st = emmc_wait_int(dev, SDHCI_INT_BUF_WR, EMMC_TIMEOUT_MS);
        if (st != HAL_OK)
            return st;

        /* Write block data to Data Port */
        for (uint32_t w = 0; w < words_per_block; w++) {
            emmc_write32(dev, SDHCI_DATA_PORT, *src++);
        }
    }

    /* Wait for transfer complete */
    return emmc_wait_int(dev, SDHCI_INT_XFER_DONE, EMMC_TIMEOUT_MS);
}

/* ── Public API ── */

hal_status_t emmc_init(sdhci_dev_t *dev, volatile void *regs)
{
    dev->regs = regs;
    dev->initialized = false;

    /* Full reset */
    hal_status_t st = emmc_reset(dev, SDHCI_RESET_ALL);
    if (st != HAL_OK)
        return st;

    /* Read capabilities for base clock */
    uint32_t caps = emmc_read32(dev, SDHCI_CAPABILITIES);
    dev->base_clock = ((caps >> 8) & 0xFF) * 1000000;  /* MHz to Hz */
    if (dev->base_clock == 0)
        dev->base_clock = 50000000;  /* Default 50MHz */
    dev->caps2 = emmc_read32(dev, SDHCI_CAPABILITIES2);
    dev->speed_mode = SD_SPEED_DEFAULT;

    /* Power on */
    emmc_power_on(dev);

    /* Set initial clock (400kHz for identification) */
    emmc_set_clock(dev, 400000);

    /* Set timeout to maximum */
    emmc_write8(dev, SDHCI_TIMEOUT_CONTROL, 0x0E);

    /* Enable all normal and error interrupt statuses */
    emmc_write16(dev, SDHCI_INT_ENABLE, 0xFFFF);
    emmc_write16(dev, SDHCI_ERR_ENABLE, 0xFFFF);

    /* Wait for card insertion if not already present */
    uint64_t deadline = hal_timer_ms() + EMMC_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t state = emmc_read32(dev, SDHCI_PRESENT_STATE);
        if ((state & SDHCI_PS_CARD_INS) && (state & SDHCI_PS_CARD_STABLE))
            break;
        hal_timer_delay_ms(10);
    }

    /* Initialize card */
    st = emmc_card_init(dev);
    if (st != HAL_OK)
        return st;

    dev->initialized = true;

    /* Auto-tune to highest supported speed */
    emmc_tune_speed(dev);

    return HAL_OK;
}

hal_status_t emmc_init_pci(sdhci_dev_t *dev, hal_device_t *hal_dev)
{
    hal_bus_pci_enable(hal_dev);
    volatile void *regs = hal_bus_map_bar(hal_dev, 0);
    if (!regs)
        return HAL_ERROR;
    return emmc_init(dev, regs);
}

hal_status_t emmc_read(sdhci_dev_t *dev, uint64_t block, uint32_t count,
                       void *buf)
{
    if (!dev->initialized)
        return HAL_ERROR;
    if (count == 0)
        return HAL_OK;

    /* For SDSC, address is byte-based; for SDHC, block-based */
    uint32_t arg = dev->card.sdhc ? (uint32_t)block
                                  : (uint32_t)(block * dev->card.block_size);

    /* Set block size and count */
    emmc_write16(dev, SDHCI_BLOCK_SIZE, (uint16_t)dev->card.block_size);
    emmc_write16(dev, SDHCI_BLOCK_COUNT, (uint16_t)count);

    /* Set transfer mode */
    uint16_t tm = SDHCI_TM_READ | SDHCI_TM_BLK_CNT_EN;
    if (count > 1)
        tm |= SDHCI_TM_MULTI_BLK | SDHCI_TM_AUTO_CMD12;
    emmc_write16(dev, SDHCI_TRANSFER_MODE, tm);

    /* Send read command */
    uint32_t cmd_idx = (count > 1) ? SD_CMD_READ_MULTI : SD_CMD_READ_SINGLE;
    uint16_t cmd_flags = SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK |
                          SDHCI_CMD_IDX_CHECK | SDHCI_CMD_DATA;

    uint32_t resp[4];
    hal_status_t st = emmc_send_cmd(dev, cmd_idx, arg, cmd_flags, resp);
    if (st != HAL_OK)
        return st;

    /* Read data via PIO */
    return emmc_pio_read_blocks(dev, count, buf);
}

hal_status_t emmc_write(sdhci_dev_t *dev, uint64_t block, uint32_t count,
                        const void *buf)
{
    if (!dev->initialized)
        return HAL_ERROR;
    if (count == 0)
        return HAL_OK;

    uint32_t arg = dev->card.sdhc ? (uint32_t)block
                                  : (uint32_t)(block * dev->card.block_size);

    /* Set block size and count */
    emmc_write16(dev, SDHCI_BLOCK_SIZE, (uint16_t)dev->card.block_size);
    emmc_write16(dev, SDHCI_BLOCK_COUNT, (uint16_t)count);

    /* Set transfer mode (write direction) */
    uint16_t tm = SDHCI_TM_BLK_CNT_EN;  /* no SDHCI_TM_READ = write */
    if (count > 1)
        tm |= SDHCI_TM_MULTI_BLK | SDHCI_TM_AUTO_CMD12;
    emmc_write16(dev, SDHCI_TRANSFER_MODE, tm);

    /* Send write command */
    uint32_t cmd_idx = (count > 1) ? SD_CMD_WRITE_MULTI : SD_CMD_WRITE_SINGLE;
    uint16_t cmd_flags = SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK |
                          SDHCI_CMD_IDX_CHECK | SDHCI_CMD_DATA;

    uint32_t resp[4];
    hal_status_t st = emmc_send_cmd(dev, cmd_idx, arg, cmd_flags, resp);
    if (st != HAL_OK)
        return st;

    /* Write data via PIO */
    return emmc_pio_write_blocks(dev, count, buf);
}

hal_status_t emmc_get_card_info(sdhci_dev_t *dev, sd_card_t *info)
{
    if (!dev->initialized)
        return HAL_ERROR;
    *info = dev->card;
    return HAL_OK;
}

/* ── Speed Tuning for SBCs ── */

/* CMD6 (SWITCH_FUNC): Check or set access mode.
 * mode: SD_SWITCH_CHECK (0) or SD_SWITCH_SET (1)
 * func: access mode value (HS=1, SDR50=2, SDR104=3, DDR50=4)
 * Returns HAL_OK if the function is supported and switched. */
static hal_status_t emmc_switch_func(sdhci_dev_t *dev, uint32_t mode,
                                      uint32_t func, uint8_t *status_buf)
{
    /* CMD6 argument:
     * [31]    = mode (0=check, 1=set)
     * [3:0]   = function group 1 (Access Mode)
     * [15:4]  = keep current for groups 2-6 (0xFFFFF0)
     */
    uint32_t arg = (mode << 31) | 0x00FFFFF0 | (func & 0x0F);

    /* CMD6 returns 512 bits (64 bytes) of status data */
    emmc_write16(dev, SDHCI_BLOCK_SIZE, 64);
    emmc_write16(dev, SDHCI_BLOCK_COUNT, 1);

    uint16_t tm = SDHCI_TM_READ | SDHCI_TM_BLK_CNT_EN;
    emmc_write16(dev, SDHCI_TRANSFER_MODE, tm);

    uint32_t resp[4];
    hal_status_t st = emmc_send_cmd(dev, SD_CMD_SWITCH_FUNC, arg,
                                     SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK |
                                     SDHCI_CMD_IDX_CHECK | SDHCI_CMD_DATA, resp);
    if (st != HAL_OK)
        return st;

    /* Read the 64 bytes of switch status via PIO */
    st = emmc_wait_int(dev, SDHCI_INT_BUF_RD, EMMC_TIMEOUT_MS);
    if (st != HAL_OK) {
        emmc_reset(dev, SDHCI_RESET_DATA);
        return st;
    }

    uint32_t *dst = (uint32_t *)status_buf;
    for (int i = 0; i < 16; i++)
        dst[i] = emmc_read32(dev, SDHCI_DATA_PORT);

    /* Wait for transfer complete */
    st = emmc_wait_int(dev, SDHCI_INT_XFER_DONE, EMMC_TIMEOUT_MS);
    if (st != HAL_OK) {
        emmc_reset(dev, SDHCI_RESET_DATA);
        return st;
    }

    return HAL_OK;
}

/* Set High-Speed mode in the host controller */
static void emmc_host_set_hs(sdhci_dev_t *dev, bool enable)
{
    uint8_t hc = emmc_read8(dev, SDHCI_HOST_CONTROL);
    if (enable)
        hc |= 0x04;  /* High Speed Enable (bit 2) */
    else
        hc &= ~0x04;
    emmc_write8(dev, SDHCI_HOST_CONTROL, hc);
}

/* Execute tuning sequence for SDR50/SDR104 */
static hal_status_t emmc_execute_tuning(sdhci_dev_t *dev)
{
    uint16_t hc2 = emmc_read16(dev, SDHCI_HOST_CONTROL2);
    hc2 |= SDHCI_HC2_EXEC_TUNING;
    emmc_write16(dev, SDHCI_HOST_CONTROL2, hc2);

    /* Send CMD19 (SEND_TUNING_BLOCK) repeatedly until tuning completes */
    for (int i = 0; i < 40; i++) {
        /* Set up for 64-byte tuning block */
        emmc_write16(dev, SDHCI_BLOCK_SIZE, 64);
        emmc_write16(dev, SDHCI_BLOCK_COUNT, 1);
        emmc_write16(dev, SDHCI_TRANSFER_MODE, SDHCI_TM_READ);

        uint32_t resp[4];
        emmc_send_cmd(dev, 19 /* CMD19 */, 0,
                       SDHCI_CMD_RESP_48 | SDHCI_CMD_CRC_CHECK |
                       SDHCI_CMD_IDX_CHECK | SDHCI_CMD_DATA, resp);

        /* Read tuning data (discard) */
        emmc_wait_int(dev, SDHCI_INT_BUF_RD, 100);
        for (int w = 0; w < 16; w++)
            (void)emmc_read32(dev, SDHCI_DATA_PORT);
        emmc_wait_int(dev, SDHCI_INT_XFER_DONE, 100);

        /* Check if tuning is complete */
        hc2 = emmc_read16(dev, SDHCI_HOST_CONTROL2);
        if (!(hc2 & SDHCI_HC2_EXEC_TUNING)) {
            if (hc2 & SDHCI_HC2_TUNED_CLK) {
                hal_console_puts("[emmc] Tuning complete\n");
                return HAL_OK;
            }
            break;
        }
        hal_timer_delay_us(1000);
    }

    /* Tuning failed — clear bits */
    hc2 = emmc_read16(dev, SDHCI_HOST_CONTROL2);
    hc2 &= ~(SDHCI_HC2_EXEC_TUNING | SDHCI_HC2_TUNED_CLK);
    emmc_write16(dev, SDHCI_HOST_CONTROL2, hc2);

    hal_console_puts("[emmc] Tuning failed (non-fatal)\n");
    return HAL_ERROR;
}

/* Signal voltage switch to 1.8V for UHS-I modes */
static hal_status_t emmc_switch_1v8(sdhci_dev_t *dev)
{
    /* Stop clock */
    uint16_t clk = emmc_read16(dev, SDHCI_CLOCK_CONTROL);
    clk &= ~0x04;
    emmc_write16(dev, SDHCI_CLOCK_CONTROL, clk);

    /* Check DAT[3:0] are low */
    uint32_t state = emmc_read32(dev, SDHCI_PRESENT_STATE);
    if (state & (0xF << 20)) {
        /* DAT lines not low — can't switch voltage */
        clk |= 0x04;
        emmc_write16(dev, SDHCI_CLOCK_CONTROL, clk);
        return HAL_ERROR;
    }

    /* Set 1.8V signaling */
    uint16_t hc2 = emmc_read16(dev, SDHCI_HOST_CONTROL2);
    hc2 |= SDHCI_HC2_1V8_SIG;
    emmc_write16(dev, SDHCI_HOST_CONTROL2, hc2);
    hal_timer_delay_ms(5);

    /* Verify 1.8V bit stuck */
    hc2 = emmc_read16(dev, SDHCI_HOST_CONTROL2);
    if (!(hc2 & SDHCI_HC2_1V8_SIG)) {
        clk |= 0x04;
        emmc_write16(dev, SDHCI_CLOCK_CONTROL, clk);
        return HAL_ERROR;
    }

    /* Re-enable clock */
    clk |= 0x04;
    emmc_write16(dev, SDHCI_CLOCK_CONTROL, clk);
    hal_timer_delay_ms(1);

    /* Check DAT[3:0] are high (card responded to voltage switch) */
    state = emmc_read32(dev, SDHCI_PRESENT_STATE);
    if (!(state & (0xF << 20))) {
        hal_console_puts("[emmc] 1.8V switch: DAT lines didn't come up\n");
        return HAL_ERROR;
    }

    return HAL_OK;
}

hal_status_t emmc_tune_speed(sdhci_dev_t *dev)
{
    if (!dev->initialized)
        return HAL_ERROR;

    /* Only SD v2+ cards support CMD6 */
    if (dev->card.type != SD_CARD_TYPE_SD_V2 &&
        dev->card.type != SD_CARD_TYPE_SDHC) {
        dev->speed_mode = SD_SPEED_DEFAULT;
        return HAL_OK;
    }

    /* Read capabilities 2 */
    dev->caps2 = emmc_read32(dev, SDHCI_CAPABILITIES2);
    dev->uhs_support = (dev->caps2 & (SDHCI_CAP2_SDR50 | SDHCI_CAP2_SDR104 |
                                       SDHCI_CAP2_DDR50)) != 0;

    /* Step 1: Try High-Speed mode (SDR25 = 50MHz) via CMD6 */
    uint8_t sw_status[64];
    memset(sw_status, 0, sizeof(sw_status));

    /* Check if HS is supported (CMD6 mode=check) */
    hal_status_t st = emmc_switch_func(dev, SD_SWITCH_CHECK,
                                         SD_ACCESS_MODE_HS, sw_status);
    if (st != HAL_OK) {
        hal_console_puts("[emmc] CMD6 check not supported\n");
        dev->speed_mode = SD_SPEED_DEFAULT;
        return HAL_OK;
    }

    /* Switch status byte 16 bits [3:0] = group 1 function support bits
     * Byte 13 (big-endian offset) has group 1 support.
     * Bytes 12-13 (big-endian) = function group 1 support bits.
     * Bit 1 set means HS is supported. */
    uint16_t group1_support = ((uint16_t)sw_status[12] << 8) | sw_status[13];

    if (!(group1_support & (1u << SD_ACCESS_MODE_HS))) {
        hal_console_puts("[emmc] High-Speed mode not supported by card\n");
        dev->speed_mode = SD_SPEED_DEFAULT;
        return HAL_OK;
    }

    /* Set High-Speed mode (CMD6 mode=set) */
    memset(sw_status, 0, sizeof(sw_status));
    st = emmc_switch_func(dev, SD_SWITCH_SET, SD_ACCESS_MODE_HS, sw_status);
    if (st != HAL_OK) {
        hal_console_puts("[emmc] CMD6 switch to HS failed\n");
        dev->speed_mode = SD_SPEED_DEFAULT;
        return HAL_OK;
    }

    /* Verify switch result: byte 16 bits [3:0] should be the selected function */
    uint8_t selected = sw_status[16] & 0x0F;
    if (selected != SD_ACCESS_MODE_HS) {
        hal_console_printf("[emmc] HS switch rejected: selected=%u\n", selected);
        dev->speed_mode = SD_SPEED_DEFAULT;
        return HAL_OK;
    }

    /* Enable HS in host controller and set 50MHz */
    emmc_host_set_hs(dev, true);
    emmc_set_clock(dev, 50000000);
    dev->speed_mode = SD_SPEED_HIGH;
    hal_console_puts("[emmc] High-Speed mode: 50 MHz\n");

    /* Step 2: Try UHS-I modes if controller supports them */
    if (!dev->uhs_support || !dev->card.sdhc) {
        hal_console_printf("[emmc] Speed mode: %s (%u MHz)\n",
                           dev->speed_mode == SD_SPEED_HIGH ? "HS" : "DS",
                           dev->speed_mode == SD_SPEED_HIGH ? 50 : 25);
        return HAL_OK;
    }

    /* Try voltage switch to 1.8V for UHS-I */
    st = emmc_switch_1v8(dev);
    if (st != HAL_OK) {
        hal_console_puts("[emmc] 1.8V signaling not available, staying at HS\n");
        return HAL_OK;
    }

    /* Try SDR50 if supported */
    if (dev->caps2 & SDHCI_CAP2_SDR50) {
        memset(sw_status, 0, sizeof(sw_status));
        st = emmc_switch_func(dev, SD_SWITCH_CHECK, SD_ACCESS_MODE_SDR50, sw_status);
        if (st == HAL_OK) {
            group1_support = ((uint16_t)sw_status[12] << 8) | sw_status[13];
            if (group1_support & (1u << SD_ACCESS_MODE_SDR50)) {
                memset(sw_status, 0, sizeof(sw_status));
                st = emmc_switch_func(dev, SD_SWITCH_SET, SD_ACCESS_MODE_SDR50, sw_status);
                if (st == HAL_OK && (sw_status[16] & 0x0F) == SD_ACCESS_MODE_SDR50) {
                    /* Set UHS mode in host controller */
                    uint16_t hc2 = emmc_read16(dev, SDHCI_HOST_CONTROL2);
                    hc2 = (hc2 & ~SDHCI_HC2_UHS_MASK) | SDHCI_HC2_SDR50;
                    emmc_write16(dev, SDHCI_HOST_CONTROL2, hc2);

                    emmc_set_clock(dev, 100000000);
                    dev->speed_mode = SD_SPEED_SDR50;
                    hal_console_puts("[emmc] SDR50 mode: 100 MHz\n");

                    /* Execute tuning if required */
                    if (dev->caps2 & SDHCI_CAP2_TUNING_SDR50)
                        emmc_execute_tuning(dev);

                    return HAL_OK;
                }
            }
        }
    }

    /* Try DDR50 if supported */
    if (dev->caps2 & SDHCI_CAP2_DDR50) {
        memset(sw_status, 0, sizeof(sw_status));
        st = emmc_switch_func(dev, SD_SWITCH_CHECK, SD_ACCESS_MODE_DDR50, sw_status);
        if (st == HAL_OK) {
            group1_support = ((uint16_t)sw_status[12] << 8) | sw_status[13];
            if (group1_support & (1u << SD_ACCESS_MODE_DDR50)) {
                memset(sw_status, 0, sizeof(sw_status));
                st = emmc_switch_func(dev, SD_SWITCH_SET, SD_ACCESS_MODE_DDR50, sw_status);
                if (st == HAL_OK && (sw_status[16] & 0x0F) == SD_ACCESS_MODE_DDR50) {
                    uint16_t hc2 = emmc_read16(dev, SDHCI_HOST_CONTROL2);
                    hc2 = (hc2 & ~SDHCI_HC2_UHS_MASK) | SDHCI_HC2_DDR50;
                    emmc_write16(dev, SDHCI_HOST_CONTROL2, hc2);

                    emmc_set_clock(dev, 50000000);
                    dev->speed_mode = SD_SPEED_DDR50;
                    hal_console_puts("[emmc] DDR50 mode: 50 MHz DDR\n");
                    return HAL_OK;
                }
            }
        }
    }

    hal_console_printf("[emmc] Final speed mode: %s\n",
                       dev->speed_mode == SD_SPEED_HIGH ? "HS (50MHz)" :
                       dev->speed_mode == SD_SPEED_SDR50 ? "SDR50 (100MHz)" :
                       dev->speed_mode == SD_SPEED_DDR50 ? "DDR50 (50MHz DDR)" :
                       "Default (25MHz)");
    return HAL_OK;
}
