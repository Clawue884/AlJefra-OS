/* SPDX-License-Identifier: MIT */
/* AlJefra OS — AHCI (SATA) Storage Driver
 * Serial ATA Advanced Host Controller Interface.
 * Architecture-independent; uses HAL for all hardware access.
 */

#ifndef ALJEFRA_DRV_AHCI_H
#define ALJEFRA_DRV_AHCI_H

#include "../../hal/hal.h"

/* ── AHCI Generic Host Control registers (offsets from ABAR / BAR5) ── */
#define AHCI_REG_CAP        0x00    /* Host Capabilities */
#define AHCI_REG_GHC        0x04    /* Global Host Control */
#define AHCI_REG_IS         0x08    /* Interrupt Status */
#define AHCI_REG_PI         0x0C    /* Ports Implemented */
#define AHCI_REG_VS         0x10    /* AHCI Version */
#define AHCI_REG_CCC_CTL    0x14    /* Command Completion Coalescing Control */
#define AHCI_REG_CAP2       0x24    /* Host Capabilities Extended */
#define AHCI_REG_BOHC       0x28    /* BIOS/OS Handoff Control */

/* ── GHC bits ── */
#define AHCI_GHC_HR         (1u << 0)   /* HBA Reset */
#define AHCI_GHC_IE         (1u << 1)   /* Interrupt Enable */
#define AHCI_GHC_AE         (1u << 31)  /* AHCI Enable */

/* ── Port register offsets (port base = 0x100 + port * 0x80) ── */
#define AHCI_PORT_CLB       0x00    /* Command List Base (lower 32) */
#define AHCI_PORT_CLBU      0x04    /* Command List Base (upper 32) */
#define AHCI_PORT_FB        0x08    /* FIS Base (lower 32) */
#define AHCI_PORT_FBU       0x0C    /* FIS Base (upper 32) */
#define AHCI_PORT_IS        0x10    /* Interrupt Status */
#define AHCI_PORT_IE        0x14    /* Interrupt Enable */
#define AHCI_PORT_CMD       0x18    /* Command and Status */
#define AHCI_PORT_TFD       0x20    /* Task File Data */
#define AHCI_PORT_SIG       0x24    /* Signature */
#define AHCI_PORT_SSTS      0x28    /* SATA Status (SCR0: SStatus) */
#define AHCI_PORT_SCTL      0x2C    /* SATA Control (SCR2: SControl) */
#define AHCI_PORT_SERR      0x30    /* SATA Error (SCR1: SError) */
#define AHCI_PORT_SACT      0x34    /* SATA Active (NCQ) */
#define AHCI_PORT_CI        0x38    /* Command Issue */

/* ── Port CMD bits ── */
#define AHCI_CMD_ST         (1u << 0)   /* Start */
#define AHCI_CMD_SUD        (1u << 1)   /* Spin-Up Device */
#define AHCI_CMD_POD        (1u << 2)   /* Power On Device */
#define AHCI_CMD_FRE        (1u << 4)   /* FIS Receive Enable */
#define AHCI_CMD_FR         (1u << 14)  /* FIS Receive Running */
#define AHCI_CMD_CR         (1u << 15)  /* Command List Running */

/* ── SATA Status (SStatus) ── */
#define AHCI_SSTS_DET_MASK  0x0F
#define AHCI_SSTS_DET_PRESENT 0x03     /* Device present and PHY established */
#define AHCI_SSTS_IPM_MASK  0x0F00
#define AHCI_SSTS_IPM_ACTIVE 0x0100

/* ── Port signature values ── */
#define AHCI_SIG_SATA       0x00000101  /* SATA drive */
#define AHCI_SIG_ATAPI      0xEB140101  /* SATAPI */
#define AHCI_SIG_SEMB       0xC33C0101  /* Enclosure management bridge */
#define AHCI_SIG_PM         0x96690101  /* Port multiplier */

/* ── ATA commands ── */
#define ATA_CMD_IDENTIFY     0xEC
#define ATA_CMD_READ_DMA_EX  0x25
#define ATA_CMD_WRITE_DMA_EX 0x35
#define ATA_CMD_FLUSH_CACHE  0xE7
#define ATA_CMD_FLUSH_EX     0xEA

/* ── FIS types ── */
#define FIS_TYPE_REG_H2D     0x27    /* Register FIS — Host to Device */
#define FIS_TYPE_REG_D2H     0x34    /* Register FIS — Device to Host */
#define FIS_TYPE_DMA_ACT     0x39    /* DMA Activate FIS */
#define FIS_TYPE_DMA_SETUP   0x41    /* DMA Setup FIS */
#define FIS_TYPE_DATA        0x46    /* Data FIS */
#define FIS_TYPE_PIO_SETUP   0x5F    /* PIO Setup FIS */

/* ── Maximum ports and command slots ── */
#define AHCI_MAX_PORTS       32
#define AHCI_MAX_CMD_SLOTS   32

/* ── Register FIS: Host to Device (20 bytes) ── */
typedef struct __attribute__((packed)) {
    uint8_t  fis_type;      /* FIS_TYPE_REG_H2D */
    uint8_t  pmport : 4;    /* Port multiplier port */
    uint8_t  rsvd0 : 3;
    uint8_t  c : 1;         /* 1 = Command, 0 = Control */
    uint8_t  command;       /* ATA command register */
    uint8_t  featurel;      /* Feature register (7:0) */
    uint8_t  lba0;          /* LBA (7:0) */
    uint8_t  lba1;          /* LBA (15:8) */
    uint8_t  lba2;          /* LBA (23:16) */
    uint8_t  device;        /* Device register */
    uint8_t  lba3;          /* LBA (31:24) */
    uint8_t  lba4;          /* LBA (39:32) */
    uint8_t  lba5;          /* LBA (47:40) */
    uint8_t  featureh;      /* Feature register (15:8) */
    uint16_t count;         /* Sector count */
    uint8_t  icc;           /* Isochronous command completion */
    uint8_t  control;       /* Control register */
    uint32_t rsvd1;         /* Reserved */
} ahci_fis_reg_h2d_t;

/* ── Command Header (32 bytes, one per command slot) ── */
typedef struct __attribute__((packed)) {
    uint8_t  cfl : 5;       /* Command FIS Length (in DWORDs) */
    uint8_t  a : 1;         /* ATAPI */
    uint8_t  w : 1;         /* Write (1 = H2D, 0 = D2H) */
    uint8_t  p : 1;         /* Prefetchable */
    uint8_t  r : 1;         /* Reset */
    uint8_t  b : 1;         /* BIST */
    uint8_t  c : 1;         /* Clear Busy upon R_OK */
    uint8_t  rsvd0 : 1;
    uint8_t  pmp : 4;       /* Port Multiplier Port */
    uint16_t prdtl;         /* Physical Region Descriptor Table Length */
    volatile uint32_t prdbc;/* PRD Byte Count transferred */
    uint32_t ctba;          /* Command Table Base Address (lower 32) */
    uint32_t ctbau;         /* Command Table Base Address (upper 32) */
    uint32_t rsvd1[4];      /* Reserved */
} ahci_cmd_header_t;

/* ── Physical Region Descriptor Table Entry (16 bytes) ── */
typedef struct __attribute__((packed)) {
    uint32_t dba;           /* Data Base Address (lower 32) */
    uint32_t dbau;          /* Data Base Address (upper 32) */
    uint32_t rsvd;          /* Reserved */
    uint32_t dbc : 22;      /* Data Byte Count (0-based, max 4MB) */
    uint32_t rsvd2 : 9;
    uint32_t i : 1;         /* Interrupt on completion */
} ahci_prdt_entry_t;

/* ── Command Table (variable size: 128B header + PRDTs) ── */
typedef struct __attribute__((packed)) {
    uint8_t  cfis[64];      /* Command FIS (up to 64 bytes) */
    uint8_t  acmd[16];      /* ATAPI Command (12/16 bytes) */
    uint8_t  rsvd[48];      /* Reserved */
    ahci_prdt_entry_t prdt[8]; /* PRDT entries (we use up to 8) */
} ahci_cmd_table_t;

/* ── FIS Receive area (256 bytes, per port) ── */
typedef struct __attribute__((packed)) {
    uint8_t  dma_setup[28];     /* DMA Setup FIS */
    uint8_t  rsvd0[4];
    uint8_t  pio_setup[20];     /* PIO Setup FIS */
    uint8_t  rsvd1[12];
    uint8_t  d2h_fis[20];      /* D2H Register FIS */
    uint8_t  rsvd2[4];
    uint8_t  set_dev_bits[8];  /* Set Device Bits FIS */
    uint8_t  ufis[64];         /* Unknown FIS */
    uint8_t  rsvd3[96];
} ahci_fis_recv_t;

/* ── ATA IDENTIFY data (selected fields from 512-byte response) ── */
typedef struct __attribute__((packed)) {
    uint16_t config;            /* Word 0: General config */
    uint16_t rsvd1[9];         /* Words 1-9 */
    char     serial[20];       /* Words 10-19: Serial number */
    uint16_t rsvd2[3];        /* Words 20-22 */
    char     firmware[8];      /* Words 23-26: Firmware revision */
    char     model[40];        /* Words 27-46: Model number */
    uint16_t rsvd3[13];       /* Words 47-59 */
    uint32_t lba28_sectors;    /* Words 60-61: Total 28-bit LBA sectors */
    uint16_t rsvd4[21];       /* Words 62-82 */
    uint16_t features_83;      /* Word 83: Features supported (bit 10 = LBA48) */
    uint16_t rsvd5[16];       /* Words 84-99 */
    uint64_t lba48_sectors;    /* Words 100-103: Total 48-bit LBA sectors */
    uint16_t rsvd6[152];      /* Words 104-255 */
} ahci_identify_t;

/* ── Per-port runtime state ── */
typedef struct {
    volatile void      *port_regs;       /* Port register base (MMIO) */
    ahci_cmd_header_t  *cmd_list;        /* Command list (32 headers, DMA) */
    uint64_t            cmd_list_phys;
    ahci_fis_recv_t    *fis_recv;        /* FIS receive area (DMA) */
    uint64_t            fis_recv_phys;
    ahci_cmd_table_t   *cmd_tables[AHCI_MAX_CMD_SLOTS]; /* Command tables (DMA) */
    uint64_t            cmd_table_phys[AHCI_MAX_CMD_SLOTS];
    uint64_t            total_sectors;   /* Total LBA48 sectors */
    uint32_t            sector_size;     /* Bytes per sector (usually 512) */
    bool                present;         /* Device detected */
    bool                is_atapi;        /* ATAPI device */
} ahci_port_t;

/* ── AHCI HBA state ── */
typedef struct {
    hal_device_t     dev;           /* Underlying HAL device */
    volatile void   *regs;          /* ABAR (BAR5) MMIO base */
    uint32_t         port_count;    /* Number of ports implemented */
    uint32_t         cmd_slots;     /* Command slots per port */
    ahci_port_t      ports[AHCI_MAX_PORTS];
    bool             initialized;
} ahci_hba_t;

/* ── Public API ── */

/* Initialize the AHCI HBA and all detected ports */
hal_status_t ahci_init(ahci_hba_t *hba, hal_device_t *dev);

/* Read `count` sectors starting at `lba` from port `port` into `buf` */
hal_status_t ahci_read(ahci_hba_t *hba, uint32_t port, uint64_t lba,
                       uint32_t count, void *buf, uint64_t buf_phys);

/* Write `count` sectors from `buf` starting at `lba` on port `port` */
hal_status_t ahci_write(ahci_hba_t *hba, uint32_t port, uint64_t lba,
                        uint32_t count, const void *buf, uint64_t buf_phys);

/* Get disk info for a port */
hal_status_t ahci_get_disk_info(ahci_hba_t *hba, uint32_t port,
                                uint64_t *total_sectors, uint32_t *sector_size);

/* Find the first SATA port with a device present. Returns port index or -1. */
int ahci_find_disk(ahci_hba_t *hba);

#endif /* ALJEFRA_DRV_AHCI_H */
