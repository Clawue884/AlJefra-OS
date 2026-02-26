/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Minimal ACPI Table Parser
 * Parses RSDP, RSDT/XSDT, MADT, MCFG, and FADT for platform discovery.
 * Primarily for x86-64 but architecture-independent data structures.
 */

#ifndef ALJEFRA_DRV_ACPI_LITE_H
#define ALJEFRA_DRV_ACPI_LITE_H

#include "../../hal/hal.h"

/* ── ACPI RSDP (Root System Description Pointer) ── */
#define ACPI_RSDP_SIG   "RSD PTR "  /* 8-byte signature */

typedef struct __attribute__((packed)) {
    char     signature[8];      /* "RSD PTR " */
    uint8_t  checksum;          /* ACPI 1.0 checksum (first 20 bytes) */
    char     oem_id[6];
    uint8_t  revision;          /* 0 = ACPI 1.0 (RSDT), 2 = ACPI 2.0+ (XSDT) */
    uint32_t rsdt_address;      /* Physical address of RSDT */
    /* ACPI 2.0+ fields: */
    uint32_t length;            /* Length of this structure */
    uint64_t xsdt_address;      /* Physical address of XSDT (64-bit) */
    uint8_t  ext_checksum;      /* Extended checksum */
    uint8_t  reserved[3];
} acpi_rsdp_t;

/* ── ACPI System Description Table Header (common to all tables) ── */
typedef struct __attribute__((packed)) {
    char     signature[4];      /* Table signature (e.g., "APIC", "MCFG") */
    uint32_t length;            /* Total length including header */
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_sdt_header_t;

/* ── RSDT (Root System Description Table) ── */
typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint32_t          entry[];    /* Array of 32-bit physical addresses */
} acpi_rsdt_t;

/* ── XSDT (Extended System Description Table) ── */
typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint64_t          entry[];    /* Array of 64-bit physical addresses */
} acpi_xsdt_t;

/* ── MADT (Multiple APIC Description Table) / APIC table ── */
#define ACPI_MADT_SIG   "APIC"

typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint32_t          lapic_addr;     /* Local APIC address */
    uint32_t          flags;          /* Bit 0: PCAT_COMPAT (dual 8259 present) */
    /* Followed by variable-length APIC structures */
} acpi_madt_t;

/* MADT entry types */
#define ACPI_MADT_LAPIC         0   /* Processor Local APIC */
#define ACPI_MADT_IOAPIC        1   /* I/O APIC */
#define ACPI_MADT_INT_OVERRIDE  2   /* Interrupt Source Override */
#define ACPI_MADT_NMI_SRC       3   /* NMI Source */
#define ACPI_MADT_LAPIC_NMI     4   /* Local APIC NMI */
#define ACPI_MADT_LAPIC_OVERRIDE 5  /* Local APIC Address Override */
#define ACPI_MADT_X2APIC        9   /* Processor Local x2APIC */

/* MADT entry header */
typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  length;
} acpi_madt_entry_header_t;

/* MADT: Local APIC entry */
typedef struct __attribute__((packed)) {
    acpi_madt_entry_header_t header;
    uint8_t  acpi_processor_id;
    uint8_t  apic_id;
    uint32_t flags;         /* Bit 0: Enabled, Bit 1: Online Capable */
} acpi_madt_lapic_t;

/* MADT: I/O APIC entry */
typedef struct __attribute__((packed)) {
    acpi_madt_entry_header_t header;
    uint8_t  ioapic_id;
    uint8_t  reserved;
    uint32_t ioapic_addr;   /* Physical address */
    uint32_t gsi_base;      /* Global System Interrupt base */
} acpi_madt_ioapic_t;

/* MADT: Interrupt Source Override */
typedef struct __attribute__((packed)) {
    acpi_madt_entry_header_t header;
    uint8_t  bus;           /* Always 0 (ISA) */
    uint8_t  source;        /* ISA IRQ source */
    uint32_t gsi;           /* Global System Interrupt number */
    uint16_t flags;         /* MPS INTI flags */
} acpi_madt_int_override_t;

/* ── MCFG (PCI Express Memory Mapped Configuration Space) ── */
#define ACPI_MCFG_SIG   "MCFG"

typedef struct __attribute__((packed)) {
    uint64_t base_address;      /* ECAM base address */
    uint16_t segment_group;
    uint8_t  start_bus;
    uint8_t  end_bus;
    uint32_t reserved;
} acpi_mcfg_entry_t;

typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint64_t          reserved;
    acpi_mcfg_entry_t entry[];
} acpi_mcfg_t;

/* ── FADT (Fixed ACPI Description Table) ── */
#define ACPI_FADT_SIG   "FACP"

typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint32_t          firmware_ctrl;    /* Physical address of FACS */
    uint32_t          dsdt;             /* Physical address of DSDT */
    uint8_t           reserved1;
    uint8_t           preferred_pm_profile;
    uint16_t          sci_int;          /* SCI interrupt number */
    uint32_t          smi_cmd;          /* SMI command port */
    uint8_t           acpi_enable;
    uint8_t           acpi_disable;
    uint8_t           s4bios_req;
    uint8_t           pstate_cnt;
    uint32_t          pm1a_evt_blk;
    uint32_t          pm1b_evt_blk;
    uint32_t          pm1a_cnt_blk;
    uint32_t          pm1b_cnt_blk;
    uint32_t          pm2_cnt_blk;
    uint32_t          pm_tmr_blk;
    uint32_t          gpe0_blk;
    uint32_t          gpe1_blk;
    uint8_t           pm1_evt_len;
    uint8_t           pm1_cnt_len;
    uint8_t           pm2_cnt_len;
    uint8_t           pm_tmr_len;
    uint8_t           gpe0_blk_len;
    uint8_t           gpe1_blk_len;
    uint8_t           gpe1_base;
    uint8_t           cst_cnt;
    uint16_t          p_lvl2_lat;
    uint16_t          p_lvl3_lat;
    uint16_t          flush_size;
    uint16_t          flush_stride;
    uint8_t           duty_offset;
    uint8_t           duty_width;
    uint8_t           day_alarm;
    uint8_t           month_alarm;
    uint8_t           century;
    uint16_t          iapc_boot_arch;   /* IA-PC Boot Architecture Flags */
    uint8_t           reserved2;
    uint32_t          flags;
    /* ACPI 2.0+ Generic Address Structures follow... */
    uint8_t           reset_reg[12];    /* Generic Address Structure */
    uint8_t           reset_value;
    uint16_t          arm_boot_arch;
    uint8_t           fadt_minor_version;
    uint64_t          x_firmware_ctrl;
    uint64_t          x_dsdt;
    /* More fields follow in later ACPI versions... */
} acpi_fadt_t;

/* ── Maximum parsed entries ── */
#define ACPI_MAX_LAPICS     256
#define ACPI_MAX_IOAPICS    8
#define ACPI_MAX_OVERRIDES  32
#define ACPI_MAX_MCFG       4

/* ── Parsed ACPI data ── */
typedef struct {
    /* RSDP */
    const acpi_rsdp_t   *rsdp;
    bool                 use_xsdt;       /* true = ACPI 2.0+ XSDT */

    /* MADT data */
    uint32_t  lapic_addr;                /* Local APIC base address */
    uint8_t   lapic_ids[ACPI_MAX_LAPICS]; /* APIC IDs of each processor */
    uint32_t  lapic_count;               /* Number of processors */
    uint32_t  ioapic_addr[ACPI_MAX_IOAPICS]; /* I/O APIC addresses */
    uint32_t  ioapic_gsi_base[ACPI_MAX_IOAPICS];
    uint32_t  ioapic_count;

    /* MCFG data (PCIe ECAM) */
    uint64_t  ecam_base;                 /* First ECAM base address */
    uint8_t   ecam_start_bus;
    uint8_t   ecam_end_bus;
    bool      ecam_found;

    /* FADT data */
    uint32_t  pm1a_cnt_blk;             /* PM1a Control Block port */
    uint32_t  sci_int;                   /* SCI interrupt number */
    bool      fadt_found;

    bool      initialized;
} acpi_info_t;

/* ── Public API ── */

/* Initialize ACPI parser. Scans for RSDP and parses all tables.
 * rsdp_hint: physical address hint for RSDP (0 to auto-scan). */
hal_status_t acpi_init(acpi_info_t *acpi, uint64_t rsdp_hint);

/* Get PCIe ECAM base address (from MCFG table) */
hal_status_t acpi_get_ecam(acpi_info_t *acpi, uint64_t *base,
                            uint8_t *start_bus, uint8_t *end_bus);

/* Get Local APIC base address */
uint32_t acpi_get_lapic_addr(acpi_info_t *acpi);

/* Get number of CPUs detected */
uint32_t acpi_get_cpu_count(acpi_info_t *acpi);

/* Shutdown the system via ACPI (write SLP_TYPa | SLP_EN to PM1a_CNT) */
hal_status_t acpi_shutdown(acpi_info_t *acpi);

/* Reboot the system via ACPI reset register */
hal_status_t acpi_reboot(acpi_info_t *acpi);

#endif /* ALJEFRA_DRV_ACPI_LITE_H */
