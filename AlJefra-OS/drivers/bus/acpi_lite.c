/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Minimal ACPI Table Parser Implementation
 * Finds RSDP, parses RSDT/XSDT, MADT, MCFG, FADT.
 */

#include "acpi_lite.h"

/* ── Helpers ── */

static bool acpi_sig_match(const char *a, const char *b, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        if (a[i] != b[i])
            return false;
    }
    return true;
}

static uint8_t acpi_checksum(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++)
        sum += p[i];
    return sum;
}

/* ── Find RSDP by scanning memory ── */

static const acpi_rsdp_t *acpi_find_rsdp(uint64_t hint)
{
    /* If a hint is provided, check it first */
    if (hint != 0) {
        const acpi_rsdp_t *r = (const acpi_rsdp_t *)(uintptr_t)hint;
        if (acpi_sig_match(r->signature, ACPI_RSDP_SIG, 8) &&
            acpi_checksum(r, 20) == 0)
            return r;
    }

    /* Scan EBDA (Extended BIOS Data Area) — first KB at segment pointed by [0x40E] */
    /* Then scan 0xE0000 - 0xFFFFF */
    const uint8_t *scan_regions[] = {
        (const uint8_t *)(uintptr_t)0x000E0000,
    };
    uint32_t scan_lengths[] = {
        0x20000,   /* 128KB: 0xE0000 - 0xFFFFF */
    };

    for (uint32_t region = 0; region < 1; region++) {
        const uint8_t *base = scan_regions[region];
        uint32_t len = scan_lengths[region];

        for (uint32_t offset = 0; offset + 20 <= len; offset += 16) {
            const acpi_rsdp_t *r = (const acpi_rsdp_t *)(base + offset);
            if (acpi_sig_match(r->signature, ACPI_RSDP_SIG, 8) &&
                acpi_checksum(r, 20) == 0)
                return r;
        }
    }

    return NULL;
}

/* ── Find a specific SDT by signature ── */

static const acpi_sdt_header_t *acpi_find_table(const acpi_rsdp_t *rsdp,
                                                   bool use_xsdt,
                                                   const char *sig)
{
    if (use_xsdt && rsdp->revision >= 2) {
        const acpi_xsdt_t *xsdt = (const acpi_xsdt_t *)(uintptr_t)rsdp->xsdt_address;
        if (!xsdt)
            return NULL;

        uint32_t entries = (xsdt->header.length - sizeof(acpi_sdt_header_t)) / 8;
        for (uint32_t i = 0; i < entries; i++) {
            const acpi_sdt_header_t *hdr =
                (const acpi_sdt_header_t *)(uintptr_t)xsdt->entry[i];
            if (hdr && acpi_sig_match(hdr->signature, sig, 4) &&
                acpi_checksum(hdr, hdr->length) == 0)
                return hdr;
        }
    } else {
        const acpi_rsdt_t *rsdt = (const acpi_rsdt_t *)(uintptr_t)rsdp->rsdt_address;
        if (!rsdt)
            return NULL;

        uint32_t entries = (rsdt->header.length - sizeof(acpi_sdt_header_t)) / 4;
        for (uint32_t i = 0; i < entries; i++) {
            const acpi_sdt_header_t *hdr =
                (const acpi_sdt_header_t *)(uintptr_t)rsdt->entry[i];
            if (hdr && acpi_sig_match(hdr->signature, sig, 4) &&
                acpi_checksum(hdr, hdr->length) == 0)
                return hdr;
        }
    }

    return NULL;
}

/* ── Parse MADT ── */

static void acpi_parse_madt(acpi_info_t *acpi, const acpi_madt_t *madt)
{
    acpi->lapic_addr = madt->lapic_addr;
    acpi->lapic_count = 0;
    acpi->ioapic_count = 0;

    uint32_t length = madt->header.length;
    uint32_t offset = sizeof(acpi_madt_t);

    while (offset + 2 <= length) {
        const acpi_madt_entry_header_t *entry =
            (const acpi_madt_entry_header_t *)((const uint8_t *)madt + offset);

        if (entry->length < 2)
            break;

        switch (entry->type) {
        case ACPI_MADT_LAPIC: {
            const acpi_madt_lapic_t *lapic = (const acpi_madt_lapic_t *)entry;
            if ((lapic->flags & 0x03) && acpi->lapic_count < ACPI_MAX_LAPICS) {
                acpi->lapic_ids[acpi->lapic_count] = lapic->apic_id;
                acpi->lapic_count++;
            }
            break;
        }
        case ACPI_MADT_IOAPIC: {
            const acpi_madt_ioapic_t *ioapic = (const acpi_madt_ioapic_t *)entry;
            if (acpi->ioapic_count < ACPI_MAX_IOAPICS) {
                acpi->ioapic_addr[acpi->ioapic_count] = ioapic->ioapic_addr;
                acpi->ioapic_gsi_base[acpi->ioapic_count] = ioapic->gsi_base;
                acpi->ioapic_count++;
            }
            break;
        }
        case ACPI_MADT_LAPIC_OVERRIDE: {
            /* 64-bit Local APIC address override */
            if (entry->length >= 12) {
                const uint8_t *data = (const uint8_t *)entry;
                uint64_t addr = *(const uint64_t *)(data + 4);
                acpi->lapic_addr = (uint32_t)addr; /* Truncate for 32-bit compat */
            }
            break;
        }
        case ACPI_MADT_X2APIC: {
            /* x2APIC: APIC ID is 32-bit at offset 4 */
            if (entry->length >= 16 && acpi->lapic_count < ACPI_MAX_LAPICS) {
                const uint8_t *data = (const uint8_t *)entry;
                uint32_t flags = *(const uint32_t *)(data + 8);
                if (flags & 0x03) {
                    uint32_t apic_id = *(const uint32_t *)(data + 4);
                    acpi->lapic_ids[acpi->lapic_count] = (uint8_t)(apic_id & 0xFF);
                    acpi->lapic_count++;
                }
            }
            break;
        }
        default:
            break;
        }

        offset += entry->length;
    }
}

/* ── Parse MCFG ── */

static void acpi_parse_mcfg(acpi_info_t *acpi, const acpi_mcfg_t *mcfg)
{
    uint32_t length = mcfg->header.length;
    uint32_t entries = (length - sizeof(acpi_mcfg_t)) / sizeof(acpi_mcfg_entry_t);

    if (entries > 0) {
        acpi->ecam_base = mcfg->entry[0].base_address;
        acpi->ecam_start_bus = mcfg->entry[0].start_bus;
        acpi->ecam_end_bus = mcfg->entry[0].end_bus;
        acpi->ecam_found = true;
    }
}

/* ── Parse FADT ── */

static void acpi_parse_fadt(acpi_info_t *acpi, const acpi_fadt_t *fadt)
{
    acpi->pm1a_cnt_blk = fadt->pm1a_cnt_blk;
    acpi->sci_int = fadt->sci_int;
    acpi->fadt_found = true;
}

/* ── Public API ── */

hal_status_t acpi_init(acpi_info_t *acpi, uint64_t rsdp_hint)
{
    acpi->initialized = false;
    acpi->ecam_found = false;
    acpi->fadt_found = false;
    acpi->lapic_count = 0;
    acpi->ioapic_count = 0;

    /* Find RSDP */
    acpi->rsdp = acpi_find_rsdp(rsdp_hint);
    if (!acpi->rsdp)
        return HAL_NO_DEVICE;

    acpi->use_xsdt = (acpi->rsdp->revision >= 2 && acpi->rsdp->xsdt_address != 0);

    /* Validate RSDT or XSDT */
    if (acpi->use_xsdt) {
        const acpi_xsdt_t *xsdt =
            (const acpi_xsdt_t *)(uintptr_t)acpi->rsdp->xsdt_address;
        if (!xsdt || !acpi_sig_match(xsdt->header.signature, "XSDT", 4))
            return HAL_ERROR;
    } else {
        const acpi_rsdt_t *rsdt =
            (const acpi_rsdt_t *)(uintptr_t)acpi->rsdp->rsdt_address;
        if (!rsdt || !acpi_sig_match(rsdt->header.signature, "RSDT", 4))
            return HAL_ERROR;
    }

    /* Parse MADT (APIC table) */
    const acpi_madt_t *madt = (const acpi_madt_t *)acpi_find_table(
        acpi->rsdp, acpi->use_xsdt, ACPI_MADT_SIG);
    if (madt)
        acpi_parse_madt(acpi, madt);

    /* Parse MCFG (PCIe ECAM) */
    const acpi_mcfg_t *mcfg = (const acpi_mcfg_t *)acpi_find_table(
        acpi->rsdp, acpi->use_xsdt, ACPI_MCFG_SIG);
    if (mcfg)
        acpi_parse_mcfg(acpi, mcfg);

    /* Parse FADT */
    const acpi_fadt_t *fadt = (const acpi_fadt_t *)acpi_find_table(
        acpi->rsdp, acpi->use_xsdt, ACPI_FADT_SIG);
    if (fadt)
        acpi_parse_fadt(acpi, fadt);

    acpi->initialized = true;
    return HAL_OK;
}

hal_status_t acpi_get_ecam(acpi_info_t *acpi, uint64_t *base,
                            uint8_t *start_bus, uint8_t *end_bus)
{
    if (!acpi->initialized || !acpi->ecam_found)
        return HAL_NO_DEVICE;
    if (base)      *base = acpi->ecam_base;
    if (start_bus) *start_bus = acpi->ecam_start_bus;
    if (end_bus)   *end_bus = acpi->ecam_end_bus;
    return HAL_OK;
}

uint32_t acpi_get_lapic_addr(acpi_info_t *acpi)
{
    return acpi->initialized ? acpi->lapic_addr : 0;
}

uint32_t acpi_get_cpu_count(acpi_info_t *acpi)
{
    return acpi->initialized ? acpi->lapic_count : 0;
}

hal_status_t acpi_shutdown(acpi_info_t *acpi)
{
    if (!acpi->initialized || !acpi->fadt_found)
        return HAL_NOT_SUPPORTED;

    /* Write SLP_TYPa=5 | SLP_EN to PM1a_CNT_BLK.
     * SLP_TYPa for S5 (shutdown) is typically 5 on QEMU/Bochs.
     * SLP_EN is bit 13. */
    uint16_t val = (5 << 10) | (1 << 13);  /* SLP_TYPa=5, SLP_EN=1 */
    hal_port_out16((uint16_t)acpi->pm1a_cnt_blk, val);

    /* If we get here, shutdown failed */
    return HAL_ERROR;
}

hal_status_t acpi_reboot(acpi_info_t *acpi)
{
    if (!acpi->initialized)
        return HAL_NOT_SUPPORTED;

    /* Try the ACPI reset register if available.
     * For simplicity, use the well-known keyboard controller reset
     * as a fallback (works on most x86). */
    if (hal_arch() == HAL_ARCH_X86_64) {
        /* Pulse bit 0 of keyboard controller port 0x64 */
        uint8_t val = 0xFE;
        hal_port_out8(0x64, val);
    }

    /* If we get here, reboot failed */
    return HAL_ERROR;
}
