/* SPDX-License-Identifier: MIT */
/* AlJefra OS — xHCI (USB 3.0) Host Controller Driver Implementation
 * Architecture-independent; uses HAL for all hardware access.
 */

#include "xhci.h"
#include "../../lib/string.h"

/* ── Constants ── */
#define XHCI_TIMEOUT_MS    5000
#define XHCI_POLL_US       100
#define XHCI_CONTEXT_SIZE  32     /* 32 bytes per context (no 64-byte contexts) */

/* ── Helpers ── */

static inline uint32_t xhci_cap_read32(xhci_controller_t *hc, uint32_t off)
{
    return hal_mmio_read32((volatile void *)((uint8_t *)hc->cap_base + off));
}

static inline uint32_t xhci_op_read32(xhci_controller_t *hc, uint32_t off)
{
    return hal_mmio_read32((volatile void *)((uint8_t *)hc->op_base + off));
}

static inline void xhci_op_write32(xhci_controller_t *hc, uint32_t off, uint32_t v)
{
    hal_mmio_write32((volatile void *)((uint8_t *)hc->op_base + off), v);
}

static inline uint64_t xhci_op_read64(xhci_controller_t *hc, uint32_t off)
{
    uint32_t lo = hal_mmio_read32((volatile void *)((uint8_t *)hc->op_base + off));
    uint32_t hi = hal_mmio_read32((volatile void *)((uint8_t *)hc->op_base + off + 4));
    return ((uint64_t)hi << 32) | lo;
}

static inline void xhci_op_write64(xhci_controller_t *hc, uint32_t off, uint64_t v)
{
    hal_mmio_write32((volatile void *)((uint8_t *)hc->op_base + off), (uint32_t)v);
    hal_mmio_write32((volatile void *)((uint8_t *)hc->op_base + off + 4), (uint32_t)(v >> 32));
}

static inline uint32_t xhci_rt_read32(xhci_controller_t *hc, uint32_t off)
{
    return hal_mmio_read32((volatile void *)((uint8_t *)hc->rt_base + off));
}

static inline void xhci_rt_write32(xhci_controller_t *hc, uint32_t off, uint32_t v)
{
    hal_mmio_write32((volatile void *)((uint8_t *)hc->rt_base + off), v);
}

static inline void xhci_rt_write64(xhci_controller_t *hc, uint32_t off, uint64_t v)
{
    hal_mmio_write32((volatile void *)((uint8_t *)hc->rt_base + off), (uint32_t)v);
    hal_mmio_write32((volatile void *)((uint8_t *)hc->rt_base + off + 4), (uint32_t)(v >> 32));
}

/* Ring doorbell: register at db_base + slot*4, value = endpoint DCI */
static inline void xhci_ring_doorbell(xhci_controller_t *hc, uint8_t slot, uint32_t val)
{
    hal_mmio_write32((volatile void *)((uint8_t *)hc->db_base + slot * 4), val);
}

/* Read PORTSC for port (1-based) */
static inline uint32_t xhci_read_portsc(xhci_controller_t *hc, uint8_t port)
{
    return xhci_op_read32(hc, XHCI_OP_PORTSC_BASE + (port - 1) * 0x10);
}

static inline void xhci_write_portsc(xhci_controller_t *hc, uint8_t port, uint32_t v)
{
    xhci_op_write32(hc, XHCI_OP_PORTSC_BASE + (port - 1) * 0x10, v);
}

/* ── Command Ring ── */

static void xhci_cmd_ring_enqueue(xhci_controller_t *hc, xhci_trb_t *trb)
{
    volatile xhci_trb_t *slot = (volatile xhci_trb_t *)(
        (uint8_t *)hc->cmd_ring + hc->cmd_enqueue * sizeof(xhci_trb_t));

    /* Set cycle bit */
    uint32_t ctrl = trb->control;
    if (hc->cmd_cycle)
        ctrl |= XHCI_TRB_CYCLE;
    else
        ctrl &= ~XHCI_TRB_CYCLE;

    hal_mmio_write64((volatile void *)&slot->param, trb->param);
    hal_mmio_write32((volatile void *)&slot->status, trb->status);
    hal_mmio_barrier();
    hal_mmio_write32((volatile void *)&slot->control, ctrl);

    hc->cmd_enqueue++;
    if (hc->cmd_enqueue >= XHCI_CMD_RING_SIZE - 1) {
        /* Write Link TRB to wrap around */
        volatile xhci_trb_t *link = (volatile xhci_trb_t *)(
            (uint8_t *)hc->cmd_ring + hc->cmd_enqueue * sizeof(xhci_trb_t));
        uint32_t link_ctrl = XHCI_TRB_TYPE(XHCI_TRB_LINK) | (1u << 1); /* Toggle Cycle */
        if (hc->cmd_cycle) link_ctrl |= XHCI_TRB_CYCLE;
        hal_mmio_write64((volatile void *)&link->param, hc->cmd_ring_phys);
        hal_mmio_write32((volatile void *)&link->status, 0);
        hal_mmio_barrier();
        hal_mmio_write32((volatile void *)&link->control, link_ctrl);
        hc->cmd_enqueue = 0;
        hc->cmd_cycle ^= 1;
    }
}

/* ── Event Ring ── */

static hal_status_t xhci_poll_event(xhci_controller_t *hc, xhci_trb_t *out,
                                     uint32_t timeout_ms)
{
    uint64_t deadline = hal_timer_ms() + timeout_ms;

    while (hal_timer_ms() < deadline) {
        volatile xhci_trb_t *evt = (volatile xhci_trb_t *)(
            (uint8_t *)hc->evt_ring + hc->evt_dequeue * sizeof(xhci_trb_t));

        uint32_t ctrl = hal_mmio_read32((volatile void *)&evt->control);
        uint8_t cycle = ctrl & XHCI_TRB_CYCLE;

        if (cycle == hc->evt_cycle) {
            /* Valid event */
            out->param = hal_mmio_read64((volatile void *)&evt->param);
            out->status = hal_mmio_read32((volatile void *)&evt->status);
            out->control = ctrl;

            hc->evt_dequeue++;
            if (hc->evt_dequeue >= XHCI_EVT_RING_SIZE) {
                hc->evt_dequeue = 0;
                hc->evt_cycle ^= 1;
            }

            /* Update ERDP */
            uint64_t erdp = hc->evt_ring_phys +
                            hc->evt_dequeue * sizeof(xhci_trb_t);
            erdp |= (1u << 3);  /* Event Handler Busy = clear */
            xhci_rt_write64(hc, XHCI_RT_ERDP(0), erdp);

            return HAL_OK;
        }

        hal_timer_delay_us(XHCI_POLL_US);
    }

    return HAL_TIMEOUT;
}

/* Send a command and wait for completion event */
static hal_status_t xhci_send_command(xhci_controller_t *hc, xhci_trb_t *cmd,
                                       xhci_trb_t *result)
{
    xhci_cmd_ring_enqueue(hc, cmd);

    /* Ring doorbell 0 (host controller) with target 0 (command ring) */
    xhci_ring_doorbell(hc, 0, 0);

    /* Wait for Command Completion Event — skip other event types
     * (e.g., Port Status Change events from port reset) */
    uint64_t deadline = hal_timer_ms() + XHCI_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        hal_status_t st = xhci_poll_event(hc, result, XHCI_TIMEOUT_MS);
        if (st != HAL_OK)
            return st;

        uint8_t trb_type = XHCI_TRB_GET_TYPE(result->control);
        if (trb_type == XHCI_TRB_CMD_COMPLETE) {
            /* Check completion code */
            uint8_t cc = (result->status >> 24) & 0xFF;
            if (cc != XHCI_CC_SUCCESS) {
                hal_console_printf("[xhci] Command failed: cc=%u\n", cc);
                return HAL_ERROR;
            }
            return HAL_OK;
        }
        /* Skip non-command events (Port Status Change, etc.) */
    }

    return HAL_TIMEOUT;
}

/* ── Transfer Ring helpers ── */

static void xhci_xfer_enqueue(xhci_trb_t *ring, uint64_t ring_phys,
                                uint16_t *enqueue, uint8_t *cycle,
                                xhci_trb_t *trb, uint16_t ring_size)
{
    xhci_trb_t *slot = &ring[*enqueue];
    uint32_t ctrl = trb->control;
    if (*cycle) ctrl |= XHCI_TRB_CYCLE;
    else ctrl &= ~XHCI_TRB_CYCLE;

    slot->param = trb->param;
    slot->status = trb->status;
    slot->control = ctrl;

    (*enqueue)++;
    if (*enqueue >= ring_size - 1) {
        /* Link TRB */
        xhci_trb_t *link = &ring[*enqueue];
        uint32_t link_ctrl = XHCI_TRB_TYPE(XHCI_TRB_LINK) | (1u << 1);
        if (*cycle) link_ctrl |= XHCI_TRB_CYCLE;
        link->param = ring_phys;
        link->status = 0;
        link->control = link_ctrl;
        *enqueue = 0;
        *cycle ^= 1;
    }
}

/* ── Control Transfer ── */

static hal_status_t xhci_do_control(xhci_controller_t *hc, uint8_t slot_id,
                                     uint8_t bmRequestType, uint8_t bRequest,
                                     uint16_t wValue, uint16_t wIndex,
                                     uint16_t wLength, void *data, uint64_t data_phys)
{
    xhci_slot_t *slot = &hc->slots[slot_id - 1];
    if (!slot->active)
        return HAL_ERROR;

    /* Build Setup TRB */
    xhci_trb_t setup;
    memset(&setup, 0, sizeof(setup));
    /* Setup stage data: bmRequestType | bRequest<<8 | wValue<<16 */
    setup.param = (uint64_t)bmRequestType |
                  ((uint64_t)bRequest << 8) |
                  ((uint64_t)wValue << 16) |
                  ((uint64_t)wIndex << 32) |
                  ((uint64_t)wLength << 48);
    setup.status = 8;  /* TRB Transfer Length = 8 (Setup packet) */
    setup.control = XHCI_TRB_TYPE(XHCI_TRB_SETUP) | XHCI_TRB_IDT;
    /* TRT (Transfer Type): 0=No Data, 2=OUT Data, 3=IN Data */
    if (wLength > 0) {
        if (bmRequestType & 0x80)
            setup.control |= (3u << 16);  /* IN Data stage */
        else
            setup.control |= (2u << 16);  /* OUT Data stage */
    }

    xhci_xfer_enqueue(slot->ep0_ring, slot->ep0_ring_phys,
                       &slot->ep0_enqueue, &slot->ep0_cycle,
                       &setup, XHCI_XFER_RING_SIZE);

    /* Data TRB (if wLength > 0) */
    if (wLength > 0 && data_phys) {
        xhci_trb_t data_trb;
        memset(&data_trb, 0, sizeof(data_trb));
        data_trb.param = data_phys;
        data_trb.status = wLength;
        data_trb.control = XHCI_TRB_TYPE(XHCI_TRB_DATA);
        if (bmRequestType & 0x80)
            data_trb.control |= XHCI_TRB_DIR_IN;

        xhci_xfer_enqueue(slot->ep0_ring, slot->ep0_ring_phys,
                           &slot->ep0_enqueue, &slot->ep0_cycle,
                           &data_trb, XHCI_XFER_RING_SIZE);
    }

    /* Status TRB */
    xhci_trb_t status_trb;
    memset(&status_trb, 0, sizeof(status_trb));
    status_trb.control = XHCI_TRB_TYPE(XHCI_TRB_STATUS) | XHCI_TRB_IOC;
    /* Direction is opposite of data stage */
    if (wLength == 0 || !(bmRequestType & 0x80))
        status_trb.control |= XHCI_TRB_DIR_IN;

    xhci_xfer_enqueue(slot->ep0_ring, slot->ep0_ring_phys,
                       &slot->ep0_enqueue, &slot->ep0_cycle,
                       &status_trb, XHCI_XFER_RING_SIZE);

    /* Ring doorbell for EP0 (DCI = 1) */
    hal_mmio_barrier();
    xhci_ring_doorbell(hc, slot_id, 1);

    /* Wait for transfer event */
    xhci_trb_t event;
    hal_status_t st = xhci_poll_event(hc, &event, XHCI_TIMEOUT_MS);
    if (st != HAL_OK)
        return st;

    uint8_t cc = (event.status >> 24) & 0xFF;
    if (cc != XHCI_CC_SUCCESS && cc != XHCI_CC_SHORT_PKT)
        return HAL_ERROR;

    return HAL_OK;
}

/* ── Scratchpad setup ── */

static hal_status_t xhci_setup_scratchpad(xhci_controller_t *hc)
{
    uint32_t hcs2 = xhci_cap_read32(hc, XHCI_CAP_HCSPARAMS2);
    uint32_t max_sp_hi = (hcs2 >> 21) & 0x1F;
    uint32_t max_sp_lo = (hcs2 >> 27) & 0x1F;
    uint32_t max_sp = (max_sp_hi << 5) | max_sp_lo;

    if (max_sp == 0)
        return HAL_OK;

    /* Allocate scratchpad buffer array */
    uint64_t array_size = max_sp * sizeof(uint64_t);
    hc->scratchpad_array = (uint64_t *)hal_dma_alloc(array_size, &hc->scratchpad_phys);
    if (!hc->scratchpad_array)
        return HAL_NO_MEMORY;
    memset(hc->scratchpad_array, 0, array_size);

    /* Allocate individual scratchpad pages */
    for (uint32_t i = 0; i < max_sp; i++) {
        uint64_t page_phys;
        void *page = hal_dma_alloc(hc->page_size, &page_phys);
        if (!page)
            return HAL_NO_MEMORY;
        memset(page, 0, hc->page_size);
        hc->scratchpad_array[i] = page_phys;
    }

    /* DCBAA[0] points to scratchpad buffer array */
    hc->dcbaa[0] = hc->scratchpad_phys;

    return HAL_OK;
}

/* ── Public API ── */

hal_status_t xhci_init(xhci_controller_t *hc, hal_device_t *dev)
{
    hc->dev = *dev;
    hc->initialized = false;

    hal_bus_pci_enable(dev);

    hc->cap_base = hal_bus_map_bar(dev, 0);
    if (!hc->cap_base)
        return HAL_ERROR;

    /* Read capability registers */
    uint8_t cap_length = (uint8_t)xhci_cap_read32(hc, XHCI_CAP_CAPLENGTH);
    uint32_t hcs1 = xhci_cap_read32(hc, XHCI_CAP_HCSPARAMS1);
    uint32_t hcc1 = xhci_cap_read32(hc, XHCI_CAP_HCCPARAMS1);
    uint32_t dboff = xhci_cap_read32(hc, XHCI_CAP_DBOFF) & ~0x03;
    uint32_t rtsoff = xhci_cap_read32(hc, XHCI_CAP_RTSOFF) & ~0x1F;

    hc->max_slots = (uint8_t)(hcs1 & 0xFF);
    hc->max_ports = (uint8_t)((hcs1 >> 24) & 0xFF);
    if (hc->max_slots > XHCI_MAX_SLOTS) hc->max_slots = XHCI_MAX_SLOTS;
    if (hc->max_ports > XHCI_MAX_PORTS) hc->max_ports = XHCI_MAX_PORTS;

    /* Check context size (64-byte contexts if HCCPARAMS1 bit 2 set) */
    /* We only support 32-byte contexts for simplicity */
    (void)hcc1;

    /* Calculate register base addresses */
    hc->op_base = (volatile void *)((uint8_t *)hc->cap_base + cap_length);
    hc->rt_base = (volatile void *)((uint8_t *)hc->cap_base + rtsoff);
    hc->db_base = (volatile void *)((uint8_t *)hc->cap_base + dboff);

    /* Read page size */
    uint32_t ps = xhci_op_read32(hc, XHCI_OP_PAGESIZE);
    hc->page_size = (uint16_t)((ps & 0xFFFF) << 12);  /* Bit n set = 2^(n+12) */
    if (hc->page_size == 0) hc->page_size = 4096;

    /* ── Stop controller ── */
    uint32_t cmd = xhci_op_read32(hc, XHCI_OP_USBCMD);
    cmd &= ~XHCI_CMD_RUN;
    xhci_op_write32(hc, XHCI_OP_USBCMD, cmd);

    /* Wait for halted */
    uint64_t deadline = hal_timer_ms() + XHCI_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        if (xhci_op_read32(hc, XHCI_OP_USBSTS) & XHCI_STS_HCH)
            break;
        hal_timer_delay_us(XHCI_POLL_US);
    }

    /* ── Reset controller ── */
    xhci_op_write32(hc, XHCI_OP_USBCMD, XHCI_CMD_HCRST);
    deadline = hal_timer_ms() + XHCI_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t s = xhci_op_read32(hc, XHCI_OP_USBCMD);
        if (!(s & XHCI_CMD_HCRST))
            break;
        hal_timer_delay_us(XHCI_POLL_US);
    }

    /* Wait for CNR to clear */
    deadline = hal_timer_ms() + XHCI_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        if (!(xhci_op_read32(hc, XHCI_OP_USBSTS) & XHCI_STS_CNR))
            break;
        hal_timer_delay_us(XHCI_POLL_US);
    }

    /* ── Configure MaxSlotsEnabled ── */
    xhci_op_write32(hc, XHCI_OP_CONFIG, hc->max_slots);

    /* ── Allocate DCBAA ── */
    uint64_t dcbaa_size = (uint64_t)(hc->max_slots + 1) * sizeof(uint64_t);
    hc->dcbaa = (uint64_t *)hal_dma_alloc(dcbaa_size, &hc->dcbaa_phys);
    if (!hc->dcbaa)
        return HAL_NO_MEMORY;
    memset(hc->dcbaa, 0, dcbaa_size);
    xhci_op_write64(hc, XHCI_OP_DCBAAP, hc->dcbaa_phys);

    /* ── Setup scratchpad buffers ── */
    hal_status_t st = xhci_setup_scratchpad(hc);
    if (st != HAL_OK)
        return st;

    /* ── Allocate Command Ring ── */
    uint64_t cmd_ring_size = (uint64_t)XHCI_CMD_RING_SIZE * sizeof(xhci_trb_t);
    hc->cmd_ring = (xhci_trb_t *)hal_dma_alloc(cmd_ring_size, &hc->cmd_ring_phys);
    if (!hc->cmd_ring)
        return HAL_NO_MEMORY;
    memset(hc->cmd_ring, 0, cmd_ring_size);
    hc->cmd_enqueue = 0;
    hc->cmd_cycle = 1;

    /* Set CRCR (Command Ring Control Register) */
    xhci_op_write64(hc, XHCI_OP_CRCR, hc->cmd_ring_phys | XHCI_TRB_CYCLE);

    /* ── Allocate Event Ring ── */
    uint64_t evt_ring_size = (uint64_t)XHCI_EVT_RING_SIZE * sizeof(xhci_trb_t);
    hc->evt_ring = (xhci_trb_t *)hal_dma_alloc(evt_ring_size, &hc->evt_ring_phys);
    if (!hc->evt_ring)
        return HAL_NO_MEMORY;
    memset(hc->evt_ring, 0, evt_ring_size);
    hc->evt_dequeue = 0;
    hc->evt_cycle = 1;

    /* Allocate ERST (1 segment) */
    hc->erst = (xhci_erst_entry_t *)hal_dma_alloc(sizeof(xhci_erst_entry_t),
                                                    &hc->erst_phys);
    if (!hc->erst)
        return HAL_NO_MEMORY;
    hc->erst->base = hc->evt_ring_phys;
    hc->erst->size = XHCI_EVT_RING_SIZE;
    hc->erst->rsvd = 0;

    /* Configure Interrupter 0 */
    xhci_rt_write32(hc, XHCI_RT_ERSTSZ(0), 1);
    xhci_rt_write64(hc, XHCI_RT_ERDP(0), hc->evt_ring_phys);
    xhci_rt_write64(hc, XHCI_RT_ERSTBA(0), hc->erst_phys);

    /* Enable interrupter */
    uint32_t iman = xhci_rt_read32(hc, XHCI_RT_IMAN(0));
    iman |= 0x02;  /* Interrupt Enable */
    xhci_rt_write32(hc, XHCI_RT_IMAN(0), iman);

    /* ── Initialize slot array ── */
    for (uint32_t i = 0; i < XHCI_MAX_SLOTS; i++) {
        hc->slots[i].active = false;
    }

    /* ── Start controller ── */
    cmd = xhci_op_read32(hc, XHCI_OP_USBCMD);
    cmd |= XHCI_CMD_RUN | XHCI_CMD_INTE;
    xhci_op_write32(hc, XHCI_OP_USBCMD, cmd);

    /* Wait for not halted */
    deadline = hal_timer_ms() + XHCI_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        if (!(xhci_op_read32(hc, XHCI_OP_USBSTS) & XHCI_STS_HCH))
            break;
        hal_timer_delay_us(XHCI_POLL_US);
    }

    hc->initialized = true;
    return HAL_OK;
}

uint8_t xhci_port_reset(xhci_controller_t *hc, uint8_t port)
{
    if (!hc->initialized || port == 0 || port > hc->max_ports)
        return 0;

    /* Check if device is connected */
    uint32_t portsc = xhci_read_portsc(hc, port);
    if (!(portsc & XHCI_PORTSC_CCS))
        return 0;

    /* Issue port reset — preserve PP, clear change bits by writing 1 */
    portsc = XHCI_PORTSC_PP | XHCI_PORTSC_PR;
    xhci_write_portsc(hc, port, portsc);

    /* Wait for Port Reset Change (PRC) */
    uint64_t deadline = hal_timer_ms() + XHCI_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        portsc = xhci_read_portsc(hc, port);
        if (portsc & XHCI_PORTSC_PRC)
            break;
        hal_timer_delay_us(XHCI_POLL_US);
    }

    /* Clear PRC */
    xhci_write_portsc(hc, port, XHCI_PORTSC_PP | XHCI_PORTSC_PRC);

    /* Check enabled */
    portsc = xhci_read_portsc(hc, port);
    if (!(portsc & XHCI_PORTSC_PED))
        return 0;

    /* Get speed */
    uint8_t speed = (uint8_t)((portsc & XHCI_PORTSC_SPEED_MASK) >> 10);

    /* Enable Slot command */
    xhci_trb_t cmd, result;
    memset(&cmd, 0, sizeof(cmd));
    cmd.control = XHCI_TRB_TYPE(XHCI_TRB_ENABLE_SLOT);

    if (xhci_send_command(hc, &cmd, &result) != HAL_OK)
        return 0;

    uint8_t slot_id = (uint8_t)((result.control >> 24) & 0xFF);
    if (slot_id == 0 || slot_id > hc->max_slots)
        return 0;

    /* Initialize slot state */
    xhci_slot_t *slot = &hc->slots[slot_id - 1];
    slot->active = true;
    slot->slot_id = slot_id;
    slot->port = port;
    slot->speed = speed;

    /* Default max packet for EP0 based on speed */
    switch (speed) {
    case XHCI_SPEED_LOW:    slot->max_packet = 8;   break;
    case XHCI_SPEED_FULL:   slot->max_packet = 8;   break;
    case XHCI_SPEED_HIGH:   slot->max_packet = 64;  break;
    case XHCI_SPEED_SUPER:  slot->max_packet = 512; break;
    default:                 slot->max_packet = 8;   break;
    }

    /* Allocate EP0 Transfer Ring */
    uint64_t ring_sz = (uint64_t)XHCI_XFER_RING_SIZE * sizeof(xhci_trb_t);
    slot->ep0_ring = (xhci_trb_t *)hal_dma_alloc(ring_sz, &slot->ep0_ring_phys);
    if (!slot->ep0_ring) {
        slot->active = false;
        return 0;
    }
    memset(slot->ep0_ring, 0, ring_sz);
    slot->ep0_enqueue = 0;
    slot->ep0_cycle = 1;

    return slot_id;
}

hal_status_t xhci_address_device(xhci_controller_t *hc, uint8_t slot_id)
{
    if (slot_id == 0 || slot_id > hc->max_slots)
        return HAL_ERROR;
    xhci_slot_t *slot = &hc->slots[slot_id - 1];
    if (!slot->active)
        return HAL_ERROR;

    /* Allocate Input Context: Input Control Ctx (32B) + Slot Ctx (32B) + EP0 Ctx (32B) = 96B */
    uint64_t ictx_phys;
    uint8_t *ictx = (uint8_t *)hal_dma_alloc(4096, &ictx_phys);
    if (!ictx)
        return HAL_NO_MEMORY;
    memset(ictx, 0, 4096);

    /* Input Control Context at offset 0 */
    xhci_input_ctrl_ctx_t *icctx = (xhci_input_ctrl_ctx_t *)ictx;
    icctx->add_flags = (1u << 0) | (1u << 1);  /* Add Slot Ctx + EP0 Ctx */

    /* Slot Context at offset 32 */
    xhci_slot_ctx_t *sctx = (xhci_slot_ctx_t *)(ictx + XHCI_CONTEXT_SIZE);
    /* Context Entries = 1 (EP0), Speed, Root Hub Port Number */
    sctx->field1 = (1u << 27) | ((uint32_t)slot->speed << 20);
    sctx->field2 = ((uint32_t)slot->port << 16);

    /* EP0 Context at offset 64 */
    xhci_ep_ctx_t *ep0ctx = (xhci_ep_ctx_t *)(ictx + 2 * XHCI_CONTEXT_SIZE);
    /* EP Type = Control (4), CErr = 3, Max Packet Size */
    ep0ctx->field2 = (3u << 1) | (XHCI_EP_CONTROL << 3) |
                     ((uint32_t)slot->max_packet << 16);
    ep0ctx->dequeue = slot->ep0_ring_phys | 1;  /* DCS = 1 */
    ep0ctx->field4 = 8;  /* Average TRB Length for control = 8 */

    /* Allocate Output Device Context */
    uint64_t octx_phys;
    void *octx = hal_dma_alloc(4096, &octx_phys);
    if (!octx) {
        hal_dma_free(ictx, 4096);
        return HAL_NO_MEMORY;
    }
    memset(octx, 0, 4096);

    /* Set DCBAA entry */
    hc->dcbaa[slot_id] = octx_phys;

    /* Send Address Device command */
    xhci_trb_t cmd, result;
    memset(&cmd, 0, sizeof(cmd));
    cmd.param = ictx_phys;
    cmd.control = XHCI_TRB_TYPE(XHCI_TRB_ADDRESS_DEVICE) |
                  ((uint32_t)slot_id << 24);

    hal_status_t st = xhci_send_command(hc, &cmd, &result);

    hal_dma_free(ictx, 4096);

    return st;
}

hal_status_t xhci_get_device_desc(xhci_controller_t *hc, uint8_t slot_id,
                                   usb_device_desc_t *desc)
{
    uint64_t buf_phys;
    void *buf = hal_dma_alloc(sizeof(usb_device_desc_t), &buf_phys);
    if (!buf)
        return HAL_NO_MEMORY;
    memset(buf, 0, sizeof(usb_device_desc_t));

    hal_status_t st = xhci_do_control(hc, slot_id,
        0x80,                  /* Device-to-host, Standard, Device */
        USB_REQ_GET_DESCRIPTOR,
        (USB_DESC_DEVICE << 8) | 0,  /* wValue: type << 8 | index */
        0,                     /* wIndex */
        sizeof(usb_device_desc_t),
        buf, buf_phys);

    if (st == HAL_OK) {
        memcpy(desc, buf, sizeof(usb_device_desc_t));
        hc->slots[slot_id - 1].dev_desc = *desc;
        /* Update max packet size from descriptor */
        if (desc->bMaxPacketSize0 > 0)
            hc->slots[slot_id - 1].max_packet = desc->bMaxPacketSize0;
    }

    hal_dma_free(buf, sizeof(usb_device_desc_t));
    return st;
}

hal_status_t xhci_get_config_desc(xhci_controller_t *hc, uint8_t slot_id,
                                   void *buf, uint16_t buf_len)
{
    uint64_t dma_phys;
    void *dma_buf = hal_dma_alloc(buf_len, &dma_phys);
    if (!dma_buf)
        return HAL_NO_MEMORY;
    memset(dma_buf, 0, buf_len);

    hal_status_t st = xhci_do_control(hc, slot_id,
        0x80,
        USB_REQ_GET_DESCRIPTOR,
        (USB_DESC_CONFIG << 8) | 0,
        0,
        buf_len,
        dma_buf, dma_phys);

    if (st == HAL_OK)
        memcpy(buf, dma_buf, buf_len);

    hal_dma_free(dma_buf, buf_len);
    return st;
}

hal_status_t xhci_set_config(xhci_controller_t *hc, uint8_t slot_id,
                              uint8_t config_value)
{
    return xhci_do_control(hc, slot_id,
        0x00,                  /* Host-to-device, Standard, Device */
        USB_REQ_SET_CONFIG,
        config_value,
        0, 0, NULL, 0);
}

hal_status_t xhci_control_transfer(xhci_controller_t *hc, uint8_t slot_id,
                                    uint8_t bmRequestType, uint8_t bRequest,
                                    uint16_t wValue, uint16_t wIndex,
                                    uint16_t wLength, void *data)
{
    if (wLength == 0)
        return xhci_do_control(hc, slot_id, bmRequestType, bRequest,
                               wValue, wIndex, 0, NULL, 0);

    uint64_t dma_phys;
    void *dma_buf = hal_dma_alloc(wLength, &dma_phys);
    if (!dma_buf)
        return HAL_NO_MEMORY;

    /* If OUT transfer, copy data to DMA buffer */
    if (!(bmRequestType & 0x80) && data)
        memcpy(dma_buf, data, wLength);
    else
        memset(dma_buf, 0, wLength);

    hal_status_t st = xhci_do_control(hc, slot_id, bmRequestType, bRequest,
                                       wValue, wIndex, wLength,
                                       dma_buf, dma_phys);

    /* If IN transfer, copy data back */
    if (st == HAL_OK && (bmRequestType & 0x80) && data)
        memcpy(data, dma_buf, wLength);

    hal_dma_free(dma_buf, wLength);
    return st;
}

hal_status_t xhci_configure_interrupt_ep(xhci_controller_t *hc, uint8_t slot_id,
                                          uint8_t ep_num, uint16_t max_packet,
                                          uint8_t interval)
{
    if (slot_id == 0 || slot_id > hc->max_slots)
        return HAL_ERROR;
    xhci_slot_t *slot = &hc->slots[slot_id - 1];
    if (!slot->active)
        return HAL_ERROR;

    /* DCI for IN endpoint = ep_num * 2 + 1 */
    uint8_t dci = ep_num * 2 + 1;
    slot->int_ep_num = ep_num;
    slot->int_ep_dci = dci;

    /* Allocate interrupt transfer ring */
    uint64_t ring_sz = (uint64_t)XHCI_XFER_RING_SIZE * sizeof(xhci_trb_t);
    slot->int_ring = (xhci_trb_t *)hal_dma_alloc(ring_sz, &slot->int_ring_phys);
    if (!slot->int_ring)
        return HAL_NO_MEMORY;
    memset(slot->int_ring, 0, ring_sz);
    slot->int_enqueue = 0;
    slot->int_cycle = 1;

    /* Build Input Context for Configure Endpoint */
    uint64_t ictx_phys;
    uint8_t *ictx = (uint8_t *)hal_dma_alloc(4096, &ictx_phys);
    if (!ictx)
        return HAL_NO_MEMORY;
    memset(ictx, 0, 4096);

    xhci_input_ctrl_ctx_t *icctx = (xhci_input_ctrl_ctx_t *)ictx;
    icctx->add_flags = (1u << 0) | (1u << dci);  /* Slot Ctx + EP Ctx */

    /* Update Slot Context: Context Entries must include this endpoint */
    xhci_slot_ctx_t *sctx = (xhci_slot_ctx_t *)(ictx + XHCI_CONTEXT_SIZE);
    sctx->field1 = ((uint32_t)dci << 27) | ((uint32_t)slot->speed << 20);
    sctx->field2 = ((uint32_t)slot->port << 16);

    /* Endpoint Context at offset (1 + dci) * 32 */
    xhci_ep_ctx_t *epctx = (xhci_ep_ctx_t *)(ictx + (1 + dci) * XHCI_CONTEXT_SIZE);
    /* EP Type = Interrupt IN (7), CErr = 3, Max Packet, Interval */
    epctx->field1 = ((uint32_t)interval << 16);
    epctx->field2 = (3u << 1) | (XHCI_EP_INTERRUPT_IN << 3) |
                    ((uint32_t)max_packet << 16);
    epctx->dequeue = slot->int_ring_phys | 1;
    epctx->field4 = max_packet;  /* Average TRB Length */

    /* Configure Endpoint command */
    xhci_trb_t cmd, result;
    memset(&cmd, 0, sizeof(cmd));
    cmd.param = ictx_phys;
    cmd.control = XHCI_TRB_TYPE(XHCI_TRB_CONFIG_EP) |
                  ((uint32_t)slot_id << 24);

    hal_status_t st = xhci_send_command(hc, &cmd, &result);
    hal_dma_free(ictx, 4096);

    return st;
}

/* ── Port enumeration — scan all ports for connected devices ── */

hal_status_t xhci_enumerate_ports(xhci_controller_t *hc)
{
    if (!hc->initialized)
        return HAL_ERROR;

    uint8_t found = 0;
    for (uint8_t port = 1; port <= hc->max_ports; port++) {
        uint32_t portsc = xhci_read_portsc(hc, port);
        if (!(portsc & XHCI_PORTSC_CCS))
            continue;

        hal_console_printf("[xhci] Port %u: device connected (speed=%u)\n",
                           port, (portsc & XHCI_PORTSC_SPEED_MASK) >> 10);

        uint8_t slot_id = xhci_port_reset(hc, port);
        if (slot_id == 0) {
            hal_console_printf("[xhci] Port %u: reset/address failed\n", port);
            continue;
        }

        /* Address device */
        hal_status_t st = xhci_address_device(hc, slot_id);
        if (st != HAL_OK) {
            hal_console_printf("[xhci] Slot %u: address failed\n", slot_id);
            continue;
        }

        hal_console_printf("[xhci] Port %u: slot %u assigned\n", port, slot_id);
        found++;
    }

    return found > 0 ? HAL_OK : HAL_NO_DEVICE;
}

hal_status_t xhci_poll_interrupt(xhci_controller_t *hc, uint8_t slot_id,
                                  void *buf, uint16_t *length)
{
    if (slot_id == 0 || slot_id > hc->max_slots)
        return HAL_ERROR;
    xhci_slot_t *slot = &hc->slots[slot_id - 1];
    if (!slot->active || !slot->int_ring)
        return HAL_ERROR;

    /* Post a Normal TRB to receive data */
    uint64_t recv_phys;
    void *recv_buf = hal_dma_alloc(64, &recv_phys);
    if (!recv_buf)
        return HAL_NO_MEMORY;
    memset(recv_buf, 0, 64);

    xhci_trb_t normal;
    memset(&normal, 0, sizeof(normal));
    normal.param = recv_phys;
    normal.status = 64;  /* Transfer length */
    normal.control = XHCI_TRB_TYPE(XHCI_TRB_NORMAL) | XHCI_TRB_IOC;

    xhci_xfer_enqueue(slot->int_ring, slot->int_ring_phys,
                       &slot->int_enqueue, &slot->int_cycle,
                       &normal, XHCI_XFER_RING_SIZE);

    hal_mmio_barrier();
    xhci_ring_doorbell(hc, slot_id, slot->int_ep_dci);

    /* Poll for event (short timeout for polling mode) */
    xhci_trb_t event;
    hal_status_t st = xhci_poll_event(hc, &event, 100);
    if (st != HAL_OK) {
        hal_dma_free(recv_buf, 64);
        return HAL_NO_DEVICE;
    }

    uint8_t cc = (event.status >> 24) & 0xFF;
    uint16_t transferred = (uint16_t)(event.status & 0xFFFFFF);
    /* transferred = residual, actual = requested - residual */
    uint16_t actual = 64 - transferred;

    if (cc == XHCI_CC_SUCCESS || cc == XHCI_CC_SHORT_PKT) {
        if (actual > 0 && buf)
            memcpy(buf, recv_buf, actual);
        if (length)
            *length = actual;
        hal_dma_free(recv_buf, 64);
        return HAL_OK;
    }

    hal_dma_free(recv_buf, 64);
    return HAL_ERROR;
}

/* ── driver_ops_t wrapper for built-in driver registration ── */
#include "usb_hid.h"
#include "../../kernel/driver_loader.h"

static xhci_controller_t g_xhci_ctrl;
static usb_hid_dev_t     g_usb_kbd;
static bool              g_kbd_found;

static hal_status_t xhci_drv_init(hal_device_t *dev)
{
    g_kbd_found = false;

    hal_status_t st = xhci_init(&g_xhci_ctrl, dev);
    if (st != HAL_OK)
        return st;

    hal_console_printf("[xhci] Controller ready: %u slots, %u ports\n",
                       g_xhci_ctrl.max_slots, g_xhci_ctrl.max_ports);

    /* Enumerate ports and address devices */
    xhci_enumerate_ports(&g_xhci_ctrl);

    /* Look for a HID keyboard among addressed slots */
    for (uint8_t i = 0; i < g_xhci_ctrl.max_slots; i++) {
        if (!g_xhci_ctrl.slots[i].active)
            continue;

        uint8_t slot_id = g_xhci_ctrl.slots[i].slot_id;
        hal_status_t hid_rc = usb_hid_init(&g_usb_kbd, &g_xhci_ctrl, slot_id);
        if (hid_rc == HAL_OK && usb_hid_is_keyboard(&g_usb_kbd)) {
            hal_console_printf("[xhci] USB keyboard found on slot %u\n", slot_id);
            g_kbd_found = true;
            break;
        }
    }

    if (!g_kbd_found)
        hal_console_puts("[xhci] No USB keyboard detected\n");

    return HAL_OK;
}

static void xhci_drv_shutdown(void)
{
    /* Stop controller */
    if (g_xhci_ctrl.initialized) {
        uint32_t cmd = xhci_op_read32(&g_xhci_ctrl, XHCI_OP_USBCMD);
        cmd &= ~XHCI_CMD_RUN;
        xhci_op_write32(&g_xhci_ctrl, XHCI_OP_USBCMD, cmd);
    }
}

static int xhci_drv_input_poll(void)
{
    if (!g_kbd_found)
        return -1;

    /* Poll xHCI for new HID reports */
    usb_hid_poll(&g_usb_kbd);

    /* Check for a key event */
    hid_key_event_t evt;
    if (usb_hid_get_key(&g_usb_kbd, &evt)) {
        if (evt.pressed && evt.ascii)
            return (int)evt.ascii;
        if (evt.pressed)
            return (int)evt.keycode | 0x100;  /* Non-ASCII: high bit set */
    }
    return -1;
}

static const driver_ops_t xhci_driver_ops = {
    .name       = "xhci",
    .category   = DRIVER_CAT_INPUT,
    .init       = xhci_drv_init,
    .shutdown   = xhci_drv_shutdown,
    .input_poll = xhci_drv_input_poll,
};

void xhci_register(void)
{
    driver_register_builtin(&xhci_driver_ops);
}
