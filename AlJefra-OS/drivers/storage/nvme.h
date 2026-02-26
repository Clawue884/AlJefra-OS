/* SPDX-License-Identifier: MIT */
/* AlJefra OS — NVMe Storage Driver
 * PCIe-attached NVM Express controller.
 * Architecture-independent; uses HAL for all hardware access.
 */

#ifndef ALJEFRA_DRV_NVME_H
#define ALJEFRA_DRV_NVME_H

#include "../../hal/hal.h"

/* ── NVMe Controller Registers (offsets from BAR0) ── */
#define NVME_REG_CAP        0x0000  /* Controller Capabilities (64-bit) */
#define NVME_REG_VS         0x0008  /* Version (32-bit) */
#define NVME_REG_INTMS      0x000C  /* Interrupt Mask Set */
#define NVME_REG_INTMC      0x0010  /* Interrupt Mask Clear */
#define NVME_REG_CC         0x0014  /* Controller Configuration */
#define NVME_REG_CSTS       0x001C  /* Controller Status */
#define NVME_REG_NSSR       0x0020  /* NVM Subsystem Reset */
#define NVME_REG_AQA        0x0024  /* Admin Queue Attributes */
#define NVME_REG_ASQ        0x0028  /* Admin Submission Queue Base (64-bit) */
#define NVME_REG_ACQ        0x0030  /* Admin Completion Queue Base (64-bit) */

/* ── CC (Controller Configuration) bits ── */
#define NVME_CC_EN          (1u << 0)       /* Enable */
#define NVME_CC_CSS_NVM     (0u << 4)       /* NVM command set */
#define NVME_CC_MPS_SHIFT   7               /* Memory Page Size (2^(12+MPS)) */
#define NVME_CC_AMS_RR      (0u << 11)      /* Round Robin arbitration */
#define NVME_CC_SHN_NONE    (0u << 14)
#define NVME_CC_SHN_NORMAL  (1u << 14)
#define NVME_CC_IOSQES_SHIFT 16             /* I/O SQ Entry Size (2^n) */
#define NVME_CC_IOCQES_SHIFT 20             /* I/O CQ Entry Size (2^n) */

/* ── CSTS (Controller Status) bits ── */
#define NVME_CSTS_RDY       (1u << 0)       /* Ready */
#define NVME_CSTS_CFS       (1u << 1)       /* Controller Fatal Status */
#define NVME_CSTS_SHST_MASK (3u << 2)
#define NVME_CSTS_SHST_NORMAL (0u << 2)
#define NVME_CSTS_SHST_COMPLETE (2u << 2)

/* ── CAP field extraction ── */
#define NVME_CAP_MQES(cap)  ((uint16_t)((cap) & 0xFFFF))          /* Max Queue Entries Supported (0-based) */
#define NVME_CAP_TO(cap)    ((uint8_t)(((cap) >> 24) & 0xFF))     /* Timeout (500ms units) */
#define NVME_CAP_DSTRD(cap) ((uint8_t)(((cap) >> 32) & 0xF))      /* Doorbell Stride (2^(2+DSTRD)) */
#define NVME_CAP_MPSMIN(cap) ((uint8_t)(((cap) >> 48) & 0xF))
#define NVME_CAP_MPSMAX(cap) ((uint8_t)(((cap) >> 52) & 0xF))

/* ── Admin opcodes ── */
#define NVME_ADMIN_DELETE_IO_SQ   0x00
#define NVME_ADMIN_CREATE_IO_SQ   0x01
#define NVME_ADMIN_GET_LOG_PAGE   0x02
#define NVME_ADMIN_DELETE_IO_CQ   0x04
#define NVME_ADMIN_CREATE_IO_CQ   0x05
#define NVME_ADMIN_IDENTIFY       0x06
#define NVME_ADMIN_SET_FEATURES   0x09
#define NVME_ADMIN_GET_FEATURES   0x0A

/* ── NVM (I/O) opcodes ── */
#define NVME_CMD_FLUSH      0x00
#define NVME_CMD_WRITE      0x01
#define NVME_CMD_READ       0x02

/* ── Identify CNS values ── */
#define NVME_IDENTIFY_CTRL  0x01
#define NVME_IDENTIFY_NS    0x00

/* ── Queue sizes ── */
#define NVME_ADMIN_QUEUE_DEPTH  32
#define NVME_IO_QUEUE_DEPTH     64

/* ── Submission Queue Entry (64 bytes) ── */
typedef struct __attribute__((packed)) {
    uint8_t  opc;           /* Opcode */
    uint8_t  fuse : 2;      /* Fused operation */
    uint8_t  rsvd0 : 4;
    uint8_t  psdt : 2;      /* PRP or SGL */
    uint16_t cid;           /* Command ID */
    uint32_t nsid;          /* Namespace ID */
    uint64_t rsvd1;
    uint64_t mptr;          /* Metadata pointer */
    uint64_t prp1;          /* PRP Entry 1 */
    uint64_t prp2;          /* PRP Entry 2 / PRP List pointer */
    uint32_t cdw10;         /* Command-specific DWORDs */
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} nvme_sq_entry_t;

/* ── Completion Queue Entry (16 bytes) ── */
typedef struct __attribute__((packed)) {
    uint32_t cdw0;          /* Command-specific result */
    uint32_t rsvd;
    uint16_t sq_head;       /* SQ Head Pointer */
    uint16_t sq_id;         /* SQ Identifier */
    uint16_t cid;           /* Command ID */
    uint16_t status;        /* Status (bit 0 = Phase Tag, bits 1-15 = status) */
} nvme_cq_entry_t;

/* ── Queue Pair ── */
typedef struct {
    volatile nvme_sq_entry_t *sq;       /* Submission queue (virtual) */
    volatile nvme_cq_entry_t *cq;       /* Completion queue (virtual) */
    uint64_t sq_phys;                    /* SQ physical address */
    uint64_t cq_phys;                    /* CQ physical address */
    volatile uint32_t *sq_doorbell;      /* SQ tail doorbell register */
    volatile uint32_t *cq_doorbell;      /* CQ head doorbell register */
    uint16_t sq_tail;                    /* Current SQ tail */
    uint16_t cq_head;                    /* Current CQ head */
    uint16_t depth;                      /* Queue depth */
    uint8_t  cq_phase;                   /* Expected CQ phase bit */
    uint16_t cid_counter;               /* Rolling command ID */
} nvme_queue_t;

/* ── Identify Controller data (selected fields) ── */
typedef struct __attribute__((packed)) {
    uint16_t vid;           /* PCI Vendor ID */
    uint16_t ssvid;         /* PCI Subsystem Vendor ID */
    char     sn[20];        /* Serial Number */
    char     mn[40];        /* Model Number */
    char     fr[8];         /* Firmware Revision */
    uint8_t  rab;           /* Recommended Arbitration Burst */
    uint8_t  ieee[3];       /* IEEE OUI */
    uint8_t  cmic;
    uint8_t  mdts;          /* Maximum Data Transfer Size (2^n * MPS) */
    uint16_t cntlid;
    uint32_t ver;
    uint8_t  rsvd[172];     /* Pad to offset 256 */
    uint16_t oacs;          /* Optional Admin Commands Supported */
    uint8_t  rsvd2[254];    /* Pad to offset 512 */
    uint8_t  sqes;          /* SQ Entry Size (min/max) */
    uint8_t  cqes;          /* CQ Entry Size (min/max) */
    uint16_t maxcmd;
    uint32_t nn;            /* Number of Namespaces */
    uint8_t  rsvd3[3564];   /* Pad to 4096 */
} nvme_identify_ctrl_t;

/* ── Identify Namespace data (selected fields) ── */
typedef struct __attribute__((packed)) {
    uint64_t nsze;          /* Namespace Size (in logical blocks) */
    uint64_t ncap;          /* Namespace Capacity */
    uint64_t nuse;          /* Namespace Utilization */
    uint8_t  nsfeat;
    uint8_t  nlbaf;         /* Number of LBA Formats (0-based) */
    uint8_t  flbas;         /* Formatted LBA Size (bits 3:0 = format index) */
    uint8_t  rsvd[101];     /* Pad to offset 128 */
    struct __attribute__((packed)) {
        uint16_t ms;        /* Metadata Size */
        uint8_t  lbads;     /* LBA Data Size (2^n) */
        uint8_t  rp;        /* Relative Performance */
    } lbaf[16];             /* LBA Format table */
    uint8_t  rsvd2[3712];   /* Pad to 4096 */
} nvme_identify_ns_t;

/* ── NVMe controller state ── */
typedef struct {
    hal_device_t       dev;             /* Underlying HAL device */
    volatile void      *regs;           /* BAR0 mapped MMIO base */
    uint32_t           db_stride;       /* Doorbell stride in bytes */
    nvme_queue_t       admin_q;         /* Admin queue pair */
    nvme_queue_t       io_q;            /* I/O queue pair (single pair) */
    void              *identify_buf;    /* 4KB DMA buffer for identify */
    uint64_t           identify_phys;
    uint32_t           max_transfer;    /* Max bytes per transfer */
    uint32_t           ns_count;        /* Number of namespaces */
    uint64_t           ns1_blocks;      /* NS1 total blocks */
    uint32_t           ns1_block_size;  /* NS1 block size in bytes */
    bool               initialized;
} nvme_controller_t;

/* ── Public API ── */

/* Initialize the NVMe controller attached to the given HAL device.
 * Returns HAL_OK on success. */
hal_status_t nvme_init(nvme_controller_t *ctrl, hal_device_t *dev);

/* Read `count` blocks starting at `lba` into `buf`.
 * buf must be DMA-capable (use hal_dma_alloc). */
hal_status_t nvme_read(nvme_controller_t *ctrl, uint64_t lba,
                       uint32_t count, void *buf, uint64_t buf_phys);

/* Write `count` blocks from `buf` starting at `lba`. */
hal_status_t nvme_write(nvme_controller_t *ctrl, uint64_t lba,
                        uint32_t count, const void *buf, uint64_t buf_phys);

/* Get namespace 1 info: total block count and block size */
hal_status_t nvme_get_ns_info(nvme_controller_t *ctrl,
                              uint64_t *total_blocks, uint32_t *block_size);

/* Shutdown the controller cleanly */
hal_status_t nvme_shutdown(nvme_controller_t *ctrl);

#endif /* ALJEFRA_DRV_NVME_H */
