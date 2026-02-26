/* SPDX-License-Identifier: MIT */
/* AlJefra OS — AHCI (SATA) Storage Driver Implementation
 * Architecture-independent SATA controller driver via AHCI.
 * Uses HAL for all MMIO, DMA, and bus operations.
 */

#include "ahci.h"
#include "../../lib/string.h"

/* ── Internal constants ── */
#define AHCI_TIMEOUT_MS       5000
#define AHCI_POLL_US          100
#define AHCI_CMD_TABLE_SIZE   256   /* 128B header + 8 PRDTs * 16B */
#define AHCI_MAX_PRDT_PER_CMD 8
#define AHCI_PRDT_MAX_BYTES   (4 * 1024 * 1024)  /* 4MB per PRDT entry */

/* ── Helpers ── */

/* Read a port register */
static inline uint32_t ahci_port_read(ahci_port_t *p, uint32_t off)
{
    return hal_mmio_read32((volatile void *)((uint8_t *)p->port_regs + off));
}

/* Write a port register */
static inline void ahci_port_write(ahci_port_t *p, uint32_t off, uint32_t val)
{
    hal_mmio_write32((volatile void *)((uint8_t *)p->port_regs + off), val);
}

/* Read an HBA register */
static inline uint32_t ahci_hba_read(ahci_hba_t *hba, uint32_t off)
{
    return hal_mmio_read32((volatile void *)((uint8_t *)hba->regs + off));
}

/* Write an HBA register */
static inline void ahci_hba_write(ahci_hba_t *hba, uint32_t off, uint32_t val)
{
    hal_mmio_write32((volatile void *)((uint8_t *)hba->regs + off), val);
}

/* ── Port management ── */

/* Stop command engine on a port */
static hal_status_t ahci_port_stop(ahci_port_t *p)
{
    uint32_t cmd = ahci_port_read(p, AHCI_PORT_CMD);

    /* Clear ST (Start) */
    cmd &= ~AHCI_CMD_ST;
    ahci_port_write(p, AHCI_PORT_CMD, cmd);

    /* Wait for CR (Command List Running) to clear */
    uint64_t deadline = hal_timer_ms() + AHCI_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        cmd = ahci_port_read(p, AHCI_PORT_CMD);
        if (!(cmd & AHCI_CMD_CR))
            break;
        hal_timer_delay_us(AHCI_POLL_US);
    }

    /* Clear FRE (FIS Receive Enable) */
    cmd = ahci_port_read(p, AHCI_PORT_CMD);
    cmd &= ~AHCI_CMD_FRE;
    ahci_port_write(p, AHCI_PORT_CMD, cmd);

    /* Wait for FR (FIS Receive Running) to clear */
    deadline = hal_timer_ms() + AHCI_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        cmd = ahci_port_read(p, AHCI_PORT_CMD);
        if (!(cmd & AHCI_CMD_FR))
            return HAL_OK;
        hal_timer_delay_us(AHCI_POLL_US);
    }

    return HAL_TIMEOUT;
}

/* Start command engine on a port */
static void ahci_port_start(ahci_port_t *p)
{
    /* Wait for CR to clear before starting */
    uint64_t deadline = hal_timer_ms() + AHCI_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t cmd = ahci_port_read(p, AHCI_PORT_CMD);
        if (!(cmd & AHCI_CMD_CR))
            break;
        hal_timer_delay_us(AHCI_POLL_US);
    }

    /* Set FRE then ST */
    uint32_t cmd = ahci_port_read(p, AHCI_PORT_CMD);
    cmd |= AHCI_CMD_FRE;
    ahci_port_write(p, AHCI_PORT_CMD, cmd);

    cmd |= AHCI_CMD_ST;
    ahci_port_write(p, AHCI_PORT_CMD, cmd);
}

/* Check if a device is present on a port */
static bool ahci_port_device_present(ahci_port_t *p)
{
    uint32_t ssts = ahci_port_read(p, AHCI_PORT_SSTS);
    uint8_t det = ssts & AHCI_SSTS_DET_MASK;
    uint16_t ipm = ssts & AHCI_SSTS_IPM_MASK;
    return (det == AHCI_SSTS_DET_PRESENT) && (ipm == AHCI_SSTS_IPM_ACTIVE);
}

/* Find a free command slot */
static int ahci_find_free_slot(ahci_hba_t *hba, ahci_port_t *p)
{
    uint32_t slots = ahci_port_read(p, AHCI_PORT_SACT) |
                     ahci_port_read(p, AHCI_PORT_CI);
    for (uint32_t i = 0; i < hba->cmd_slots; i++) {
        if (!(slots & (1u << i)))
            return (int)i;
    }
    return -1;
}

/* Wait for a command slot to complete */
static hal_status_t ahci_wait_slot(ahci_port_t *p, int slot)
{
    uint64_t deadline = hal_timer_ms() + AHCI_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t ci = ahci_port_read(p, AHCI_PORT_CI);
        if (!(ci & (1u << slot))) {
            /* Check for errors */
            uint32_t tfd = ahci_port_read(p, AHCI_PORT_TFD);
            if (tfd & 0x01) /* BSY or ERR */
                return HAL_ERROR;
            return HAL_OK;
        }

        /* Check for task file error */
        uint32_t is = ahci_port_read(p, AHCI_PORT_IS);
        if (is & (1u << 30)) { /* TFES — Task File Error Status */
            return HAL_ERROR;
        }

        hal_timer_delay_us(AHCI_POLL_US);
    }
    return HAL_TIMEOUT;
}

/* Initialize a single port */
static hal_status_t ahci_port_init(ahci_hba_t *hba, uint32_t port_num)
{
    ahci_port_t *p = &hba->ports[port_num];

    /* Calculate port register base: 0x100 + port * 0x80 */
    p->port_regs = (volatile void *)((uint8_t *)hba->regs + 0x100 + port_num * 0x80);

    /* Check device presence */
    if (!ahci_port_device_present(p)) {
        p->present = false;
        return HAL_NO_DEVICE;
    }

    /* Check signature */
    uint32_t sig = ahci_port_read(p, AHCI_PORT_SIG);
    if (sig == AHCI_SIG_ATAPI)
        p->is_atapi = true;
    else if (sig != AHCI_SIG_SATA) {
        p->present = false;
        return HAL_NO_DEVICE;
    }

    /* Stop command engine before reconfiguring */
    ahci_port_stop(p);

    /* Allocate Command List (1KB — 32 headers * 32 bytes) */
    p->cmd_list = (ahci_cmd_header_t *)hal_dma_alloc(1024, &p->cmd_list_phys);
    if (!p->cmd_list)
        return HAL_NO_MEMORY;
    memset(p->cmd_list, 0, 1024);

    /* Allocate FIS Receive area (256 bytes) */
    p->fis_recv = (ahci_fis_recv_t *)hal_dma_alloc(256, &p->fis_recv_phys);
    if (!p->fis_recv)
        return HAL_NO_MEMORY;
    memset(p->fis_recv, 0, 256);

    /* Allocate Command Tables (one per slot we actually use) */
    for (uint32_t i = 0; i < hba->cmd_slots; i++) {
        p->cmd_tables[i] = (ahci_cmd_table_t *)hal_dma_alloc(
            AHCI_CMD_TABLE_SIZE, &p->cmd_table_phys[i]);
        if (!p->cmd_tables[i])
            return HAL_NO_MEMORY;
        memset(p->cmd_tables[i], 0, AHCI_CMD_TABLE_SIZE);

        /* Point command header to command table */
        p->cmd_list[i].ctba  = (uint32_t)(p->cmd_table_phys[i] & 0xFFFFFFFF);
        p->cmd_list[i].ctbau = (uint32_t)(p->cmd_table_phys[i] >> 32);
    }

    /* Set Command List and FIS Receive base addresses */
    ahci_port_write(p, AHCI_PORT_CLB,  (uint32_t)(p->cmd_list_phys & 0xFFFFFFFF));
    ahci_port_write(p, AHCI_PORT_CLBU, (uint32_t)(p->cmd_list_phys >> 32));
    ahci_port_write(p, AHCI_PORT_FB,   (uint32_t)(p->fis_recv_phys & 0xFFFFFFFF));
    ahci_port_write(p, AHCI_PORT_FBU,  (uint32_t)(p->fis_recv_phys >> 32));

    /* Clear pending interrupts and errors */
    ahci_port_write(p, AHCI_PORT_SERR, 0xFFFFFFFF);
    ahci_port_write(p, AHCI_PORT_IS, 0xFFFFFFFF);

    /* Start command engine */
    ahci_port_start(p);

    p->present = true;
    p->sector_size = 512; /* Default; will be updated by IDENTIFY */

    return HAL_OK;
}

/* Issue ATA IDENTIFY DEVICE command */
static hal_status_t ahci_identify(ahci_hba_t *hba, uint32_t port_num)
{
    ahci_port_t *p = &hba->ports[port_num];
    if (!p->present)
        return HAL_NO_DEVICE;

    int slot = ahci_find_free_slot(hba, p);
    if (slot < 0)
        return HAL_BUSY;

    /* Allocate DMA buffer for 512-byte IDENTIFY response */
    uint64_t id_phys;
    void *id_buf = hal_dma_alloc(512, &id_phys);
    if (!id_buf)
        return HAL_NO_MEMORY;
    memset(id_buf, 0, 512);

    /* Set up command header */
    ahci_cmd_header_t *hdr = &p->cmd_list[slot];
    hdr->cfl = sizeof(ahci_fis_reg_h2d_t) / 4; /* FIS length in DWORDs (5) */
    hdr->w = 0;    /* Device to Host */
    hdr->prdtl = 1; /* One PRDT entry */
    hdr->prdbc = 0;

    /* Set up command table */
    ahci_cmd_table_t *tbl = p->cmd_tables[slot];
    memset(tbl, 0, AHCI_CMD_TABLE_SIZE);

    /* Build Register H2D FIS */
    ahci_fis_reg_h2d_t *fis = (ahci_fis_reg_h2d_t *)tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;            /* Command */
    fis->command = ATA_CMD_IDENTIFY;
    fis->device = 0;

    /* Set up PRDT entry */
    tbl->prdt[0].dba  = (uint32_t)(id_phys & 0xFFFFFFFF);
    tbl->prdt[0].dbau = (uint32_t)(id_phys >> 32);
    tbl->prdt[0].dbc  = 511;   /* 512 bytes (0-based) */
    tbl->prdt[0].i    = 0;

    /* Issue the command */
    hal_mmio_barrier();
    ahci_port_write(p, AHCI_PORT_CI, 1u << slot);

    /* Wait for completion */
    hal_status_t st = ahci_wait_slot(p, slot);
    if (st != HAL_OK) {
        hal_dma_free(id_buf, 512);
        return st;
    }

    /* Parse IDENTIFY response */
    ahci_identify_t *id = (ahci_identify_t *)id_buf;

    /* Check for LBA48 support (word 83 bit 10) */
    if (id->features_83 & (1u << 10)) {
        p->total_sectors = id->lba48_sectors;
    } else {
        p->total_sectors = id->lba28_sectors;
    }

    p->sector_size = 512;

    hal_dma_free(id_buf, 512);
    return HAL_OK;
}

/* ── Generic ATA DMA command (READ/WRITE) ── */

static hal_status_t ahci_ata_dma(ahci_hba_t *hba, uint32_t port_num,
                                  uint64_t lba, uint32_t count,
                                  uint64_t buf_phys, bool write)
{
    ahci_port_t *p = &hba->ports[port_num];
    if (!p->present)
        return HAL_NO_DEVICE;

    uint32_t remaining = count;
    uint64_t cur_lba = lba;
    uint64_t cur_phys = buf_phys;

    while (remaining > 0) {
        /* Limit per-command transfer to fit PRDT entries.
         * Each PRDT entry can do 4MB. With 8 entries = 32MB.
         * ATA limit: 65536 sectors per READ/WRITE DMA EXT. */
        uint32_t chunk = remaining;
        if (chunk > 65536) chunk = 65536;

        /* Calculate bytes for this chunk */
        uint64_t chunk_bytes = (uint64_t)chunk * p->sector_size;

        /* Calculate PRDT entries needed */
        uint32_t prdt_count = 0;
        {
            uint64_t left = chunk_bytes;
            while (left > 0 && prdt_count < AHCI_MAX_PRDT_PER_CMD) {
                uint64_t entry_size = left;
                if (entry_size > AHCI_PRDT_MAX_BYTES)
                    entry_size = AHCI_PRDT_MAX_BYTES;
                prdt_count++;
                left -= entry_size;
            }
            /* If we can't fit it, reduce chunk */
            if (left > 0) {
                chunk = (uint32_t)((uint64_t)AHCI_MAX_PRDT_PER_CMD *
                                   AHCI_PRDT_MAX_BYTES / p->sector_size);
                chunk_bytes = (uint64_t)chunk * p->sector_size;
            }
        }

        int slot = ahci_find_free_slot(hba, p);
        if (slot < 0)
            return HAL_BUSY;

        /* Set up command header */
        ahci_cmd_header_t *hdr = &p->cmd_list[slot];
        hdr->cfl = sizeof(ahci_fis_reg_h2d_t) / 4;
        hdr->w = write ? 1 : 0;
        hdr->prdbc = 0;

        /* Build command table */
        ahci_cmd_table_t *tbl = p->cmd_tables[slot];
        memset(tbl, 0, AHCI_CMD_TABLE_SIZE);

        /* Register H2D FIS */
        ahci_fis_reg_h2d_t *fis = (ahci_fis_reg_h2d_t *)tbl->cfis;
        fis->fis_type = FIS_TYPE_REG_H2D;
        fis->c = 1;
        fis->command = write ? ATA_CMD_WRITE_DMA_EX : ATA_CMD_READ_DMA_EX;
        fis->device = (1 << 6);    /* LBA mode */
        fis->lba0 = (uint8_t)(cur_lba & 0xFF);
        fis->lba1 = (uint8_t)((cur_lba >> 8) & 0xFF);
        fis->lba2 = (uint8_t)((cur_lba >> 16) & 0xFF);
        fis->lba3 = (uint8_t)((cur_lba >> 24) & 0xFF);
        fis->lba4 = (uint8_t)((cur_lba >> 32) & 0xFF);
        fis->lba5 = (uint8_t)((cur_lba >> 40) & 0xFF);
        fis->count = (uint16_t)(chunk == 65536 ? 0 : chunk); /* 0 means 65536 */

        /* Fill PRDT entries */
        uint64_t offset = 0;
        uint64_t left = chunk_bytes;
        uint32_t pi = 0;
        while (left > 0 && pi < AHCI_MAX_PRDT_PER_CMD) {
            uint64_t entry_size = left;
            if (entry_size > AHCI_PRDT_MAX_BYTES)
                entry_size = AHCI_PRDT_MAX_BYTES;
            tbl->prdt[pi].dba  = (uint32_t)((cur_phys + offset) & 0xFFFFFFFF);
            tbl->prdt[pi].dbau = (uint32_t)((cur_phys + offset) >> 32);
            tbl->prdt[pi].dbc  = (uint32_t)(entry_size - 1);  /* 0-based */
            tbl->prdt[pi].i    = 0;
            offset += entry_size;
            left -= entry_size;
            pi++;
        }
        hdr->prdtl = (uint16_t)pi;

        /* Issue command */
        hal_mmio_barrier();
        ahci_port_write(p, AHCI_PORT_CI, 1u << slot);

        hal_status_t st = ahci_wait_slot(p, slot);
        if (st != HAL_OK)
            return st;

        remaining -= chunk;
        cur_lba += chunk;
        cur_phys += chunk_bytes;
    }

    return HAL_OK;
}

/* ── Public API ── */

hal_status_t ahci_init(ahci_hba_t *hba, hal_device_t *dev)
{
    hba->dev = *dev;
    hba->initialized = false;

    /* Enable bus mastering + memory space */
    hal_bus_pci_enable(dev);

    /* Map BAR5 (ABAR — AHCI Base Memory Register) */
    hba->regs = hal_bus_map_bar(dev, 5);
    if (!hba->regs)
        return HAL_ERROR;

    /* Enable AHCI mode (GHC.AE = 1) */
    uint32_t ghc = ahci_hba_read(hba, AHCI_REG_GHC);
    ghc |= AHCI_GHC_AE;
    ahci_hba_write(hba, AHCI_REG_GHC, ghc);

    /* Perform HBA reset */
    ghc = ahci_hba_read(hba, AHCI_REG_GHC);
    ghc |= AHCI_GHC_HR;
    ahci_hba_write(hba, AHCI_REG_GHC, ghc);

    /* Wait for reset to complete (HR clears itself) */
    uint64_t deadline = hal_timer_ms() + AHCI_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        ghc = ahci_hba_read(hba, AHCI_REG_GHC);
        if (!(ghc & AHCI_GHC_HR))
            break;
        hal_timer_delay_us(AHCI_POLL_US);
    }
    if (ghc & AHCI_GHC_HR)
        return HAL_TIMEOUT;

    /* Re-enable AHCI mode after reset */
    ghc = ahci_hba_read(hba, AHCI_REG_GHC);
    ghc |= AHCI_GHC_AE;
    ahci_hba_write(hba, AHCI_REG_GHC, ghc);

    /* Read capabilities */
    uint32_t cap = ahci_hba_read(hba, AHCI_REG_CAP);
    hba->cmd_slots = ((cap >> 8) & 0x1F) + 1;  /* NCS: 0-based */
    uint32_t port_count = (cap & 0x1F) + 1;     /* NP: 0-based */
    if (port_count > AHCI_MAX_PORTS) port_count = AHCI_MAX_PORTS;
    hba->port_count = port_count;

    /* Read which ports are implemented */
    uint32_t pi = ahci_hba_read(hba, AHCI_REG_PI);

    /* Initialize each implemented port */
    for (uint32_t i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!(pi & (1u << i))) {
            hba->ports[i].present = false;
            continue;
        }
        ahci_port_init(hba, i);
        /* If port has a SATA device, run IDENTIFY */
        if (hba->ports[i].present && !hba->ports[i].is_atapi) {
            ahci_identify(hba, i);
        }
    }

    /* Enable interrupts globally (optional for polling mode) */
    ghc = ahci_hba_read(hba, AHCI_REG_GHC);
    ghc |= AHCI_GHC_IE;
    ahci_hba_write(hba, AHCI_REG_GHC, ghc);

    hba->initialized = true;
    return HAL_OK;
}

hal_status_t ahci_read(ahci_hba_t *hba, uint32_t port, uint64_t lba,
                       uint32_t count, void *buf, uint64_t buf_phys)
{
    if (!hba->initialized || port >= AHCI_MAX_PORTS)
        return HAL_ERROR;
    (void)buf; /* Physical address used for DMA */
    return ahci_ata_dma(hba, port, lba, count, buf_phys, false);
}

hal_status_t ahci_write(ahci_hba_t *hba, uint32_t port, uint64_t lba,
                        uint32_t count, const void *buf, uint64_t buf_phys)
{
    if (!hba->initialized || port >= AHCI_MAX_PORTS)
        return HAL_ERROR;
    (void)buf;
    return ahci_ata_dma(hba, port, lba, count, buf_phys, true);
}

hal_status_t ahci_get_disk_info(ahci_hba_t *hba, uint32_t port,
                                uint64_t *total_sectors, uint32_t *sector_size)
{
    if (!hba->initialized || port >= AHCI_MAX_PORTS)
        return HAL_ERROR;
    if (!hba->ports[port].present)
        return HAL_NO_DEVICE;
    if (total_sectors)
        *total_sectors = hba->ports[port].total_sectors;
    if (sector_size)
        *sector_size = hba->ports[port].sector_size;
    return HAL_OK;
}

int ahci_find_disk(ahci_hba_t *hba)
{
    for (uint32_t i = 0; i < AHCI_MAX_PORTS; i++) {
        if (hba->ports[i].present && !hba->ports[i].is_atapi)
            return (int)i;
    }
    return -1;
}

/* ── driver_ops_t wrapper for built-in driver registration ── */
#include "../../kernel/driver_loader.h"

static ahci_hba_t g_ahci_hba;
static int g_ahci_port = -1;  /* First active SATA port */

static hal_status_t ahci_drv_init(hal_device_t *dev)
{
    hal_status_t rc = ahci_init(&g_ahci_hba, dev);
    if (rc != HAL_OK) return rc;
    g_ahci_port = ahci_find_disk(&g_ahci_hba);
    if (g_ahci_port < 0) return HAL_NO_DEVICE;
    return HAL_OK;
}

static int64_t ahci_drv_read(void *buf, uint64_t lba, uint32_t count)
{
    if (g_ahci_port < 0) return -1;
    /* Freestanding identity mapping: virtual address == physical address */
    hal_status_t rc = ahci_read(&g_ahci_hba, (uint32_t)g_ahci_port,
                                 lba, count, buf, (uint64_t)buf);
    return (rc == HAL_OK) ? (int64_t)count : -1;
}

static int64_t ahci_drv_write(const void *buf, uint64_t lba, uint32_t count)
{
    if (g_ahci_port < 0) return -1;
    hal_status_t rc = ahci_write(&g_ahci_hba, (uint32_t)g_ahci_port,
                                  lba, count, buf, (uint64_t)buf);
    return (rc == HAL_OK) ? (int64_t)count : -1;
}

static const driver_ops_t ahci_driver_ops = {
    .name       = "ahci",
    .category   = DRIVER_CAT_STORAGE,
    .init       = ahci_drv_init,
    .shutdown   = NULL,
    .read       = ahci_drv_read,
    .write      = ahci_drv_write,
};

void ahci_register(void)
{
    driver_register_builtin(&ahci_driver_ops);
}
