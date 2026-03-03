/* SPDX-License-Identifier: MIT */
/* AlJefra OS — AI Bootstrap Implementation
 *
 * Boot sequence: Hardware detect → Network → Marketplace → Drivers → Ready
 *
 * The AI bootstrap is the core of the universal boot concept:
 * we only need enough drivers to get network connectivity,
 * then AI handles the rest by downloading appropriate drivers.
 */

#include "ai_bootstrap.h"
#include "driver_loader.h"
#include "../hal/hal.h"
#include "../store/verify.h"
#include "../lib/string.h"

/* Network/marketplace modules (from net/ and ai/) */
extern hal_status_t dhcp_discover(uint32_t *ip, uint32_t *gateway, uint32_t *dns);
extern void tcp_init(uint32_t local_ip, uint32_t gateway, uint32_t netmask);
#include "../ai/marketplace.h"

/* AlJefra Store Ed25519 public key (32 bytes).
 * In development mode this is all-zeros, and signature verification is skipped.
 * For production, replace with the real store signing key. */
static const uint8_t ALJEFRA_STORE_PUBKEY[32] = {0};

static bootstrap_state_t g_state = BOOTSTRAP_INIT;

/* ── Helpers ── */

/* Check if a device already has a loaded driver */
static int device_has_driver(hal_device_t *dev)
{
    /* Check if any loaded driver handles this vendor:device */
    const driver_ops_t *net = driver_get_network();
    const driver_ops_t *stor = driver_get_storage();
    const driver_ops_t *inp = driver_get_input();

    /* If we have drivers in the major categories, the device is likely covered */
    (void)dev; (void)net; (void)stor; (void)inp;

    /* For now, check by PCI class if a driver category is filled */
    switch (dev->class_code) {
    case 0x01: return driver_get_storage() != NULL;
    case 0x02: return driver_get_network() != NULL;
    case 0x03: return driver_find(DRIVER_CAT_DISPLAY) != NULL;
    case 0x0C: return driver_get_input() != NULL;
    default: return 0;
    }
}

/* ── Build hardware manifest ── */

void ai_bootstrap_build_manifest(hal_device_t *devices, uint32_t count,
                                  hardware_manifest_t *manifest)
{
    manifest->arch = hal_arch();
    manifest->ram_bytes = hal_mmu_total_ram();

    hal_cpu_info_t cpu;
    hal_cpu_get_info(&cpu);

    /* Copy CPU strings */
    for (int i = 0; i < 16 && cpu.vendor[i]; i++)
        manifest->cpu_vendor[i] = cpu.vendor[i];
    for (int i = 0; i < 48 && cpu.model[i]; i++)
        manifest->cpu_model[i] = cpu.model[i];

    manifest->entry_count = 0;
    for (uint32_t i = 0; i < count && manifest->entry_count < HAL_BUS_MAX_DEVICES; i++) {
        manifest_entry_t *e = &manifest->entries[manifest->entry_count];
        e->vendor_id = devices[i].vendor_id;
        e->device_id = devices[i].device_id;
        e->class_code = devices[i].class_code;
        e->subclass = devices[i].subclass;
        e->has_driver = device_has_driver(&devices[i]) ? 1 : 0;
        e->reserved = 0;
        manifest->entry_count++;
    }
}

/* ── Main bootstrap sequence ── */

hal_status_t ai_bootstrap(hal_device_t *devices, uint32_t count)
{
    hal_status_t rc;

    hal_console_puts("[bootstrap] === AI Bootstrap Starting ===\n");

    /* Step 0: Set trusted public key for driver signature verification.
     * If the key is all-zeros (dev mode), verification is skipped. */
    int key_nonzero = 0;
    for (int i = 0; i < 32; i++) {
        if (ALJEFRA_STORE_PUBKEY[i]) { key_nonzero = 1; break; }
    }
    if (key_nonzero)
        ajdrv_set_trusted_key(ALJEFRA_STORE_PUBKEY);

    /* Step 1: Build hardware manifest */
    hardware_manifest_t manifest;
    for (uint64_t i = 0; i < sizeof(manifest); i++)
        ((uint8_t *)&manifest)[i] = 0;

    ai_bootstrap_build_manifest(devices, count, &manifest);

    hal_console_printf("[bootstrap] Manifest: %u devices, %u MB RAM\n",
                       manifest.entry_count,
                       (uint32_t)(manifest.ram_bytes / (1024 * 1024)));

    /* Count devices needing drivers */
    uint32_t need_drivers = 0;
    for (uint32_t i = 0; i < manifest.entry_count; i++) {
        if (!manifest.entries[i].has_driver)
            need_drivers++;
    }

    if (need_drivers == 0) {
        hal_console_puts("[bootstrap] All devices have drivers, skipping download\n");
        g_state = BOOTSTRAP_COMPLETE;
        return HAL_OK;
    }

    hal_console_printf("[bootstrap] %u devices need drivers\n", need_drivers);

    /* Step 2: Check network availability */
    const driver_ops_t *net = driver_get_network();
    if (!net) {
        hal_console_puts("[bootstrap] No network driver available!\n");
        hal_console_puts("[bootstrap] Cannot download drivers — using built-in only\n");
        g_state = BOOTSTRAP_FAILED;
        return HAL_NO_DEVICE;
    }

    /* Step 3: DHCP to get network configuration */
    hal_console_puts("[bootstrap] Running DHCP...\n");
    uint32_t ip = 0, gateway = 0, dns = 0;
    rc = dhcp_discover(&ip, &gateway, &dns);
    if (rc != HAL_OK) {
        hal_console_puts("[bootstrap] DHCP failed, trying static config\n");
        /* Fall back to static IP if configured */
        ip = 0x0A000002;       /* 10.0.0.2 */
        gateway = 0x0A000001;  /* 10.0.0.1 */
        dns = 0x08080808;      /* 8.8.8.8 */
    }

    hal_console_printf("[bootstrap] IP: %u.%u.%u.%u\n",
                       (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                       (ip >> 8) & 0xFF, ip & 0xFF);

    /* Initialize TCP stack and marketplace with DHCP results */
    tcp_init(ip, gateway, 0xFFFFFF00); /* /24 netmask */
    marketplace_set_gateway(gateway);

    g_state = BOOTSTRAP_NET_UP;

    /* Step 4: Connect to marketplace */
    hal_console_puts("[bootstrap] Connecting to AlJefra Store...\n");
    rc = marketplace_connect();
    if (rc != HAL_OK) {
        hal_console_puts("[bootstrap] Marketplace connection failed\n");
        g_state = BOOTSTRAP_FAILED;
        return rc;
    }
    g_state = BOOTSTRAP_CONNECTED;

    /* Step 5: Send manifest, get driver recommendations */
    hal_console_puts("[bootstrap] Sending hardware manifest...\n");
    rc = marketplace_send_manifest(&manifest);
    if (rc != HAL_OK) {
        hal_console_puts("[bootstrap] Manifest send failed\n");
        marketplace_disconnect();
        g_state = BOOTSTRAP_FAILED;
        return rc;
    }
    g_state = BOOTSTRAP_MANIFEST_SENT;

    /* Step 6: Download and install each recommended driver */
    g_state = BOOTSTRAP_DOWNLOADING;
    uint32_t downloaded = 0;

    for (uint32_t i = 0; i < manifest.entry_count; i++) {
        manifest_entry_t *e = &manifest.entries[i];

        if (e->has_driver)
            continue; /* Already have a driver */

        hal_console_printf("[bootstrap] Downloading driver for %04x:%04x...\n",
                           e->vendor_id, e->device_id);

        void *drv_data = NULL;
        uint64_t drv_size = 0;
        rc = marketplace_get_driver(e->vendor_id, e->device_id, &drv_data, &drv_size);
        if (rc != HAL_OK) {
            hal_console_printf("[bootstrap] No driver available for %04x:%04x\n",
                               e->vendor_id, e->device_id);
            continue;
        }

        /* Find the matching hal_device_t */
        hal_device_t *target_dev = NULL;
        for (uint32_t j = 0; j < count; j++) {
            if (devices[j].vendor_id == e->vendor_id &&
                devices[j].device_id == e->device_id) {
                target_dev = &devices[j];
                break;
            }
        }

        /* Load the driver */
        rc = driver_load_runtime(drv_data, drv_size, target_dev);
        if (rc == HAL_OK) {
            downloaded++;
            e->has_driver = 1;
            hal_console_printf("[bootstrap] Installed driver for %04x:%04x\n",
                               e->vendor_id, e->device_id);
        } else {
            hal_console_printf("[bootstrap] Failed to load driver for %04x:%04x\n",
                               e->vendor_id, e->device_id);
        }

        /* Free the downloaded data */
        if (drv_data)
            hal_dma_free(drv_data, drv_size);
    }

    /* Step 7: Check for OS updates before disconnecting */
    {
        char update_url[256];
        update_url[0] = '\0';
        rc = marketplace_check_updates("0.1.0", update_url, sizeof(update_url));
        if (rc == HAL_OK && update_url[0]) {
            hal_console_printf("[bootstrap] OS update: %s\n", update_url);
            /* OTA update download/apply deferred to next reboot cycle */
        }
    }

    marketplace_disconnect();

    hal_console_printf("[bootstrap] Downloaded %u drivers\n", downloaded);
    g_state = BOOTSTRAP_COMPLETE;

    hal_console_puts("[bootstrap] === AI Bootstrap Complete ===\n");
    return HAL_OK;
}

bootstrap_state_t ai_bootstrap_state(void)
{
    return g_state;
}
