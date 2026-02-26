/* SPDX-License-Identifier: MIT */
/* AlJefra OS — eMMC / SD Card Driver
 * SD Host Controller Interface (SDHCI) driver for eMMC and SD cards.
 * Architecture-independent; uses HAL for all hardware access.
 */

#ifndef ALJEFRA_DRV_EMMC_H
#define ALJEFRA_DRV_EMMC_H

#include "../../hal/hal.h"

/* ── SDHCI Register offsets ── */
#define SDHCI_SDMA_ADDR       0x00    /* SDMA System Address */
#define SDHCI_BLOCK_SIZE      0x04    /* Block Size (16-bit at +0x04) */
#define SDHCI_BLOCK_COUNT     0x06    /* Block Count (16-bit at +0x06) */
#define SDHCI_ARGUMENT        0x08    /* Argument (32-bit) */
#define SDHCI_TRANSFER_MODE   0x0C    /* Transfer Mode (16-bit) */
#define SDHCI_COMMAND         0x0E    /* Command (16-bit) */
#define SDHCI_RESPONSE0       0x10    /* Response bits 0-31 */
#define SDHCI_RESPONSE1       0x14    /* Response bits 32-63 */
#define SDHCI_RESPONSE2       0x18    /* Response bits 64-95 */
#define SDHCI_RESPONSE3       0x1C    /* Response bits 96-127 */
#define SDHCI_DATA_PORT       0x20    /* Buffer Data Port (32-bit) */
#define SDHCI_PRESENT_STATE   0x24    /* Present State (32-bit) */
#define SDHCI_HOST_CONTROL    0x28    /* Host Control 1 (8-bit) */
#define SDHCI_POWER_CONTROL   0x29    /* Power Control (8-bit) */
#define SDHCI_BLOCK_GAP       0x2A    /* Block Gap Control */
#define SDHCI_WAKEUP_CONTROL  0x2B    /* Wakeup Control */
#define SDHCI_CLOCK_CONTROL   0x2C    /* Clock Control (16-bit) */
#define SDHCI_TIMEOUT_CONTROL 0x2E    /* Timeout Control (8-bit) */
#define SDHCI_SOFTWARE_RESET  0x2F    /* Software Reset (8-bit) */
#define SDHCI_INT_STATUS      0x30    /* Normal Interrupt Status (16-bit) */
#define SDHCI_ERR_STATUS      0x32    /* Error Interrupt Status (16-bit) */
#define SDHCI_INT_ENABLE      0x34    /* Normal Interrupt Status Enable */
#define SDHCI_ERR_ENABLE      0x36    /* Error Interrupt Status Enable */
#define SDHCI_INT_SIGNAL      0x38    /* Normal Interrupt Signal Enable */
#define SDHCI_ERR_SIGNAL      0x3A    /* Error Interrupt Signal Enable */
#define SDHCI_CAPABILITIES    0x40    /* Capabilities (32-bit) */
#define SDHCI_CAPABILITIES2   0x44    /* Capabilities 2 (32-bit) */
#define SDHCI_MAX_CURRENT     0x48    /* Max Current Capabilities */
#define SDHCI_AUTO_CMD_STATUS 0x3C    /* Auto CMD Error Status */
#define SDHCI_HOST_CONTROL2   0x3E    /* Host Control 2 (16-bit) */
#define SDHCI_HOST_VERSION    0xFE    /* Host Controller Version */

/* ── Host Control 2 bits ── */
#define SDHCI_HC2_UHS_MASK    0x0007  /* UHS Mode Select */
#define SDHCI_HC2_SDR12       0x0000
#define SDHCI_HC2_SDR25       0x0001
#define SDHCI_HC2_SDR50       0x0002
#define SDHCI_HC2_SDR104      0x0003
#define SDHCI_HC2_DDR50       0x0004
#define SDHCI_HC2_1V8_SIG     (1u << 3)   /* 1.8V Signaling Enable */
#define SDHCI_HC2_EXEC_TUNING (1u << 6)   /* Execute Tuning */
#define SDHCI_HC2_TUNED_CLK   (1u << 7)   /* Sampling Clock Select */

/* ── Capabilities 2 bits ── */
#define SDHCI_CAP2_SDR50      (1u << 0)
#define SDHCI_CAP2_SDR104     (1u << 1)
#define SDHCI_CAP2_DDR50      (1u << 2)
#define SDHCI_CAP2_DRV_TYPE_A (1u << 4)
#define SDHCI_CAP2_DRV_TYPE_C (1u << 5)
#define SDHCI_CAP2_DRV_TYPE_D (1u << 6)
#define SDHCI_CAP2_TUNING_SDR50 (1u << 13)
#define SDHCI_CAP2_USE_TUNING (1u << 14)

/* ── Present State bits ── */
#define SDHCI_PS_CMD_INHIBIT  (1u << 0)     /* Command Inhibit (CMD) */
#define SDHCI_PS_DAT_INHIBIT  (1u << 1)     /* Command Inhibit (DAT) */
#define SDHCI_PS_DAT_ACTIVE   (1u << 2)     /* DAT Line Active */
#define SDHCI_PS_WRITE_ACTIVE (1u << 8)     /* Write Transfer Active */
#define SDHCI_PS_READ_ACTIVE  (1u << 9)     /* Read Transfer Active */
#define SDHCI_PS_BUF_WR_EN   (1u << 10)    /* Buffer Write Enable */
#define SDHCI_PS_BUF_RD_EN   (1u << 11)    /* Buffer Read Enable */
#define SDHCI_PS_CARD_INS    (1u << 16)    /* Card Inserted */
#define SDHCI_PS_CARD_STABLE (1u << 17)    /* Card Stable */

/* ── Software Reset bits ── */
#define SDHCI_RESET_ALL       0x01
#define SDHCI_RESET_CMD       0x02
#define SDHCI_RESET_DATA      0x04

/* ── Normal Interrupt Status bits ── */
#define SDHCI_INT_CMD_DONE    (1u << 0)
#define SDHCI_INT_XFER_DONE  (1u << 1)
#define SDHCI_INT_BLK_GAP    (1u << 2)
#define SDHCI_INT_DMA        (1u << 3)
#define SDHCI_INT_BUF_WR     (1u << 4)
#define SDHCI_INT_BUF_RD     (1u << 5)
#define SDHCI_INT_CARD_INS   (1u << 6)
#define SDHCI_INT_CARD_REM   (1u << 7)
#define SDHCI_INT_ERROR      (1u << 15)

/* ── Transfer Mode bits ── */
#define SDHCI_TM_DMA_EN      (1u << 0)
#define SDHCI_TM_BLK_CNT_EN  (1u << 1)
#define SDHCI_TM_AUTO_CMD12  (1u << 2)
#define SDHCI_TM_AUTO_CMD23  (1u << 3)
#define SDHCI_TM_READ        (1u << 4)     /* 1 = read, 0 = write */
#define SDHCI_TM_MULTI_BLK   (1u << 5)

/* ── Command register bits ── */
#define SDHCI_CMD_RESP_NONE   (0u << 0)
#define SDHCI_CMD_RESP_136    (1u << 0)
#define SDHCI_CMD_RESP_48     (2u << 0)
#define SDHCI_CMD_RESP_48B    (3u << 0)     /* 48-bit with busy */
#define SDHCI_CMD_CRC_CHECK   (1u << 3)
#define SDHCI_CMD_IDX_CHECK   (1u << 4)
#define SDHCI_CMD_DATA        (1u << 5)

/* ── SD/MMC Commands ── */
#define SD_CMD_GO_IDLE        0       /* CMD0: Reset */
#define SD_CMD_ALL_SEND_CID   2       /* CMD2: Get CID */
#define SD_CMD_SEND_RCA       3       /* CMD3: Get Relative Card Address */
#define SD_CMD_SELECT_CARD    7       /* CMD7: Select/deselect card */
#define SD_CMD_SEND_IF_COND   8       /* CMD8: Send Interface Condition */
#define SD_CMD_SEND_CSD       9       /* CMD9: Get CSD */
#define SD_CMD_STOP_TRANS     12      /* CMD12: Stop transmission */
#define SD_CMD_SET_BLOCKLEN   16      /* CMD16: Set Block Length */
#define SD_CMD_READ_SINGLE    17      /* CMD17: Read Single Block */
#define SD_CMD_READ_MULTI     18      /* CMD18: Read Multiple Block */
#define SD_CMD_WRITE_SINGLE   24      /* CMD24: Write Single Block */
#define SD_CMD_WRITE_MULTI    25      /* CMD25: Write Multiple Block */
#define SD_CMD_APP_CMD        55      /* CMD55: App command prefix */
#define SD_CMD_SWITCH_FUNC    6       /* CMD6: Switch Function */
#define SD_ACMD_SET_BUS_WIDTH 6       /* ACMD6: Set Bus Width */
#define SD_ACMD_SD_STATUS     13      /* ACMD13: SD Status */
#define SD_ACMD_SD_SEND_OP    41      /* ACMD41: SD_SEND_OP_COND */
#define SD_ACMD_SEND_SCR      51      /* ACMD51: Send SCR */

/* ── SD Switch Function group 1 (Access Mode) values ── */
#define SD_SWITCH_CHECK       0       /* Check function */
#define SD_SWITCH_SET         1       /* Set function */
#define SD_ACCESS_MODE_DS     0       /* Default Speed (12.5MB/s) */
#define SD_ACCESS_MODE_HS     1       /* High Speed (25MB/s) */
#define SD_ACCESS_MODE_SDR50  2       /* SDR50 (50MB/s) */
#define SD_ACCESS_MODE_SDR104 3       /* SDR104 (104MB/s) */
#define SD_ACCESS_MODE_DDR50  4       /* DDR50 (50MB/s) */

/* ── OCR register bits ── */
#define SD_OCR_HCS            (1u << 30)    /* Host Capacity Support (SDHC/SDXC) */
#define SD_OCR_BUSY           (1u << 31)    /* Card power-up status (busy if 0) */
#define SD_OCR_S18R           (1u << 24)    /* Switching to 1.8V request */
#define SD_OCR_S18A           (1u << 24)    /* Switching to 1.8V accepted (in response) */
#define SD_OCR_3V3            (1u << 20)    /* 3.2-3.3V */
#define SD_OCR_3V2            (1u << 19)    /* 3.1-3.2V */

/* ── Card types ── */
typedef enum {
    SD_CARD_TYPE_SD_V1  = 0,    /* SD 1.x (SDSC) */
    SD_CARD_TYPE_SD_V2  = 1,    /* SD 2.0 (SDSC) */
    SD_CARD_TYPE_SDHC   = 2,    /* SDHC/SDXC */
    SD_CARD_TYPE_MMC    = 3,    /* eMMC */
    SD_CARD_TYPE_NONE   = 4,
} sd_card_type_t;

/* ── Card info ── */
typedef struct {
    sd_card_type_t type;
    uint32_t       rca;         /* Relative Card Address */
    uint64_t       capacity;    /* Capacity in bytes */
    uint32_t       block_size;  /* Block size (usually 512) */
    uint64_t       total_blocks;/* Total blocks */
    bool           sdhc;        /* High-capacity card */
    uint32_t       cid[4];      /* Card Identification register */
    uint32_t       csd[4];      /* Card Specific Data register */
} sd_card_t;

/* ── Speed modes (reported after tuning) ── */
typedef enum {
    SD_SPEED_DEFAULT  = 0,   /* Default Speed: 25MHz max */
    SD_SPEED_HIGH     = 1,   /* High Speed: 50MHz max */
    SD_SPEED_SDR50    = 2,   /* SDR50: 100MHz max (1.8V) */
    SD_SPEED_SDR104   = 3,   /* SDR104: 208MHz max (1.8V) */
    SD_SPEED_DDR50    = 4,   /* DDR50: 50MHz DDR (1.8V) */
} sd_speed_mode_t;

/* ── SDHCI controller state ── */
typedef struct {
    volatile void  *regs;       /* SDHCI register base (MMIO) */
    sd_card_t       card;       /* Detected card */
    uint32_t        base_clock; /* Base clock in Hz (from capabilities) */
    uint32_t        caps2;      /* Capabilities register 2 */
    sd_speed_mode_t speed_mode; /* Current speed mode */
    bool            uhs_support;/* Controller supports UHS-I modes */
    bool            initialized;
} sdhci_dev_t;

/* ── Public API ── */

/* Initialize SDHCI controller and detect/initialize card.
 * `regs` is the MMIO base of the SDHCI register set. */
hal_status_t emmc_init(sdhci_dev_t *dev, volatile void *regs);

/* Initialize from HAL device (PCI-attached SD controller) */
hal_status_t emmc_init_pci(sdhci_dev_t *dev, hal_device_t *hal_dev);

/* Read blocks from card */
hal_status_t emmc_read(sdhci_dev_t *dev, uint64_t block, uint32_t count,
                       void *buf);

/* Write blocks to card */
hal_status_t emmc_write(sdhci_dev_t *dev, uint64_t block, uint32_t count,
                        const void *buf);

/* Get card info */
hal_status_t emmc_get_card_info(sdhci_dev_t *dev, sd_card_t *info);

/* Tune card to highest supported speed mode.
 * Call after emmc_init/emmc_init_pci. Tries HS → SDR50 → DDR50. */
hal_status_t emmc_tune_speed(sdhci_dev_t *dev);

#endif /* ALJEFRA_DRV_EMMC_H */
