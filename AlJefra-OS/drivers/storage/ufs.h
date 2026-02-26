/* SPDX-License-Identifier: MIT */
/* AlJefra OS — UFS (Universal Flash Storage) Driver
 * UFSHCI (UFS Host Controller Interface) compliant driver.
 * Architecture-independent; uses HAL for all hardware access.
 *
 * UFS is the standard storage interface on modern smartphones and tablets
 * (Samsung, Qualcomm Snapdragon, MediaTek Dimensity, Google Tensor).
 *
 * Supports UFS 2.x and 3.x via the JEDEC UFSHCI specification.
 * SCSI command layer over UFS Transport Protocol (UTP).
 */

#ifndef ALJEFRA_DRV_UFS_H
#define ALJEFRA_DRV_UFS_H

#include "../../hal/hal.h"

/* ── UFSHCI Register Offsets (from BAR0) ── */

#define UFSHCI_CAP         0x00    /* Controller Capabilities (32-bit) */
#define UFSHCI_VER         0x08    /* UFS Version (32-bit) */
#define UFSHCI_HCDDID      0x10    /* Host Controller ID / Device ID (32-bit) */
#define UFSHCI_HCPMID      0x14    /* Host Controller PID / MID (32-bit) */
#define UFSHCI_IS          0x20    /* Interrupt Status (32-bit) */
#define UFSHCI_IE          0x24    /* Interrupt Enable (32-bit) */
#define UFSHCI_HCS         0x30    /* Host Controller Status (32-bit) */
#define UFSHCI_HCE         0x34    /* Host Controller Enable (32-bit) */
#define UFSHCI_UECPA       0x38    /* UIC Error Code PHY Adapter Layer (32-bit) */
#define UFSHCI_UECDL       0x3C    /* UIC Error Code Data Link Layer (32-bit) */
#define UFSHCI_UECN        0x40    /* UIC Error Code Network Layer (32-bit) */
#define UFSHCI_UECT        0x44    /* UIC Error Code Transport Layer (32-bit) */
#define UFSHCI_UECDME      0x48    /* UIC Error Code DME (32-bit) */
#define UFSHCI_UTRLBA      0x50    /* UTP Transfer Request List Base Address (32-bit) */
#define UFSHCI_UTRLBAU     0x54    /* UTP Transfer Request List Base Address Upper (32-bit) */
#define UFSHCI_UTRLDBR     0x58    /* UTP Transfer Request List Door Bell Register (32-bit) */
#define UFSHCI_UTRLCLR     0x5C    /* UTP Transfer Request List Clear Register (32-bit) */
#define UFSHCI_UTRLRSR     0x60    /* UTP Transfer Request List Run-Stop Register (32-bit) */
#define UFSHCI_UTMRLBA     0x70    /* UTP Task Management Request List Base Address (32-bit) */
#define UFSHCI_UTMRLBAU    0x74    /* UTP Task Management Request List Base Address Upper (32-bit) */
#define UFSHCI_UTMRLDBR    0x78    /* UTP Task Management Request List Door Bell Register (32-bit) */
#define UFSHCI_UTMRLCLR    0x7C    /* UTP Task Management Request List Clear Register (32-bit) */
#define UFSHCI_UTMRLRSR    0x80    /* UTP Task Management Request List Run-Stop Register (32-bit) */
#define UFSHCI_UICCMD      0x90    /* UIC Command (32-bit) */
#define UFSHCI_UCMDARG1    0x94    /* UIC Command Argument 1 (32-bit) */
#define UFSHCI_UCMDARG2    0x98    /* UIC Command Argument 2 (32-bit) */
#define UFSHCI_UCMDARG3    0x9C    /* UIC Command Argument 3 (32-bit) */

/* ── Controller Capabilities (UFSHCI_CAP) bits ── */

#define UFSHCI_CAP_NUTRS_MASK  0x0000001F   /* Number of UTP Transfer Request Slots (0-based) */
#define UFSHCI_CAP_NUTMRS_MASK 0x00070000   /* Number of UTP Task Management Request Slots */
#define UFSHCI_CAP_NUTMRS_SHIFT 16
#define UFSHCI_CAP_AUTOH8      (1u << 23)   /* Auto-Hibernate support */
#define UFSHCI_CAP_64AS        (1u << 24)   /* 64-bit Addressing Supported */
#define UFSHCI_CAP_OODDS       (1u << 25)   /* Out of Order Data Delivery */

/* ── Interrupt Status / Enable (UFSHCI_IS / UFSHCI_IE) bits ── */

#define UFSHCI_IS_UTRCS    (1u << 0)    /* UTP Transfer Request Completion Status */
#define UFSHCI_IS_UDEPRI   (1u << 1)    /* UIC DME Endpoint Reset Indication */
#define UFSHCI_IS_UE       (1u << 2)    /* UIC Error */
#define UFSHCI_IS_UTMS     (1u << 3)    /* UIC Test Mode Status */
#define UFSHCI_IS_UPMS     (1u << 4)    /* UIC Power Mode Status */
#define UFSHCI_IS_UHXS     (1u << 5)    /* UIC Hibernate Exit Status */
#define UFSHCI_IS_UHES     (1u << 6)    /* UIC Hibernate Enter Status */
#define UFSHCI_IS_ULLS     (1u << 7)    /* UIC Link Lost Status */
#define UFSHCI_IS_ULSS     (1u << 8)    /* UIC Link Startup Status */
#define UFSHCI_IS_UTMRCS   (1u << 9)    /* UTP Task Management Request Completion Status */
#define UFSHCI_IS_UCCS     (1u << 10)   /* UIC Command Completion Status */
#define UFSHCI_IS_DFES     (1u << 11)   /* Device Fatal Error Status */
#define UFSHCI_IS_UTPES    (1u << 12)   /* UTP Error Status */
#define UFSHCI_IS_HCFES    (1u << 16)   /* Host Controller Fatal Error Status */
#define UFSHCI_IS_SBFES    (1u << 17)   /* System Bus Fatal Error Status */
#define UFSHCI_IS_CEFES    (1u << 18)   /* Crypto Engine Fatal Error Status */

/* ── Host Controller Status (UFSHCI_HCS) bits ── */

#define UFSHCI_HCS_DP      (1u << 0)    /* Device Present */
#define UFSHCI_HCS_UTRLRDY (1u << 1)    /* UTP Transfer Request List Ready */
#define UFSHCI_HCS_UTMRLRDY (1u << 2)   /* UTP Task Management Request List Ready */
#define UFSHCI_HCS_UCRDY   (1u << 3)    /* UIC Command Ready */
#define UFSHCI_HCS_UPMCRS_MASK  (7u << 8)  /* UIC Power Mode Change Request Status */

/* ── Host Controller Enable (UFSHCI_HCE) ── */

#define UFSHCI_HCE_HCE     (1u << 0)    /* Host Controller Enable */
#define UFSHCI_HCE_CGE     (1u << 1)    /* Crypto General Enable */

/* ── UIC Commands ── */

#define UIC_CMD_DME_GET       0x01
#define UIC_CMD_DME_SET       0x02
#define UIC_CMD_DME_PEER_GET  0x03
#define UIC_CMD_DME_PEER_SET  0x04
#define UIC_CMD_DME_POWERON   0x10
#define UIC_CMD_DME_POWEROFF  0x11
#define UIC_CMD_DME_ENABLE    0x12
#define UIC_CMD_DME_RESET     0x14
#define UIC_CMD_DME_ENDPTRESET 0x15
#define UIC_CMD_DME_LINK_STARTUP 0x16
#define UIC_CMD_DME_HIBERNATE_ENTER  0x17
#define UIC_CMD_DME_HIBERNATE_EXIT   0x18
#define UIC_CMD_DME_TEST_MODE 0x1A

/* ── UniPro DME Attributes ── */

#define DME_LOCAL             0   /* Local attribute access */
#define DME_PEER              1   /* Peer attribute access */

#define PA_AVAIL_TX_DATA_LANES    0x1520
#define PA_AVAIL_RX_DATA_LANES    0x1540
#define PA_ACTIVE_TX_DATA_LANES   0x1560
#define PA_ACTIVE_RX_DATA_LANES   0x1580
#define PA_TX_GEAR                0x1568
#define PA_RX_GEAR                0x1583
#define PA_TX_TERMINATION         0x1569
#define PA_RX_TERMINATION         0x1584
#define PA_HS_SERIES              0x156A
#define PA_PWR_MODE               0x1571
#define PA_MAX_RX_HS_GEAR         0x1587
#define PA_CONNECTED_TX_DATA_LANES 0x1561
#define PA_CONNECTED_RX_DATA_LANES 0x1581
#define DME_FC0_PROTECTION_TIMEOUT 0xD041
#define DME_TC0_REPLAY_TIMEOUT    0xD042

/* ── SCSI Constants ── */

#define SCSI_OP_TEST_UNIT_READY  0x00
#define SCSI_OP_REQUEST_SENSE    0x03
#define SCSI_OP_INQUIRY          0x12
#define SCSI_OP_READ_CAPACITY_10 0x25
#define SCSI_OP_READ_10          0x28
#define SCSI_OP_WRITE_10         0x2A
#define SCSI_OP_SYNCHRONIZE_CACHE 0x35
#define SCSI_OP_READ_CAPACITY_16 0x9E
#define SCSI_OP_REPORT_LUNS      0xA0

/* ── UFS Query Request / Response ── */

/* Query opcode */
#define UPIU_QUERY_OP_READ_DESC   0x01
#define UPIU_QUERY_OP_WRITE_DESC  0x02
#define UPIU_QUERY_OP_READ_ATTR   0x03
#define UPIU_QUERY_OP_WRITE_ATTR  0x04
#define UPIU_QUERY_OP_READ_FLAG   0x05
#define UPIU_QUERY_OP_SET_FLAG    0x06
#define UPIU_QUERY_OP_CLEAR_FLAG  0x07
#define UPIU_QUERY_OP_TOGGLE_FLAG 0x08

/* Query descriptor IDN */
#define UFS_DESC_IDN_DEVICE      0x00
#define UFS_DESC_IDN_CONFIGURATION 0x01
#define UFS_DESC_IDN_UNIT        0x02
#define UFS_DESC_IDN_INTERCONNECT 0x04
#define UFS_DESC_IDN_STRING      0x05
#define UFS_DESC_IDN_GEOMETRY    0x07
#define UFS_DESC_IDN_POWER       0x08
#define UFS_DESC_IDN_HEALTH      0x09

/* Query flags IDN */
#define UFS_FLAG_DEVICE_INIT     0x01   /* fDeviceInit */
#define UFS_FLAG_PERMANENT_WPE   0x02   /* fPermanentWPEn */
#define UFS_FLAG_POWER_ON_WPE    0x03   /* fPowerOnWPEn */
#define UFS_FLAG_PURGE_ENABLE    0x06   /* fPurgeEnable */

/* Query attributes IDN */
#define UFS_ATTR_BOOT_LUN_EN     0x00
#define UFS_ATTR_CURRENT_POWER_MODE 0x02
#define UFS_ATTR_ACTIVE_ICC_LEVEL 0x03
#define UFS_ATTR_BKOPS_STATUS    0x05

/* ── UPIU (UFS Protocol Information Unit) ── */

/* Transaction types */
#define UPIU_TRANS_NOP_OUT       0x00
#define UPIU_TRANS_COMMAND       0x01
#define UPIU_TRANS_DATA_OUT      0x02
#define UPIU_TRANS_TASK_MGMT     0x04
#define UPIU_TRANS_QUERY_REQ     0x16
#define UPIU_TRANS_NOP_IN        0x20
#define UPIU_TRANS_RESPONSE      0x21
#define UPIU_TRANS_DATA_IN       0x22
#define UPIU_TRANS_TASK_MGMT_RSP 0x24
#define UPIU_TRANS_READY_TO_XFER 0x31
#define UPIU_TRANS_QUERY_RSP     0x36
#define UPIU_TRANS_REJECT        0x3F

/* Task attributes */
#define UPIU_TASK_ATTR_SIMPLE    0x00
#define UPIU_TASK_ATTR_ORDERED   0x01
#define UPIU_TASK_ATTR_HEAD      0x02

/* Command set type */
#define UPIU_COMMAND_SET_SCSI    0x00

/* Data direction flags (in OCS) */
#define UTP_NO_DATA_TRANSFER     0x00
#define UTP_HOST_TO_DEVICE       0x02
#define UTP_DEVICE_TO_HOST       0x04

/* OCS (Overall Command Status) values */
#define OCS_SUCCESS              0x00
#define OCS_INVALID_CMD_TABLE    0x01
#define OCS_INVALID_PRDT_ATTR    0x02
#define OCS_MISMATCH_DATA_SIZE   0x03
#define OCS_MISMATCH_RESP_SIZE   0x04
#define OCS_COMM_FAILURE         0x05
#define OCS_ABORTED              0x06
#define OCS_FATAL_ERROR          0x07
#define OCS_DEVICE_FATAL         0x08
#define OCS_INVALID_CRYPTO_CFG   0x09
#define OCS_GENERAL_CRYPTO_ERR   0x0A
#define OCS_INVALID_COMMAND      0x0F

/* ── UPIU Header (12 bytes, used in both request and response) ── */

typedef struct __attribute__((packed)) {
    uint8_t  trans_type;       /* Transaction type + flags */
    uint8_t  flags;            /* Flags (Read/Write) */
    uint8_t  lun;              /* Logical Unit Number */
    uint8_t  task_tag;         /* Task Tag (command ID) */
    uint8_t  cmd_set_type;     /* Command Set Type (0 = SCSI) */
    uint8_t  query_func;      /* Query function / TM function */
    uint8_t  response;         /* Response code */
    uint8_t  status;           /* SCSI status (in response) */
    uint8_t  ehs_length;       /* Extra Header Segment Length */
    uint8_t  device_info;      /* Device information */
    uint16_t data_segment_len; /* Data segment length (big-endian) */
} upiu_header_t;

/* ── UPIU Command (Request UPIU for SCSI commands) ── */
/* Total: 32 bytes (header 12 + expected data transfer length 4 + CDB 16) */

typedef struct __attribute__((packed)) {
    upiu_header_t hdr;
    uint32_t      exp_data_xfer_len;   /* Expected data transfer length (big-endian) */
    uint8_t       cdb[16];              /* SCSI CDB */
} upiu_cmd_t;

/* ── UPIU Response ── */

typedef struct __attribute__((packed)) {
    upiu_header_t hdr;
    uint32_t      residual_xfer_count;  /* Residual transfer count (big-endian) */
    uint8_t       rsvd[16];             /* Reserved */
} upiu_response_t;

/* ── UPIU Query Request/Response ── */

typedef struct __attribute__((packed)) {
    upiu_header_t hdr;
    uint8_t       opcode;       /* Query opcode */
    uint8_t       idn;          /* Descriptor IDN / Flag IDN / Attribute IDN */
    uint8_t       index;        /* Index */
    uint8_t       selector;     /* Selector */
    uint16_t      rsvd1;
    uint16_t      length;       /* Length (big-endian) */
    uint32_t      value;        /* Value (big-endian, for attributes) */
    uint32_t      rsvd2;
} upiu_query_t;

/* ── Physical Region Descriptor Table Entry (PRDT) ──
 * Used for scatter-gather DMA. Each entry describes one
 * physically contiguous memory region. */

typedef struct __attribute__((packed)) {
    uint32_t base_addr;        /* Data base address (lower 32 bits) */
    uint32_t base_addr_upper;  /* Data base address (upper 32 bits) */
    uint32_t rsvd;
    uint32_t size;             /* Data byte count (bits 17:0, 0-based) */
} ufs_prdt_entry_t;

/* ── UTP Transfer Request Descriptor (UTRD) ──
 * 32 bytes. Points to the command UPIU, response UPIU, and PRDT. */

typedef struct __attribute__((packed)) {
    /* DW0: Header */
    uint32_t dw0;
    /* Bits [31:28] = Command Type (1=SCSI, 2=NOP_OUT, 6=Query)
     * Bits [27:26] = Data Direction (00=No data, 01=Write, 10=Read)
     * Bits [25:24] = Interrupt
     * Bits [23:0]  = Reserved */

    /* DW1: Reserved / DUNL */
    uint32_t dw1;

    /* DW2: OCS (Overall Command Status) */
    uint32_t ocs;

    /* DW3: Reserved / DUNU */
    uint32_t dw3;

    /* DW4-5: UTP Command Descriptor Base Address (64-bit, must be 128-byte aligned) */
    uint32_t ucdba;            /* Lower 32 bits */
    uint32_t ucdbau;           /* Upper 32 bits */

    /* DW6-7: Response UPIU offset and PRDT offset/length */
    uint16_t resp_upiu_offset; /* Offset of response UPIU in UCD (in DWORDs) */
    uint16_t resp_upiu_length; /* Length of response UPIU (in DWORDs) */
    uint16_t prdt_offset;      /* Offset of PRDT in UCD (in DWORDs) */
    uint16_t prdt_length;      /* Number of PRDT entries */
} ufs_utrd_t;

/* ── UTP Task Management Request Descriptor (UTMRD) ── */

typedef struct __attribute__((packed)) {
    /* DW0: Header */
    uint32_t dw0;
    /* Bits [31:28] = Reserved
     * Bit [24] = Interrupt */

    /* DW1-3: Reserved */
    uint32_t rsvd[3];

    /* DW4-11: Task Management Request UPIU (32 bytes) */
    upiu_header_t req_hdr;
    uint32_t      input_param1;
    uint32_t      input_param2;
    uint32_t      input_param3;
    uint32_t      rsvd2[2];

    /* DW12-19: Task Management Response UPIU (32 bytes) */
    upiu_header_t rsp_hdr;
    uint32_t      output_param1;
    uint32_t      output_param2;
    uint32_t      rsvd3[3];
} ufs_utmrd_t;

/* ── UTP Command Descriptor (UCD) ──
 * Contains the command UPIU, response UPIU space, and PRDT.
 * Must be 128-byte aligned. */

/* Maximum PRDT entries per command (supports up to 256KB per transfer
 * with 4KB entries, or 1MB with 64KB entries) */
#define UFS_MAX_PRDT_ENTRIES   64

typedef struct __attribute__((packed, aligned(128))) {
    /* Command UPIU (32 bytes) */
    upiu_cmd_t     cmd_upiu;

    /* Padding to 64 bytes */
    uint8_t        rsvd_cmd[32];

    /* Response UPIU space (32 bytes) */
    upiu_response_t rsp_upiu;

    /* Padding to 128 bytes */
    uint8_t        rsvd_rsp[32];

    /* PRDT (each entry = 16 bytes) */
    ufs_prdt_entry_t prdt[UFS_MAX_PRDT_ENTRIES];
} ufs_ucd_t;

/* ── UFS Device Descriptor (selected fields) ── */

typedef struct __attribute__((packed)) {
    uint8_t  bLength;                  /* Descriptor length */
    uint8_t  bDescriptorIDN;           /* Descriptor IDN (0x00) */
    uint8_t  bDevice;                  /* Device type */
    uint8_t  bDeviceClass;             /* Device class */
    uint8_t  bDeviceSubClass;
    uint8_t  bProtocol;
    uint8_t  bNumberLU;                /* Number of logical units */
    uint8_t  bNumberWLU;               /* Number of well-known LUs */
    uint8_t  bBootEnable;
    uint8_t  bDescrAccessEn;
    uint8_t  bInitPowerMode;
    uint8_t  bHighPriorityLUN;
    uint8_t  bSecureRemovalType;
    uint8_t  bSecurityLU;
    uint8_t  bBackgroundOpsTermLat;
    uint8_t  bInitActiveICCLevel;
    uint16_t wSpecVersion;             /* UFS spec version (big-endian) */
    uint16_t wManufactureDate;
    uint8_t  iManufacturerName;        /* String descriptor index */
    uint8_t  iProductName;
    uint8_t  iSerialNumber;
    uint8_t  iOemID;
    uint16_t wManufacturerID;          /* JEDEC manufacturer ID */
    uint8_t  bUD0BaseOffset;
    uint8_t  bUDConfigPLength;
    uint8_t  bDeviceRTTCap;
    uint16_t wPeriodicRTCUpdate;
    uint8_t  bUFSFeaturesSupport;
    uint8_t  bFFUTimeout;
    uint8_t  bQueueDepth;              /* Queue depth supported */
    uint16_t wDeviceVersion;
    uint8_t  bNumSecureWPArea;
    uint32_t dPSAMaxDataSize;
    uint8_t  bPSAStateTimeout;
    uint8_t  iProductRevisionLevel;
    uint8_t  rsvd[36];                 /* Pad to common size */
} ufs_device_desc_t;

/* ── Queue sizes ── */

#define UFS_MAX_TRANSFER_SLOTS  32     /* Maximum UTRD slots */
#define UFS_MAX_TASK_MGMT_SLOTS  8    /* Maximum UTMRD slots */
#define UFS_BLOCK_SIZE          4096   /* UFS logical block size */
#define UFS_TIMEOUT_MS          5000   /* Command timeout */
#define UFS_POLL_US             100    /* Polling interval */

/* ── UFS Controller State ── */

typedef struct {
    hal_device_t       dev;              /* Underlying HAL device */
    volatile void     *regs;             /* BAR0 mapped MMIO base */

    /* Capabilities */
    uint32_t           nutrs;            /* Number of transfer request slots */
    uint32_t           nutmrs;           /* Number of task management request slots */
    bool               addr_64bit;       /* 64-bit addressing supported */

    /* UTP Transfer Request List */
    ufs_utrd_t        *utrl;             /* Transfer request descriptor array (DMA) */
    uint64_t           utrl_phys;        /* Physical address of UTRL */

    /* UTP Command Descriptors (one per slot) */
    ufs_ucd_t         *ucds;             /* Command descriptors (DMA) */
    uint64_t           ucds_phys;        /* Physical address of UCDs */

    /* UTP Task Management Request List */
    ufs_utmrd_t       *utmrl;           /* Task management descriptors (DMA) */
    uint64_t           utmrl_phys;       /* Physical address of UTMRL */

    /* DMA buffer for queries/descriptors */
    void              *query_buf;        /* 4KB query buffer */
    uint64_t           query_buf_phys;

    /* DMA buffer for data transfers */
    void              *data_buf;         /* 64KB data buffer */
    uint64_t           data_buf_phys;

    /* Rolling task tag counter */
    uint8_t            task_tag;

    /* Device info (from device descriptor) */
    ufs_device_desc_t  dev_desc;
    uint8_t            num_luns;         /* Number of logical units */
    uint64_t           lun0_blocks;      /* LUN 0 total blocks */
    uint32_t           lun0_block_size;  /* LUN 0 block size */

    /* UFS spec version */
    uint16_t           spec_version;

    bool               initialized;
} ufs_controller_t;

/* ── Public API ── */

/* Initialize UFS controller from a HAL device.
 * Performs host controller enable, link startup, device initialization,
 * and reads device descriptor and LUN 0 capacity. */
hal_status_t ufs_init(ufs_controller_t *ctrl, hal_device_t *dev);

/* Read `count` blocks starting at `lba` from LUN 0 into `buf`.
 * buf must be DMA-capable (from hal_dma_alloc). buf_phys is its physical address. */
hal_status_t ufs_read(ufs_controller_t *ctrl, uint64_t lba,
                       uint32_t count, void *buf, uint64_t buf_phys);

/* Write `count` blocks from `buf` starting at `lba` to LUN 0.
 * buf must be DMA-capable. */
hal_status_t ufs_write(ufs_controller_t *ctrl, uint64_t lba,
                        uint32_t count, const void *buf, uint64_t buf_phys);

/* Query a UFS device descriptor.
 * idn: descriptor IDN (UFS_DESC_IDN_*).
 * index: descriptor index.
 * buf: output buffer, buf_len: buffer size.
 * Returns HAL_OK on success. */
hal_status_t ufs_query_descriptor(ufs_controller_t *ctrl, uint8_t idn,
                                   uint8_t index, void *buf, uint16_t buf_len);

/* Read a UFS attribute.
 * idn: attribute IDN.
 * value: output value. */
hal_status_t ufs_read_attribute(ufs_controller_t *ctrl, uint8_t idn,
                                 uint32_t *value);

/* Get LUN 0 info: total block count and block size. */
hal_status_t ufs_get_lun_info(ufs_controller_t *ctrl,
                               uint64_t *total_blocks, uint32_t *block_size);

/* Shutdown the controller cleanly (power down UFS device). */
hal_status_t ufs_shutdown(ufs_controller_t *ctrl);

/* Register UFS driver with the driver_loader system. */
void ufs_register(void);

#endif /* ALJEFRA_DRV_UFS_H */
