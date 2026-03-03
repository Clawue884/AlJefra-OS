/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Driver Installation Implementation */

#include "install.h"
#include "verify.h"
#include "package.h"
#include "../kernel/driver_loader.h"
#include "../hal/hal.h"

hal_status_t ajdrv_install(const void *data, uint64_t size, hal_device_t *dev)
{
    /* Step 1: Verify signature */
    hal_status_t rc = ajdrv_verify(data, size);
    if (rc != HAL_OK) {
        hal_console_puts("[install] Signature verification failed\n");
        return rc;
    }

    /* Step 2: Check architecture */
    const ajdrv_header_t *hdr = (const ajdrv_header_t *)data;
    if (hdr->arch != AJDRV_ARCH_ANY && hdr->arch != (uint32_t)hal_arch()) {
        hal_console_puts("[install] Architecture mismatch\n");
        return HAL_NOT_SUPPORTED;
    }

    /* Step 3: Load the driver binary */
    rc = driver_load_runtime(data, size, dev);
    if (rc != HAL_OK) {
        hal_console_puts("[install] Driver load failed\n");
        return rc;
    }

    const char *name = (const char *)data + hdr->name_offset;
    hal_console_printf("[install] Successfully installed '%s'\n", name);
    return HAL_OK;
}

hal_status_t ajdrv_install_from_storage(const char *filename, hal_device_t *dev)
{
    /* Read the file from storage via the active storage driver */
    const driver_ops_t *stor = driver_get_storage();
    if (!stor || !stor->read) {
        hal_console_puts("[install] No storage driver available\n");
        return HAL_NO_DEVICE;
    }

    /* Allocate a buffer for the driver package (max 256KB) */
    uint64_t phys;
    uint64_t max_size = 256 * 1024;
    void *buf = hal_dma_alloc(max_size, &phys);
    if (!buf)
        return HAL_NO_MEMORY;

    /* TODO: Implement BMFS file lookup by name
     * For now, this would need filesystem support to translate
     * filename → LBA offset and size.
     */

    hal_console_printf("[install] TODO: Load '%s' from storage\n", filename);

    hal_dma_free(buf, max_size);
    return HAL_NOT_SUPPORTED;
}

/* ── CRC-32 (IEEE 802.3) for OTA integrity ── */

static uint32_t crc32_byte(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (int i = 0; i < 8; i++) {
        if (crc & 1)
            crc = (crc >> 1) ^ 0xEDB88320;
        else
            crc >>= 1;
    }
    return crc;
}

static uint32_t crc32_buf(const void *data, uint64_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *p = (const uint8_t *)data;
    for (uint64_t i = 0; i < len; i++)
        crc = crc32_byte(crc, p[i]);
    return crc ^ 0xFFFFFFFF;
}

/* ── Parse version string "major.minor.patch" ── */

static void parse_version(const char *ver, uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    *major = *minor = *patch = 0;
    uint32_t *cur = major;
    while (*ver) {
        if (*ver == '.') {
            if (cur == major) cur = minor;
            else if (cur == minor) cur = patch;
            ver++;
            continue;
        }
        if (*ver >= '0' && *ver <= '9')
            *cur = *cur * 10 + (*ver - '0');
        ver++;
    }
}

/* ── OTA Update: Download + Stage ── */

#include "../net/tcp.h"

hal_status_t ota_download_update(const char *url, const char *version)
{
    hal_console_printf("[ota] Staging update v%s\n", version);

    /* We need storage to stage the update */
    const driver_ops_t *stor = driver_get_storage();
    if (!stor || !stor->write || !stor->read) {
        hal_console_puts("[ota] No storage driver — cannot stage update\n");
        return HAL_NO_DEVICE;
    }

    /* For now, we just record the update metadata.
     * Full HTTP download of the kernel binary would follow the same pattern
     * as marketplace_get_driver() but saving to storage instead of memory. */

    /* Build OTA staging header */
    uint64_t phys;
    uint8_t *sector_buf = (uint8_t *)hal_dma_alloc(512, &phys);
    if (!sector_buf)
        return HAL_NO_MEMORY;

    for (int i = 0; i < 512; i++)
        sector_buf[i] = 0;

    ota_staging_header_t *hdr = (ota_staging_header_t *)sector_buf;
    hdr->magic = OTA_MAGIC;
    parse_version(version, &hdr->version_major, &hdr->version_minor, &hdr->version_patch);
    hdr->kernel_size = 0;  /* Will be filled when download completes */
    hdr->kernel_crc32 = 0;
    hdr->status = 0;       /* Pending */

    /* Write staging header to storage */
    int64_t wr = stor->write(sector_buf, OTA_STAGING_LBA, 1);
    hal_dma_free(sector_buf, 512);

    if (wr <= 0) {
        hal_console_puts("[ota] Failed to write staging header\n");
        return HAL_ERROR;
    }

    hal_console_printf("[ota] Update v%u.%u.%u staged (pending download)\n",
                       hdr->version_major, hdr->version_minor, hdr->version_patch);
    return HAL_OK;
}

hal_status_t ota_check_and_apply(void)
{
    const driver_ops_t *stor = driver_get_storage();
    if (!stor || !stor->read)
        return HAL_NO_DEVICE;

    /* Read staging header */
    uint64_t phys;
    uint8_t *sector_buf = (uint8_t *)hal_dma_alloc(512, &phys);
    if (!sector_buf)
        return HAL_NO_MEMORY;

    int64_t rd = stor->read(sector_buf, OTA_STAGING_LBA, 1);
    if (rd <= 0) {
        hal_dma_free(sector_buf, 512);
        return HAL_ERROR;
    }

    ota_staging_header_t *hdr = (ota_staging_header_t *)sector_buf;

    if (hdr->magic != OTA_MAGIC || hdr->status != 0) {
        /* No pending update */
        hal_dma_free(sector_buf, 512);
        return HAL_OK;
    }

    if (hdr->kernel_size == 0) {
        /* Header written but download not complete */
        hal_console_printf("[ota] Pending update v%u.%u.%u (download incomplete)\n",
                           hdr->version_major, hdr->version_minor, hdr->version_patch);
        hal_dma_free(sector_buf, 512);
        return HAL_OK;
    }

    hal_console_printf("[ota] Applying update v%u.%u.%u (%u bytes)\n",
                       hdr->version_major, hdr->version_minor, hdr->version_patch,
                       hdr->kernel_size);

    /* Read the kernel binary from staging area (LBA after header) */
    uint32_t sectors = (hdr->kernel_size + 511) / 512;
    if (sectors > OTA_STAGING_SECTORS - 1) {
        hal_console_puts("[ota] Kernel too large for staging area\n");
        hdr->status = 2; /* Failed */
        stor->write(sector_buf, OTA_STAGING_LBA, 1);
        hal_dma_free(sector_buf, 512);
        return HAL_ERROR;
    }

    uint64_t kern_phys;
    void *kern_buf = hal_dma_alloc(hdr->kernel_size, &kern_phys);
    if (!kern_buf) {
        hal_dma_free(sector_buf, 512);
        return HAL_NO_MEMORY;
    }

    rd = stor->read(kern_buf, OTA_STAGING_LBA + 1, sectors);
    if (rd <= 0) {
        hal_console_puts("[ota] Failed to read staged kernel\n");
        hdr->status = 2;
        stor->write(sector_buf, OTA_STAGING_LBA, 1);
        hal_dma_free(kern_buf, hdr->kernel_size);
        hal_dma_free(sector_buf, 512);
        return HAL_ERROR;
    }

    /* Verify CRC32 */
    uint32_t crc = crc32_buf(kern_buf, hdr->kernel_size);
    if (crc != hdr->kernel_crc32) {
        hal_console_printf("[ota] CRC mismatch: got 0x%08x, expected 0x%08x\n",
                           crc, hdr->kernel_crc32);
        hdr->status = 2;
        stor->write(sector_buf, OTA_STAGING_LBA, 1);
        hal_dma_free(kern_buf, hdr->kernel_size);
        hal_dma_free(sector_buf, 512);
        return HAL_ERROR;
    }

    /* Mark as applied */
    hdr->status = 1;
    stor->write(sector_buf, OTA_STAGING_LBA, 1);

    hal_console_printf("[ota] Update v%u.%u.%u verified and staged — reboot to activate\n",
                       hdr->version_major, hdr->version_minor, hdr->version_patch);

    hal_dma_free(kern_buf, hdr->kernel_size);
    hal_dma_free(sector_buf, 512);
    return HAL_OK;
}
