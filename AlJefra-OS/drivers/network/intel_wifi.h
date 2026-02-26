/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- Intel AX200/AX210 WiFi Driver
 * Architecture-independent; uses HAL for all hardware access.
 *
 * Supports:
 *   - Intel Wi-Fi 6 AX200   (PCI ID 8086:2723)
 *   - Intel Wi-Fi 6E AX210  (PCI ID 8086:2725)
 *   - Intel Wi-Fi 6E AX210  (PCI ID 8086:4DF0)  -- alternate sub-SKU
 *
 * The Intel WiFi hardware (iwlwifi family) uses a command/response
 * architecture with TFD (Transmit Frame Descriptor) rings for commands
 * and data, and RBD (Receive Buffer Descriptor) rings for responses
 * and received frames.
 *
 * Firmware is required and must be loaded before the device becomes
 * operational. The driver parses IWL firmware format (.ucode files).
 *
 * Hardware reference: Linux iwlwifi driver (drivers/net/wireless/intel/iwlwifi)
 */

#ifndef ALJEFRA_DRV_INTEL_WIFI_H
#define ALJEFRA_DRV_INTEL_WIFI_H

#include "../../hal/hal.h"
#include "wifi_framework.h"

/* ====================================================================
 * Device IDs
 * ==================================================================== */

#define INTEL_VENDOR_ID            0x8086
#define INTEL_AX200_DEVICE_ID      0x2723   /* Wi-Fi 6 AX200 160MHz */
#define INTEL_AX210_DEVICE_ID      0x2725   /* Wi-Fi 6E AX210 160MHz */
#define INTEL_AX210_ALT_DEVICE_ID  0x4DF0   /* Wi-Fi 6E AX210 (alternate SKU) */

/* ====================================================================
 * CSR (Control/Status Registers) -- BAR0 direct access
 * These registers are in the device's MMIO space mapped via BAR0.
 *
 * Reference: iwl-csr.h in Linux iwlwifi
 * ==================================================================== */

/* Hardware IF Configuration -- bits define board/antenna config */
#define IWL_CSR_HW_IF_CONFIG        0x000
#define IWL_CSR_HW_IF_CONFIG_NIC_READY      (1u << 22)
#define IWL_CSR_HW_IF_CONFIG_PREPARE         (1u << 27)

/* Interrupt cause register -- read to determine interrupt source */
#define IWL_CSR_INT                 0x008
#define IWL_CSR_INT_BIT_FH_RX      (1u << 31)  /* RX DMA completed */
#define IWL_CSR_INT_BIT_HW_ERR     (1u << 29)  /* Hardware error */
#define IWL_CSR_INT_BIT_RF_KILL     (1u << 7)   /* RF Kill switch toggled */
#define IWL_CSR_INT_BIT_CT_KILL     (1u << 6)   /* Critical temp shutdown */
#define IWL_CSR_INT_BIT_SW_RX      (1u << 3)   /* RX command response */
#define IWL_CSR_INT_BIT_WAKEUP     (1u << 1)   /* Wakeup interrupt */
#define IWL_CSR_INT_BIT_ALIVE      (1u << 0)   /* uCode ALIVE notification */
#define IWL_CSR_INT_BIT_FH_TX      (1u << 27)  /* TX DMA completed */
#define IWL_CSR_INT_BIT_SW_ERR     (1u << 25)  /* uCode software error */

/* Interrupt mask register */
#define IWL_CSR_INT_MASK            0x00C

/* FH (Firmware-Hardware) interrupt status */
#define IWL_CSR_FH_INT              0x010
#define IWL_CSR_FH_INT_RX_MASK     (1u << 16 | 1u << 17)  /* RX channels 0-1 */
#define IWL_CSR_FH_INT_TX_MASK     (1u << 0)               /* TX channel 0 */

/* Reset control */
#define IWL_CSR_RESET               0x020
#define IWL_CSR_RESET_REG_FLAG_SW_RESET        (1u << 7)
#define IWL_CSR_RESET_REG_FLAG_MASTER_DISABLED (1u << 8)
#define IWL_CSR_RESET_REG_FLAG_STOP_MASTER     (1u << 9)
#define IWL_CSR_RESET_REG_FLAG_NEVO_RESET      (1u << 0)

/* GP (General Purpose) control -- main power/clock management register */
#define IWL_CSR_GP_CNTRL           0x024
#define IWL_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY  (1u << 0)
#define IWL_CSR_GP_CNTRL_REG_FLAG_INIT_DONE        (1u << 2)
#define IWL_CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ   (1u << 3)
#define IWL_CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP   (1u << 4)
#define IWL_CSR_GP_CNTRL_REG_FLAG_XTAL_ON          (1u << 15)
#define IWL_CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW    (1u << 27)
#define IWL_CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN     (1u << 0)

/* Hardware Revision -- identifies exact silicon stepping */
#define IWL_CSR_HW_REV             0x028
#define IWL_CSR_HW_REV_STEP_MASK  0x000000C   /* Silicon stepping */
#define IWL_CSR_HW_REV_DASH_MASK  0x0000003   /* Dash number */
#define IWL_CSR_HW_REV_TYPE_MASK  0x000FFF0   /* Device type */

/* EEPROM / OTP / NVM access registers */
#define IWL_CSR_EEPROM_GP          0x030
#define IWL_CSR_OTP_GP             0x034
#define IWL_CSR_GIO                0x03C
#define IWL_CSR_GIO_REG_VAL_L0S_ENABLED (1u << 1)

/* uCode-related registers */
#define IWL_CSR_UCODE_DRV_GP1     0x054
#define IWL_CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED  (1u << 2)
#define IWL_CSR_UCODE_DRV_GP2     0x060

/* GP (General Purpose) uCode register */
#define IWL_CSR_GP_UCODE           0x048
#define IWL_CSR_GP_UCODE_REG_FLAG_CMD_READY  (1u << 0)

/* LED control */
#define IWL_CSR_LED_REG            0x094
#define IWL_CSR_LED_REG_TURN_ON   (1u << 5)
#define IWL_CSR_LED_REG_TURN_OFF  0

/* DRAM interrupt table register */
#define IWL_CSR_DRAM_INT_TBL_REG  0x00A
#define IWL_CSR_DRAM_INT_TBL_ENABLE  (1u << 31)
#define IWL_CSR_DRAM_INIT_TBL_WRAP_CHECK (1u << 27)

/* DBG registers */
#define IWL_CSR_DBG_HPET_MEM      0x240
#define IWL_CSR_DBG_LINK_PWR_MGMT 0x250

/* MSIX related */
#define IWL_CSR_MSIX_FH_INT_CAUSES_AD   0x800
#define IWL_CSR_MSIX_HW_INT_CAUSES_AD   0x808

/* ====================================================================
 * Peripheral (PRPH) Registers -- accessed indirectly via CSR
 * These are device-internal registers accessible through a window
 * mechanism: write address to PRPH_RADDR/WADDR, then read/write data
 * from PRPH_RDATA/WDATA.
 *
 * Reference: iwl-prph.h in Linux iwlwifi
 * ==================================================================== */

/* Indirect register access window */
#define IWL_CSR_PRPH_WADDR        0x044
#define IWL_CSR_PRPH_RADDR        0x048
#define IWL_CSR_PRPH_WDATA        0x04C
#define IWL_CSR_PRPH_RDATA        0x050

/* APMG (Autonomous Power Management Group) registers -- peripheral space */
#define IWL_APMG_BASE              0x003000
#define IWL_APMG_CLK_CTRL         (IWL_APMG_BASE + 0x0000)
#define IWL_APMG_CLK_EN           (IWL_APMG_BASE + 0x0004)
#define IWL_APMG_CLK_DIS          (IWL_APMG_BASE + 0x0008)
#define IWL_APMG_PS               (IWL_APMG_BASE + 0x000C)
#define IWL_APMG_DIGITAL_SVR      (IWL_APMG_BASE + 0x0058)
#define IWL_APMG_ANALOG_SVR       (IWL_APMG_BASE + 0x006C)
#define IWL_APMG_PCIDEV_STT       (IWL_APMG_BASE + 0x0010)
#define IWL_APMG_RTC_INT_MSK      (IWL_APMG_BASE + 0x0048)
#define IWL_APMG_RTC_INT_STT      (IWL_APMG_BASE + 0x004C)

/* Clock enable bits for APMG_CLK_EN/DIS */
#define IWL_APMG_CLK_VAL_DMA_CLK_RQT    (1u << 9)
#define IWL_APMG_CLK_VAL_BSM_CLK_RQT    (1u << 11)

/* APMG PS (Power Source) register values */
#define IWL_APMG_PS_CTRL_VAL_RESET_REQ   (1u << 26)

/* BSM (Bootstrap) registers -- used for initial firmware loading */
#define IWL_BSM_BASE               0x003400
#define IWL_BSM_WR_CTRL           (IWL_BSM_BASE + 0x0000)
#define IWL_BSM_WR_MEM_SRC        (IWL_BSM_BASE + 0x0004)
#define IWL_BSM_WR_MEM_DST        (IWL_BSM_BASE + 0x0008)
#define IWL_BSM_WR_DWCOUNT        (IWL_BSM_BASE + 0x000C)
#define IWL_BSM_SRAM_BASE         0x003800

/* BSM control bits */
#define IWL_BSM_WR_CTRL_START     (1u << 31)

/* SCD (Scheduler) registers -- TX queue scheduling */
#define IWL_SCD_BASE               0x0A02C00
#define IWL_SCD_SRAM_BASE_ADDR    (IWL_SCD_BASE + 0x000)
#define IWL_SCD_DRAM_BASE_ADDR    (IWL_SCD_BASE + 0x010)
#define IWL_SCD_CONTEXT_MEM_LOWER (IWL_SCD_BASE + 0x020)
#define IWL_SCD_CONTEXT_MEM_UPPER (IWL_SCD_BASE + 0x028)
#define IWL_SCD_TX_STTS_MEM_LOWER (IWL_SCD_BASE + 0x040)
#define IWL_SCD_TX_STTS_MEM_UPPER (IWL_SCD_BASE + 0x048)
#define IWL_SCD_TRANSLATE_TBL     (IWL_SCD_BASE + 0x07C)
#define IWL_SCD_CONTEXT_QUEUE_OFFSET(q) (IWL_SCD_BASE + 0x0A0 + (q) * 8)
#define IWL_SCD_TXFACT             (IWL_SCD_BASE + 0x01C)
#define IWL_SCD_QUEUECHAIN_SEL    (IWL_SCD_BASE + 0x0E0)
#define IWL_SCD_AGGR_SEL          (IWL_SCD_BASE + 0x0E4)
#define IWL_SCD_INTERRUPT_MASK    (IWL_SCD_BASE + 0x108)
#define IWL_SCD_GP_CTRL           (IWL_SCD_BASE + 0x1A8)
#define IWL_SCD_EN_CTRL           (IWL_SCD_BASE + 0x1B0)
#define IWL_SCD_QUEUE_STATUS(q)   (IWL_SCD_BASE + 0x104 + 4 * (q))

/* Scheduler queue config bits */
#define IWL_SCD_QUEUE_CTX_REG1_CREDIT_SHIFT    8
#define IWL_SCD_QUEUE_CTX_REG1_CREDIT_MASK     (0x1F << 8)
#define IWL_SCD_QUEUE_CTX_REG1_SUPER_CREDIT    (1u << 24)
#define IWL_SCD_QUEUE_CTX_REG2_FRAME_LIMIT_SHIFT 16
#define IWL_SCD_QUEUE_CTX_REG2_WIN_SIZE_SHIFT  0

/* ====================================================================
 * FH (Firmware-Hardware) DMA Engine Registers -- BAR0 direct access
 * These control the DMA engine that moves data between host memory
 * and the device's internal SRAM.
 *
 * Reference: fh.h in Linux iwlwifi
 * ==================================================================== */

/* TFD circular buffer base address per queue */
#define IWL_FH_MEM_CBBC_BASE      0x19000
#define IWL_FH_MEM_CBBC(q)        (IWL_FH_MEM_CBBC_BASE + 4 * (q))

/* TX DMA channel status/config registers */
#define IWL_FH_TCSR_BASE          0x1D00
#define IWL_FH_TCSR_CONFIG(ch)    (IWL_FH_TCSR_BASE + 0x20 * (ch))
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE    (1u << 31)
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE     (1u << 30)
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE  (1u << 3)
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_ENDTFD   (1u << 20)
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_CIRQ_RTC_NOINT     0
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_MSG_MODE_TFD       (1u << 8)

/* TX DMA status */
#define IWL_FH_TSSR_BASE          0x1EA0
#define IWL_FH_TSSR_TX_STATUS     (IWL_FH_TSSR_BASE + 0x010)
#define IWL_FH_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(ch) (1u << ((ch) + 24))
#define IWL_FH_TSSR_TX_ERROR      (IWL_FH_TSSR_BASE + 0x018)

/* RX DMA configuration */
#define IWL_FH_RX_CONFIG           0x1C00
#define IWL_FH_RX_CONFIG_REG_VAL_DMA_CHNL_EN_ENABLE  (1u << 31)
#define IWL_FH_RX_CONFIG_REG_VAL_RB_SIZE_4K           0
#define IWL_FH_RX_CONFIG_REG_VAL_RB_SIZE_8K           (1u << 16)
#define IWL_FH_RX_CONFIG_REG_VAL_NRBD_MASK            (0xFFFu << 20)
#define IWL_FH_RX_CONFIG_REG_IRQ_DST_NO_INT_OR_MATCH  0
#define IWL_FH_RX_CONFIG_REG_IRQ_DST_INT_HOST         (1u << 12)
#define IWL_FH_RX_CONFIG_SINGLE_FRAME                  (1u << 15)

/* RX buffer descriptor base address */
#define IWL_FH_RSCSR_CHNL0_RBDC_BASE   0x1C80
/* RX write pointer (number of RBDs provided to HW) */
#define IWL_FH_RSCSR_CHNL0_WPTR        0x1C88
/* RX status write pointer -- device writes status here */
#define IWL_FH_RSCSR_CHNL0_STTS_WPTR   0x1C90

/* RX status area */
#define IWL_FH_MEM_RSCSR_BASE     0x1BC0

/* ====================================================================
 * Firmware Image Constants
 * ==================================================================== */

/* IWL ucode file magic */
#define IWL_UCODE_MAGIC           0x0A4C5749  /* "IWL\n" */

/* Firmware section types (TLV IDs) */
#define IWL_UCODE_TLV_INST        1    /* Instruction text */
#define IWL_UCODE_TLV_DATA        2    /* Data */
#define IWL_UCODE_TLV_INIT        3    /* Init instruction text */
#define IWL_UCODE_TLV_INIT_DATA   4    /* Init data */
#define IWL_UCODE_TLV_BOOT        5    /* Bootstrap */
#define IWL_UCODE_TLV_PROBE_MAX   6    /* Max probe length */
#define IWL_UCODE_TLV_SEC_RT      18   /* Runtime section (gen2) */
#define IWL_UCODE_TLV_SEC_INIT    19   /* Init section (gen2) */
#define IWL_UCODE_TLV_SEC_WOWLAN  20   /* WoWLAN section */
#define IWL_UCODE_TLV_PHY_SKU     35   /* PHY SKU */
#define IWL_UCODE_TLV_CMD_VER     48   /* Command versions */
#define IWL_UCODE_TLV_FW_DBG_DEST 56   /* Debug destination */
#define IWL_UCODE_TLV_PHY_CFG     72   /* PHY configuration */
#define IWL_UCODE_TLV_PNVM_VER    78   /* Production NVM version */
#define IWL_UCODE_TLV_PNVM_SKU    80   /* Production NVM SKU */
#define IWL_UCODE_TLV_TYPE_HCMD   84   /* Host command mapping */

/* Firmware TLV header */
typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t length;
    /* data follows */
} iwl_ucode_tlv_t;

/* Firmware file header */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t ver;
    uint32_t size;       /* Size of data after this header */
    /* TLVs follow */
} iwl_ucode_hdr_t;

/* Firmware section descriptor -- for DMA loading */
typedef struct {
    uint64_t phys_addr;   /* Physical address in host memory */
    uint32_t offset;      /* Offset in device SRAM */
    uint32_t len;         /* Section length */
} iwl_fw_section_t;

#define IWL_MAX_FW_SECTIONS  16

/* ====================================================================
 * DMA Descriptors
 *
 * TFD (Transmit Frame Descriptor) -- used for both host commands and
 * TX data frames. Each TFD points to up to 20 transfer buffers (TBs).
 * The TFDs form a circular ring per queue.
 *
 * RBD (Receive Buffer Descriptor) -- points to an RX buffer.
 * The device fills RX buffers with received data and firmware
 * command responses.
 * ==================================================================== */

/* TFD (Transmit Frame Descriptor) -- 128 bytes total */
#define IWL_NUM_TFD_TBS     20   /* Max transfer buffers per TFD */

typedef struct __attribute__((packed)) {
    uint32_t tb_lo;     /* DMA address low 32 bits */
    uint16_t tb_hi_n_len; /* High 4 bits of addr | (length << 4) */
} iwl_tfd_tb_t;

typedef struct __attribute__((packed)) {
    uint8_t     __reserved1[3];
    uint8_t     num_tbs;          /* Number of transfer buffers */
    iwl_tfd_tb_t tbs[IWL_NUM_TFD_TBS];
    uint32_t    __pad;
} iwl_tfd_t;

/* RBD (Receive Buffer Descriptor) -- 8 bytes (gen1), 16 bytes (gen2) */
typedef struct __attribute__((packed)) {
    uint32_t addr_lo;   /* Physical address of RX buffer, low 32 */
    uint32_t addr_hi;   /* Physical address of RX buffer, high 32 */
} iwl_rbd_t;

/* RB Status -- written by device to report completed RX transfers */
typedef struct __attribute__((packed)) {
    uint16_t closed_rb_num;   /* Index of last closed RB */
    uint16_t closed_fr_num;   /* Index of last closed frame */
} iwl_rb_status_t;

/* ====================================================================
 * Host Command Interface
 *
 * Host commands are sent to the firmware through the command queue
 * (TFD queue 4 on gen1/gen2 devices). Each command has an 8-byte
 * header followed by command-specific data.
 *
 * The firmware responds through the RX ring.
 * ==================================================================== */

/* Host command header (sent via TFD queue) */
typedef struct __attribute__((packed)) {
    uint8_t  cmd;             /* Command ID */
    uint8_t  group_id;        /* Command group (0 = legacy) */
    uint8_t  seq;             /* Sequence number (set by driver) */
    uint8_t  flags;           /* Command flags */
    uint16_t length;          /* Data length after this header */
    uint8_t  reserved[2];
    /* Command data follows */
} iwl_host_cmd_hdr_t;

/* Host command flags */
#define IWL_CMD_FLAG_WANT_RESP  (1u << 0)  /* Device should send response */
#define IWL_CMD_FLAG_ASYNC      (1u << 1)  /* Async, don't wait for resp */

/* Common host command IDs (from iwl-commands.h) */
#define IWL_CMD_ALIVE           0x01
#define IWL_CMD_ERROR           0x05
#define IWL_CMD_ECHO            0x06
#define IWL_CMD_RXON            0x10   /* Legacy association */
#define IWL_CMD_RXON_ASSOC      0x11
#define IWL_CMD_QOS_PARAM       0x13
#define IWL_CMD_RXON_TIMING     0x14
#define IWL_CMD_ADD_STA         0x18
#define IWL_CMD_REMOVE_STA      0x19
#define IWL_CMD_TX              0x1C
#define IWL_CMD_TXPOWER         0xC5
#define IWL_CMD_MAC_CONTEXT     0x28
#define IWL_CMD_TIME_EVENT      0x29
#define IWL_CMD_BINDING         0x2B
#define IWL_CMD_PHY_CONTEXT     0x08
#define IWL_CMD_PHY_CONFIG      0x6A
#define IWL_CMD_CALIB_RES       0x68
#define IWL_CMD_CALIB_COMPLETE  0x67
#define IWL_CMD_NIC_CONFIG      0x77
#define IWL_CMD_SCAN_REQ        0x80
#define IWL_CMD_SCAN_ABORT      0x81
#define IWL_CMD_SCAN_COMPLETE   0x84
#define IWL_CMD_NVM_ACCESS      0x88
#define IWL_CMD_POWER_TABLE     0x77
#define IWL_CMD_SET_TX_POWER    0xC5
#define IWL_CMD_BT_CONFIG       0x9B
#define IWL_CMD_STATISTICS      0x9C
#define IWL_CMD_REPLY_RX_PHY    0xC0
#define IWL_CMD_REPLY_RX_MPDU   0xC1
#define IWL_CMD_REPLY_RX        0xC3
#define IWL_CMD_BA_NOTIF        0xC5
#define IWL_CMD_MCC_UPDATE      0xC8   /* Regulatory domain */
#define IWL_CMD_PNVM_INIT       0xF0   /* Production NVM init */

/* RX packet header -- at the start of each RX buffer */
typedef struct __attribute__((packed)) {
    uint32_t len_n_flags;     /* bits 0-13: length, bit 14: padding */
    uint8_t  cmd;             /* Notification/response command ID */
    uint8_t  group_id;
    uint8_t  seq;
    uint8_t  flags;
} iwl_rx_pkt_hdr_t;

/* NVM (Non-Volatile Memory) sections */
#define IWL_NVM_SECTION_HW     0
#define IWL_NVM_SECTION_MAC    1
#define IWL_NVM_SECTION_PHY    2
#define IWL_NVM_SECTION_REG    3   /* Regulatory */
#define IWL_NVM_SECTION_CAL    4

/* ====================================================================
 * TX Command Structures
 *
 * When sending 802.11 frames, the driver builds a TX command
 * (IWL_CMD_TX) with rate, security, and frame metadata, followed
 * by the actual 802.11 frame data.
 * ==================================================================== */

/* TX command -- prepended to TX data frames */
typedef struct __attribute__((packed)) {
    uint16_t len;             /* MAC frame length (bytes) */
    uint16_t next_frame_len;  /* Length of next frame (for A-MPDU) */
    uint32_t tx_flags;        /* TX_CMD_FLG_* */
    uint8_t  rate_n_flags[4]; /* Rate info (OFDM/HT/VHT) */
    uint8_t  sta_id;          /* Station table index */
    uint8_t  sec_ctl;         /* Security control */
    uint8_t  initial_rate_index;
    uint8_t  reserved;
    uint8_t  key[16];         /* Encryption key (for HW crypto) */
    uint16_t next_frame_flags;
    uint16_t reserved2;
    uint32_t life_time;       /* Frame lifetime (usec) */
    uint32_t dram_lsb_ptr;
    uint8_t  dram_msb_ptr;
    uint8_t  rts_retry_limit;
    uint8_t  data_retry_limit;
    uint8_t  tid_tspec;
    uint16_t pm_frame_timeout;
    uint16_t reserved3;
} iwl_tx_cmd_t;

/* TX flags */
#define IWL_TX_CMD_FLG_ACK         (1u << 3)
#define IWL_TX_CMD_FLG_STA_RATE    (1u << 4)
#define IWL_TX_CMD_FLG_MORE_FRAG   (1u << 2)
#define IWL_TX_CMD_FLG_TSF         (1u << 16)

/* ====================================================================
 * Ring Sizes and Queue Configuration
 * ==================================================================== */

#define IWL_CMD_QUEUE          4     /* Queue index for host commands */
#define IWL_CMD_QUEUE_SIZE     32    /* TFDs in command queue */
#define IWL_TX_QUEUE           0     /* Queue index for TX data */
#define IWL_TX_QUEUE_SIZE      32    /* TFDs in TX data queue */
#define IWL_RX_RING_SIZE       64    /* Number of RBDs */
#define IWL_RX_BUF_SIZE        4096  /* Bytes per RX buffer (4KB) */
#define IWL_CMD_BUF_SIZE       320   /* Host command buffer size */
#define IWL_MAX_QUEUES         32    /* Maximum TFD queues */
#define IWL_SCD_MEM_QUEUE_SIZE 256   /* SCD queue context SRAM (DWORDs) */

/* ====================================================================
 * Device State Machine
 * ==================================================================== */

typedef enum {
    IWL_DEV_RESET = 0,        /* After power-on or HW reset */
    IWL_DEV_INIT,             /* Clocks up, BAR mapped, HW rev read */
    IWL_DEV_FW_LOADING,       /* Firmware DMA in progress */
    IWL_DEV_FW_ALIVE,         /* Firmware sent ALIVE notification */
    IWL_DEV_CALIBRATING,      /* Post-init calibration in progress */
    IWL_DEV_OPERATIONAL,      /* Ready for normal operation */
    IWL_DEV_ERROR,            /* Unrecoverable error (needs reset) */
} iwl_dev_state_t;

/* ====================================================================
 * Device Generation
 * ==================================================================== */

typedef enum {
    IWL_GEN_AX200 = 0,        /* Wi-Fi 6 (gen2 transport, 22000 series) */
    IWL_GEN_AX210,             /* Wi-Fi 6E (gen3 / Ty/So transport) */
} iwl_device_gen_t;

/* ====================================================================
 * Device Context
 * ==================================================================== */

typedef struct {
    hal_device_t       dev;
    volatile void     *regs;            /* BAR0 MMIO base */
    uint16_t           device_id;       /* PCIe device ID */
    uint32_t           hw_rev;          /* Hardware revision register */
    uint32_t           hw_rev_step;     /* Silicon stepping */
    iwl_dev_state_t    state;
    iwl_device_gen_t   gen;             /* Device generation */

    /* MAC address from NVM */
    uint8_t            mac[6];

    /* RF Kill status */
    bool               rf_kill;

    /* Command queue (TFD ring) */
    iwl_tfd_t         *cmd_tfd;         /* TFD ring (DMA-coherent) */
    uint64_t           cmd_tfd_phys;
    void              *cmd_bufs[IWL_CMD_QUEUE_SIZE]; /* Command buffers */
    uint64_t           cmd_bufs_phys[IWL_CMD_QUEUE_SIZE];
    uint16_t           cmd_write;       /* Write pointer */
    uint16_t           cmd_read;        /* Read pointer */
    uint8_t            cmd_seq;         /* Sequence counter */

    /* TX data queue (TFD ring) */
    iwl_tfd_t         *tx_tfd;
    uint64_t           tx_tfd_phys;
    void              *tx_bufs[IWL_TX_QUEUE_SIZE];
    uint64_t           tx_bufs_phys[IWL_TX_QUEUE_SIZE];
    uint16_t           tx_write;
    uint16_t           tx_read;

    /* TX byte-count table (for scheduler) */
    uint16_t          *scd_bc_tbl;
    uint64_t           scd_bc_tbl_phys;

    /* RX ring (RBD ring) */
    iwl_rbd_t         *rx_rbd;          /* RBD ring (DMA-coherent) */
    uint64_t           rx_rbd_phys;
    void              *rx_bufs[IWL_RX_RING_SIZE]; /* RX buffers */
    uint64_t           rx_bufs_phys[IWL_RX_RING_SIZE];
    iwl_rb_status_t   *rx_status;       /* RB status (DMA-coherent) */
    uint64_t           rx_status_phys;
    uint16_t           rx_read;         /* Read pointer */

    /* Firmware image (pointer to loaded firmware in memory) */
    const void        *fw_data;
    uint32_t           fw_size;
    uint32_t           fw_inst_size;     /* Instruction section size */
    uint32_t           fw_data_size;     /* Data section size */
    iwl_fw_section_t   fw_sections[IWL_MAX_FW_SECTIONS];
    uint32_t           fw_section_count;

    /* Interrupt coalescing table (for ICT) */
    uint32_t          *ict_table;
    uint64_t           ict_table_phys;
    uint32_t           ict_index;

    /* WiFi framework context */
    wifi_ctx_t         wifi_ctx;

    /* Current channel */
    uint8_t            channel;

    /* Interrupt statistics */
    uint32_t           irq_count;
    uint32_t           rx_irq_count;
    uint32_t           tx_irq_count;

    bool               initialized;
} iwl_dev_t;

/* ====================================================================
 * Public API
 * ==================================================================== */

/* Initialize the Intel WiFi device from a HAL device.
 * Performs:
 *   - PCIe enable + BAR mapping
 *   - NIC reset (SW_RESET, clock init)
 *   - HW_IF_CONFIG programming
 *   - DMA ring allocation (cmd, tx, rx)
 *   - FH DMA engine configuration
 *   - NVM read (MAC address)
 *   - WiFi framework initialization
 *
 * NOTE: firmware must be loaded separately via iwl_load_firmware()
 * before the device can scan or connect.
 */
hal_status_t iwl_init(iwl_dev_t *nic, hal_device_t *dev);

/* Load firmware into the device.
 * fw_data: Pointer to IWL firmware image in memory
 * fw_size: Size of firmware image
 *
 * Parses TLV-format firmware, DMAs sections to device SRAM,
 * and waits for ALIVE notification.
 */
hal_status_t iwl_load_firmware(iwl_dev_t *nic, const void *fw_data,
                                uint32_t fw_size);

/* Send a host command to the firmware via the command queue (TFD queue 4). */
hal_status_t iwl_send_cmd(iwl_dev_t *nic, uint8_t cmd_id,
                            const void *data, uint16_t data_len);

/* Wait for a firmware response on the RX ring. */
hal_status_t iwl_wait_response(iwl_dev_t *nic, void *buf,
                                 uint32_t buf_size, uint32_t timeout_ms);

/* Send a raw 802.11 frame via the TX data queue (TFD queue 0). */
hal_status_t iwl_tx_raw(iwl_dev_t *nic, const void *frame, uint32_t len);

/* Receive a raw 802.11 frame from the RX ring (polling). */
hal_status_t iwl_rx_raw(iwl_dev_t *nic, void *buf, uint32_t *len);

/* Set radio channel via PHY_CONTEXT firmware command. */
hal_status_t iwl_set_channel(iwl_dev_t *nic, uint8_t channel);

/* Read MAC address from NVM via firmware command. */
hal_status_t iwl_read_mac(iwl_dev_t *nic);

/* Get cached MAC address. */
void iwl_get_mac(iwl_dev_t *nic, uint8_t mac[6]);

/* Check if device is operational. */
bool iwl_is_operational(iwl_dev_t *nic);

/* Hardware reset: SW_RESET + clock init + HW_IF_CONFIG. */
hal_status_t iwl_reset(iwl_dev_t *nic);

/* Power management: put device to sleep / wake. */
hal_status_t iwl_power_save(iwl_dev_t *nic, bool enable);

/* Handle interrupt (polled or from ISR). */
void iwl_handle_interrupt(iwl_dev_t *nic);

/* Probe for Intel WiFi devices on the PCIe bus.
 * Returns the number of devices found (0, 1, or more).
 * Fills *dev with the first device found.
 */
uint32_t iwl_probe(hal_device_t *dev);

/* ---- WiFi operations (high-level, delegate to wifi_framework) ---- */

uint32_t     iwl_scan(iwl_dev_t *nic, const uint8_t *channels,
                        uint32_t n_chan, uint32_t dwell_ms);
hal_status_t iwl_connect(iwl_dev_t *nic, const char *ssid,
                           const char *passphrase);
hal_status_t iwl_disconnect(iwl_dev_t *nic);
hal_status_t iwl_send(iwl_dev_t *nic, const void *frame, uint32_t len);
hal_status_t iwl_recv(iwl_dev_t *nic, void *buf, uint32_t *len);

/* ---- driver_ops_t registration ---- */

void intel_wifi_register(void);

#endif /* ALJEFRA_DRV_INTEL_WIFI_H */
