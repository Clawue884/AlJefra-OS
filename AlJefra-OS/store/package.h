/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Driver Package Format (.ajdrv)
 *
 * Binary format for distributing portable drivers.
 * Packages are signed with Ed25519 for integrity.
 *
 * Layout:
 *   [Header: 64 bytes]
 *   [Name: variable, null-terminated]
 *   [Description: variable, null-terminated]
 *   [Code: variable, relocatable binary]
 *   [Signature: 64 bytes Ed25519]
 *
 * The signature covers bytes [0..signature_offset).
 */

#ifndef ALJEFRA_PACKAGE_H
#define ALJEFRA_PACKAGE_H

#include <stdint.h>

/* Magic: "AJDV" in little-endian */
#define AJDRV_MAGIC      0x56444A41

/* Current format version */
#define AJDRV_VERSION    1

/* Architecture codes (matches hal_arch_t) */
#define AJDRV_ARCH_X86_64   0
#define AJDRV_ARCH_AARCH64  1
#define AJDRV_ARCH_RISCV64  2
#define AJDRV_ARCH_ANY      0xFF  /* Architecture-independent (scripts, data) */

/* Driver category codes (matches driver_category_t) */
#define AJDRV_CAT_STORAGE   0
#define AJDRV_CAT_NETWORK   1
#define AJDRV_CAT_INPUT     2
#define AJDRV_CAT_DISPLAY   3
#define AJDRV_CAT_GPU       4
#define AJDRV_CAT_BUS       5
#define AJDRV_CAT_OTHER     6

/* Package header (64 bytes, all fields little-endian) */
typedef struct __attribute__((packed)) {
    uint32_t magic;            /* 0x00: AJDRV_MAGIC */
    uint32_t version;          /* 0x04: AJDRV_VERSION */
    uint32_t arch;             /* 0x08: Target architecture */
    uint32_t category;         /* 0x0C: Driver category */
    uint32_t code_offset;      /* 0x10: Offset to executable code */
    uint32_t code_size;        /* 0x14: Size of executable code */
    uint32_t name_offset;      /* 0x18: Offset to name string */
    uint32_t name_size;        /* 0x1C: Size of name (including null) */
    uint32_t desc_offset;      /* 0x20: Offset to description string */
    uint32_t desc_size;        /* 0x24: Size of description */
    uint32_t entry_offset;     /* 0x28: Offset within code to entry function */
    uint32_t signature_offset; /* 0x2C: Offset to Ed25519 signature (64 bytes) */
    uint16_t vendor_id;        /* 0x30: PCI vendor ID this driver supports */
    uint16_t device_id;        /* 0x32: PCI device ID (0xFFFF = any) */
    uint16_t min_os_version;   /* 0x34: Minimum OS version (major.minor) */
    uint16_t flags;            /* 0x36: Package flags */
    uint8_t  reserved[8];      /* 0x38: Reserved for future use */
} ajdrv_header_t;                /* Total: 64 bytes (0x40) */

/* Package flags */
#define AJDRV_FLAG_ESSENTIAL   (1u << 0)  /* Required for boot */
#define AJDRV_FLAG_OPTIONAL    (1u << 1)  /* Can be unloaded */
#define AJDRV_FLAG_BETA        (1u << 2)  /* Not production-ready */

/* Ed25519 signature size */
#define AJDRV_SIGNATURE_SIZE  64

/* Ed25519 public key size */
#define AJDRV_PUBKEY_SIZE     32

/* Validate a package header (checks magic, version, field bounds) */
static inline int ajdrv_validate_header(const ajdrv_header_t *hdr, uint64_t total_size)
{
    if (hdr->magic != AJDRV_MAGIC) return -1;
    if (hdr->version == 0 || hdr->version > AJDRV_VERSION) return -2;
    if (hdr->code_offset + hdr->code_size > total_size) return -3;
    if (hdr->name_offset + hdr->name_size > total_size) return -4;
    if (hdr->signature_offset + AJDRV_SIGNATURE_SIZE > total_size) return -5;
    if (hdr->entry_offset >= hdr->code_size) return -6;
    return 0; /* Valid */
}

#endif /* ALJEFRA_PACKAGE_H */
