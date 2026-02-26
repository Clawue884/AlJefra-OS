/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Driver Installation */

#ifndef ALJEFRA_INSTALL_H
#define ALJEFRA_INSTALL_H

#include <stdint.h>
#include "../hal/hal.h"

/* Install a driver from an .ajdrv package buffer.
 * Verifies signature, checks architecture, loads the driver.
 * dev can be NULL if the device is not yet known.
 */
hal_status_t ajdrv_install(const void *data, uint64_t size, hal_device_t *dev);

/* Install a driver from storage (by filename in BMFS) */
hal_status_t ajdrv_install_from_storage(const char *filename, hal_device_t *dev);

/* OTA kernel update: download kernel binary from URL, verify signature,
 * write to storage at the OTA staging sector.
 * Returns HAL_OK if update was downloaded and staged.
 * Actual apply happens on next reboot (boot loader checks staging area).
 */
hal_status_t ota_download_update(const char *url, const char *version);

/* Check if a staged OTA update exists and apply it.
 * Called early in boot before kernel_main().
 * Returns HAL_OK if update was applied (reboot recommended).
 */
hal_status_t ota_check_and_apply(void);

/* OTA staging area: reserved sectors on storage device */
#define OTA_STAGING_LBA     0x10000    /* 256MB offset (512-byte sectors) */
#define OTA_STAGING_SECTORS  4096      /* 2MB max kernel image */
#define OTA_MAGIC           0x4F544155 /* "OTAU" */

/* OTA staging header (written to first sector of staging area) */
typedef struct {
    uint32_t magic;         /* OTA_MAGIC */
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;
    uint32_t kernel_size;   /* Size of kernel binary in bytes */
    uint32_t kernel_crc32;  /* CRC32 for integrity check */
    uint8_t  signature[64]; /* Ed25519 signature of kernel binary */
    uint8_t  status;        /* 0=pending, 1=applied, 2=failed */
    uint8_t  reserved[3];
} ota_staging_header_t;

#endif /* ALJEFRA_INSTALL_H */
