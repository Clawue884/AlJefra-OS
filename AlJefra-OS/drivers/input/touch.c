/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Touchscreen Input Framework Implementation
 * Multi-touch support via HID-over-I2C and HID-over-USB transports.
 * Architecture-independent; uses HAL for all hardware access.
 *
 * Transport overview:
 *   I2C: Follows Microsoft HID-over-I2C protocol. The touch controller sits
 *        on an I2C bus at a known slave address. We read its HID descriptor,
 *        parse the report descriptor for contact layouts, then poll for
 *        input reports containing multi-touch contact data.
 *   USB: Reuses the xHCI driver. The touch controller is a USB HID device
 *        with a digitizer usage page. We configure the interrupt endpoint
 *        and poll for reports.
 *
 * Gesture recognition runs on every event update, detecting:
 *   tap, double-tap, long-press, swipe (4 directions), pinch in/out.
 */

#include "touch.h"
#include "usb_hid.h"   /* USB_CLASS_HID, USB descriptor types */
#include "../../lib/string.h"

/* ── Internal helpers ── */

/* Integer square root (Newton's method) */
static uint32_t touch_isqrt(uint32_t n)
{
    if (n == 0) return 0;
    uint32_t x = n;
    uint32_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

/* Absolute value for int32_t */
static inline int32_t touch_abs(int32_t v)
{
    return (v < 0) ? -v : v;
}

/* Distance between two points */
static uint32_t touch_distance(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    int32_t dx = (int32_t)x2 - (int32_t)x1;
    int32_t dy = (int32_t)y2 - (int32_t)y1;
    return touch_isqrt((uint32_t)(dx * dx + dy * dy));
}

/* ── Event queue operations ── */

static void touch_push_event(touch_dev_t *dev, const touch_event_t *evt)
{
    uint8_t next = (dev->eq_head + 1);  /* Wraps at 256 naturally for uint8_t */
    if (next == dev->eq_tail)
        return;  /* Queue full, drop event */
    dev->event_queue[dev->eq_head] = *evt;
    dev->eq_head = next;
}

static void touch_push_gesture(touch_dev_t *dev, const touch_gesture_t *g)
{
    uint8_t next = (dev->gq_head + 1) & 0x0F;  /* 16-entry queue */
    if (next == dev->gq_tail)
        return;
    dev->gesture_queue[dev->gq_head] = *g;
    dev->gq_head = next;
}

/* ── I2C low-level operations ──
 *
 * These implement a minimal I2C master using MMIO registers.
 * The register layout follows a common pattern found in Designware I2C
 * controllers (used on many SoCs: Intel, Samsung Exynos, Rockchip, etc.).
 *
 * For platforms with different I2C controller IPs, these functions
 * would be replaced by a HAL I2C abstraction. For now, we implement
 * the Designware-compatible register set directly.
 */

/* Designware I2C register offsets */
#define I2C_CON         0x00    /* Control register */
#define I2C_TAR         0x04    /* Target address */
#define I2C_DATA_CMD    0x10    /* Data command register */
#define I2C_SS_SCL_HCNT 0x14    /* Standard speed SCL high count */
#define I2C_SS_SCL_LCNT 0x18    /* Standard speed SCL low count */
#define I2C_FS_SCL_HCNT 0x1C    /* Fast speed SCL high count */
#define I2C_FS_SCL_LCNT 0x20    /* Fast speed SCL low count */
#define I2C_INTR_STAT   0x2C    /* Interrupt status */
#define I2C_INTR_MASK   0x30    /* Interrupt mask */
#define I2C_RAW_INTR    0x34    /* Raw interrupt status */
#define I2C_CLR_INTR    0x40    /* Clear combined interrupt */
#define I2C_CLR_TX_ABRT 0x54    /* Clear TX abort */
#define I2C_ENABLE      0x6C    /* Enable register */
#define I2C_STATUS      0x70    /* Status register */
#define I2C_TXFLR       0x74    /* TX FIFO level */
#define I2C_RXFLR       0x78    /* RX FIFO level */
#define I2C_TX_ABRT_SRC 0x80    /* TX abort source */
#define I2C_COMP_PARAM1 0xF4    /* Component parameter */

/* I2C_CON bits */
#define I2C_CON_MASTER_MODE   (1u << 0)
#define I2C_CON_SPEED_STD     (1u << 1)
#define I2C_CON_SPEED_FAST    (2u << 1)
#define I2C_CON_10BIT_SLAVE   (1u << 3)
#define I2C_CON_10BIT_MASTER  (1u << 4)
#define I2C_CON_RESTART_EN    (1u << 5)
#define I2C_CON_SLAVE_DISABLE (1u << 6)

/* I2C_DATA_CMD bits */
#define I2C_DATA_CMD_READ     (1u << 8)
#define I2C_DATA_CMD_STOP     (1u << 9)
#define I2C_DATA_CMD_RESTART  (1u << 10)

/* I2C_STATUS bits */
#define I2C_STATUS_ACTIVITY   (1u << 0)
#define I2C_STATUS_TFNF       (1u << 1)   /* TX FIFO not full */
#define I2C_STATUS_TFE        (1u << 2)   /* TX FIFO empty */
#define I2C_STATUS_RFNE       (1u << 3)   /* RX FIFO not empty */

/* I2C timeout */
#define I2C_TIMEOUT_MS        100

static inline uint32_t i2c_read_reg(volatile void *base, uint32_t off)
{
    return hal_mmio_read32((volatile void *)((uint8_t *)base + off));
}

static inline void i2c_write_reg(volatile void *base, uint32_t off, uint32_t val)
{
    hal_mmio_write32((volatile void *)((uint8_t *)base + off), val);
}

/* Wait for TX FIFO to have space */
static hal_status_t i2c_wait_tx_ready(volatile void *base)
{
    uint64_t deadline = hal_timer_ms() + I2C_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        if (i2c_read_reg(base, I2C_STATUS) & I2C_STATUS_TFNF)
            return HAL_OK;
        hal_timer_delay_us(10);
    }
    return HAL_TIMEOUT;
}

/* Wait for RX FIFO to have data */
static hal_status_t i2c_wait_rx_ready(volatile void *base)
{
    uint64_t deadline = hal_timer_ms() + I2C_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        if (i2c_read_reg(base, I2C_STATUS) & I2C_STATUS_RFNE)
            return HAL_OK;
        hal_timer_delay_us(10);
    }
    return HAL_TIMEOUT;
}

/* Wait for I2C controller to become idle (no activity) */
static hal_status_t i2c_wait_idle(volatile void *base)
{
    uint64_t deadline = hal_timer_ms() + I2C_TIMEOUT_MS;
    while (hal_timer_ms() < deadline) {
        uint32_t st = i2c_read_reg(base, I2C_STATUS);
        if (!(st & I2C_STATUS_ACTIVITY) && (st & I2C_STATUS_TFE))
            return HAL_OK;
        hal_timer_delay_us(10);
    }
    return HAL_TIMEOUT;
}

/* Initialize I2C controller for the given slave address */
static hal_status_t i2c_init(volatile void *base, uint8_t addr)
{
    /* Disable controller before configuration */
    i2c_write_reg(base, I2C_ENABLE, 0);
    hal_timer_delay_us(100);

    /* Configure: master mode, fast speed (400kHz), 7-bit addressing,
     * restart enable, slave disabled */
    uint32_t con = I2C_CON_MASTER_MODE | I2C_CON_SPEED_FAST |
                   I2C_CON_RESTART_EN | I2C_CON_SLAVE_DISABLE;
    i2c_write_reg(base, I2C_CON, con);

    /* Set target address */
    i2c_write_reg(base, I2C_TAR, (uint32_t)addr);

    /* Set SCL timing for ~400kHz (assuming 100MHz input clock)
     * HCNT = 100, LCNT = 150 gives approximately 400kHz */
    i2c_write_reg(base, I2C_FS_SCL_HCNT, 100);
    i2c_write_reg(base, I2C_FS_SCL_LCNT, 150);

    /* Disable all interrupts (we use polling) */
    i2c_write_reg(base, I2C_INTR_MASK, 0);

    /* Enable controller */
    i2c_write_reg(base, I2C_ENABLE, 1);
    hal_timer_delay_us(100);

    return HAL_OK;
}

/* Write bytes to I2C slave, then read response bytes.
 * If read_len == 0, this is a write-only transaction. */
static hal_status_t i2c_write_read(volatile void *base,
                                    const uint8_t *write_buf, uint16_t write_len,
                                    uint8_t *read_buf, uint16_t read_len)
{
    hal_status_t st;

    /* Clear any pending abort */
    i2c_read_reg(base, I2C_CLR_TX_ABRT);

    /* Write phase */
    for (uint16_t i = 0; i < write_len; i++) {
        st = i2c_wait_tx_ready(base);
        if (st != HAL_OK) return st;

        uint32_t cmd = (uint32_t)write_buf[i];
        /* If this is the last byte and there is no read phase, add STOP */
        if (i == write_len - 1 && read_len == 0)
            cmd |= I2C_DATA_CMD_STOP;
        i2c_write_reg(base, I2C_DATA_CMD, cmd);
    }

    /* Read phase */
    for (uint16_t i = 0; i < read_len; i++) {
        st = i2c_wait_tx_ready(base);
        if (st != HAL_OK) return st;

        uint32_t cmd = I2C_DATA_CMD_READ;
        if (i == 0 && write_len > 0)
            cmd |= I2C_DATA_CMD_RESTART;
        if (i == read_len - 1)
            cmd |= I2C_DATA_CMD_STOP;
        i2c_write_reg(base, I2C_DATA_CMD, cmd);

        st = i2c_wait_rx_ready(base);
        if (st != HAL_OK) return st;

        read_buf[i] = (uint8_t)(i2c_read_reg(base, I2C_DATA_CMD) & 0xFF);
    }

    /* Wait for transaction to complete */
    return i2c_wait_idle(base);
}

/* ── I2C HID operations ── */

/* Read the HID descriptor from the I2C touch controller.
 * Per spec, the HID descriptor is at register 0x0001 (or 0x0020/0x0030
 * depending on device). We write the 16-bit register address then
 * read back the descriptor. */
static hal_status_t i2c_hid_read_descriptor(touch_dev_t *dev)
{
    /* The HID descriptor register address is typically 0x0001 for most
     * I2C-HID devices. Some use 0x0020 or 0x0030. We try 0x0001 first. */
    uint8_t reg_addr[2] = { 0x01, 0x00 };  /* Little-endian register 0x0001 */
    uint8_t buf[sizeof(i2c_hid_desc_t)];

    hal_status_t st = i2c_write_read(dev->i2c_base, reg_addr, 2,
                                      buf, sizeof(buf));
    if (st != HAL_OK)
        return st;

    memcpy(&dev->i2c_hid_desc, buf, sizeof(i2c_hid_desc_t));

    /* Validate: descriptor length should be at least 30 bytes */
    if (dev->i2c_hid_desc.wHIDDescLength < 30)
        return HAL_ERROR;

    /* Validate: BCD version should be 0x0100 */
    if (dev->i2c_hid_desc.bcdVersion != 0x0100)
        return HAL_ERROR;

    return HAL_OK;
}

/* Reset the I2C HID device */
static hal_status_t i2c_hid_reset(touch_dev_t *dev)
{
    uint8_t cmd[4];

    /* Write to command register: RESET command */
    cmd[0] = (uint8_t)(dev->i2c_hid_desc.wCommandRegister & 0xFF);
    cmd[1] = (uint8_t)(dev->i2c_hid_desc.wCommandRegister >> 8);
    cmd[2] = (uint8_t)(I2C_HID_CMD_RESET & 0xFF);
    cmd[3] = (uint8_t)(I2C_HID_CMD_RESET >> 8);

    hal_status_t st = i2c_write_read(dev->i2c_base, cmd, 4, NULL, 0);
    if (st != HAL_OK)
        return st;

    /* Wait for device to reset */
    hal_timer_delay_ms(100);

    return HAL_OK;
}

/* Set power state */
static hal_status_t i2c_hid_set_power(touch_dev_t *dev, uint16_t power_state)
{
    uint8_t cmd[4];

    cmd[0] = (uint8_t)(dev->i2c_hid_desc.wCommandRegister & 0xFF);
    cmd[1] = (uint8_t)(dev->i2c_hid_desc.wCommandRegister >> 8);
    uint16_t val = I2C_HID_CMD_SET_POWER | power_state;
    cmd[2] = (uint8_t)(val & 0xFF);
    cmd[3] = (uint8_t)(val >> 8);

    return i2c_write_read(dev->i2c_base, cmd, 4, NULL, 0);
}

/* ── HID Report Descriptor Parser ──
 *
 * A minimal parser for HID report descriptors to extract the multi-touch
 * contact layout. We look for:
 *   - Usage Page (Digitizer) -> Usage (Finger)
 *   - Tip Switch, Contact ID, X, Y, Pressure fields
 *   - Contact Count field
 *
 * This is a simplified parser that handles the most common touch controller
 * report descriptor formats (Goodix, FocalTech, Atmel, Synaptics, ELAN).
 */

/* Extract an unsigned value of 1-4 bytes from the report descriptor */
static uint32_t hid_desc_get_unsigned(const uint8_t *data, uint8_t size)
{
    uint32_t val = 0;
    for (uint8_t i = 0; i < size; i++)
        val |= ((uint32_t)data[i]) << (i * 8);
    return val;
}

/* Extract a signed value of 1-4 bytes from the report descriptor */
static int32_t hid_desc_get_signed(const uint8_t *data, uint8_t size)
{
    uint32_t val = hid_desc_get_unsigned(data, size);
    /* Sign-extend based on size */
    if (size == 1 && (val & 0x80))
        val |= 0xFFFFFF00;
    else if (size == 2 && (val & 0x8000))
        val |= 0xFFFF0000;
    return (int32_t)val;
}

static hal_status_t touch_parse_report_desc(touch_dev_t *dev,
                                             const uint8_t *desc, uint16_t len)
{
    touch_report_desc_t *rd = &dev->report_desc;
    memset(rd, 0, sizeof(*rd));

    /* HID report descriptor parsing state */
    uint16_t usage_page = 0;
    uint16_t usage = 0;
    int32_t  logical_min = 0;
    int32_t  logical_max = 0;
    uint16_t report_size = 0;
    uint16_t report_count = 0;
    uint8_t  report_id = 0;

    bool in_finger = false;          /* Inside a Finger collection */
    uint16_t bit_offset = 0;         /* Current bit position in report */
    uint8_t  contact_field_idx = 0;  /* Field index within contact */
    bool     found_contact_count = false;

    uint16_t pos = 0;
    while (pos < len) {
        uint8_t prefix = desc[pos];
        if (prefix == 0)
            break;

        /* Item type and size from prefix byte */
        uint8_t bSize = prefix & 0x03;
        uint8_t bType = (prefix >> 2) & 0x03;
        uint8_t bTag  = (prefix >> 4) & 0x0F;

        if (bSize == 3) bSize = 4;  /* Size 3 means 4 bytes */

        if (pos + 1 + bSize > len)
            break;

        const uint8_t *data = &desc[pos + 1];
        uint32_t uval = hid_desc_get_unsigned(data, bSize);
        int32_t  sval = hid_desc_get_signed(data, bSize);
        (void)sval;

        /* Main items (bType == 0) */
        if (bType == 0) {
            switch (bTag) {
            case 0x08: /* Input */
                if (in_finger && contact_field_idx < TOUCH_MAX_FIELDS) {
                    /* Record this field in the contact layout */
                    touch_report_field_t *f = &rd->contact.fields[contact_field_idx];
                    f->usage = (uint8_t)usage;
                    f->bit_offset = bit_offset;
                    f->bit_size = report_size;
                    f->logical_min = logical_min;
                    f->logical_max = logical_max;
                    contact_field_idx++;
                    rd->contact.field_count = contact_field_idx;
                }

                /* Check if this is the Contact Count field */
                if (!in_finger && usage_page == HID_USAGE_PAGE_DIGITIZER &&
                    usage == HID_USAGE_CONTACT_COUNT && !found_contact_count) {
                    rd->contact_count_offset = bit_offset;
                    rd->contact_count_bits = (uint8_t)report_size;
                    found_contact_count = true;
                }

                bit_offset += report_size * report_count;
                usage = 0;
                break;

            case 0x0A: /* Collection */
                if (usage_page == HID_USAGE_PAGE_DIGITIZER &&
                    usage == HID_USAGE_FINGER) {
                    in_finger = true;
                    contact_field_idx = 0;
                }
                break;

            case 0x0C: /* End Collection */
                if (in_finger) {
                    rd->contact.contact_bit_size = bit_offset;
                    in_finger = false;
                }
                break;
            }
        }
        /* Global items (bType == 1) */
        else if (bType == 1) {
            switch (bTag) {
            case 0x00: usage_page = (uint16_t)uval; break;       /* Usage Page */
            case 0x01: logical_min = (int32_t)uval; break;       /* Logical Minimum */
            case 0x02: logical_max = (int32_t)uval; break;       /* Logical Maximum */
            case 0x07: report_size = (uint16_t)uval; break;      /* Report Size */
            case 0x09: report_count = (uint16_t)uval; break;     /* Report Count */
            case 0x08: report_id = (uint8_t)uval;                /* Report ID */
                       rd->report_id = report_id;
                       if (bit_offset == 0) bit_offset = 8;      /* Report ID is 8 bits */
                       break;
            }
        }
        /* Local items (bType == 2) */
        else if (bType == 2) {
            switch (bTag) {
            case 0x00: usage = (uint16_t)uval; break;    /* Usage */
            }
        }

        pos += 1 + bSize;
    }

    rd->total_report_bits = bit_offset;

    /* Determine max contacts from parsed data, default to TOUCH_MAX_POINTS */
    rd->max_contacts = TOUCH_MAX_POINTS;

    /* Look for logical_max on contact count field to set max contacts */
    if (found_contact_count && logical_max > 0 && logical_max <= TOUCH_MAX_POINTS)
        rd->max_contacts = (uint8_t)logical_max;

    return HAL_OK;
}

/* Read and parse the HID report descriptor from I2C device */
static hal_status_t i2c_hid_read_report_desc(touch_dev_t *dev)
{
    uint16_t desc_len = dev->i2c_hid_desc.wReportDescLength;
    if (desc_len == 0 || desc_len > 512)
        return HAL_ERROR;

    /* We have a 256-byte report buffer. Use it temporarily for
     * the report descriptor (most touch descriptors are <256 bytes).
     * For longer descriptors, we read the first 256 bytes. */
    uint8_t *buf = dev->report_buf;
    uint16_t read_len = desc_len;
    if (read_len > 256)
        read_len = 256;

    /* Write the report descriptor register address, then read */
    uint8_t reg_addr[2];
    reg_addr[0] = (uint8_t)(dev->i2c_hid_desc.wReportDescRegister & 0xFF);
    reg_addr[1] = (uint8_t)(dev->i2c_hid_desc.wReportDescRegister >> 8);

    hal_status_t st = i2c_write_read(dev->i2c_base, reg_addr, 2,
                                      buf, read_len);
    if (st != HAL_OK)
        return st;

    return touch_parse_report_desc(dev, buf, read_len);
}

/* ── Extract a field value from a report buffer (bit-level access) ── */

static uint32_t touch_extract_field(const uint8_t *buf, uint16_t bit_offset,
                                     uint16_t bit_size)
{
    uint32_t val = 0;
    for (uint16_t i = 0; i < bit_size; i++) {
        uint16_t byte_idx = (bit_offset + i) / 8;
        uint8_t  bit_idx  = (bit_offset + i) % 8;
        if (buf[byte_idx] & (1u << bit_idx))
            val |= (1u << i);
    }
    return val;
}

/* ── Normalize a raw coordinate to screen coordinates ── */

static uint16_t touch_normalize(uint32_t raw, int32_t logical_min,
                                 int32_t logical_max, uint16_t screen_dim)
{
    if (logical_max <= logical_min)
        return 0;

    int32_t range = logical_max - logical_min;
    int32_t adj = (int32_t)raw - logical_min;
    if (adj < 0) adj = 0;
    if (adj > range) adj = range;

    return (uint16_t)(((uint32_t)adj * (uint32_t)(screen_dim - 1)) / (uint32_t)range);
}

/* ── Process a single parsed touch report ── */

static void touch_process_report(touch_dev_t *dev, const uint8_t *report,
                                  uint16_t report_len)
{
    touch_report_desc_t *rd = &dev->report_desc;

    /* Skip report ID byte if present */
    uint16_t data_offset = 0;
    if (rd->report_id != 0) {
        if (report_len < 1)
            return;
        /* Check report ID matches */
        if (report[0] != rd->report_id)
            return;
        data_offset = 1;
    }

    /* Read contact count */
    uint8_t contact_count = 0;
    if (rd->contact_count_bits > 0) {
        contact_count = (uint8_t)touch_extract_field(report + data_offset,
                                                      rd->contact_count_offset,
                                                      rd->contact_count_bits);
    }
    if (contact_count > TOUCH_MAX_POINTS)
        contact_count = TOUCH_MAX_POINTS;

    uint64_t now = hal_timer_ms();

    /* Track which contact IDs are still active */
    bool seen_id[TOUCH_MAX_POINTS];
    memset(seen_id, 0, sizeof(seen_id));

    /* Parse each contact in the report */
    uint16_t contact_bit_base = 0;

    /* Find the bit offset for the first contact data.
     * This is typically right after the contact count field. */
    if (rd->contact.field_count > 0) {
        contact_bit_base = rd->contact.fields[0].bit_offset;
    }

    for (uint8_t c = 0; c < contact_count; c++) {
        /* Parse fields for this contact */
        uint8_t  contact_id = 0;
        bool     tip_switch = false;
        uint32_t raw_x = 0;
        uint32_t raw_y = 0;
        uint32_t raw_pressure = 0;

        int32_t x_min = 0, x_max = 4095;
        int32_t y_min = 0, y_max = 4095;

        for (uint8_t f = 0; f < rd->contact.field_count; f++) {
            touch_report_field_t *field = &rd->contact.fields[f];
            uint16_t bit_off = contact_bit_base +
                               c * rd->contact.contact_bit_size +
                               (field->bit_offset - contact_bit_base);

            uint32_t val = touch_extract_field(report + data_offset,
                                                bit_off, field->bit_size);

            switch (field->usage) {
            case HID_USAGE_TIP_SWITCH:
                tip_switch = (val != 0);
                break;
            case HID_USAGE_CONTACT_ID:
                contact_id = (uint8_t)val;
                break;
            case HID_USAGE_X:
                raw_x = val;
                x_min = field->logical_min;
                x_max = field->logical_max;
                break;
            case HID_USAGE_Y:
                raw_y = val;
                y_min = field->logical_min;
                y_max = field->logical_max;
                break;
            case HID_USAGE_TIP_PRESSURE:
                raw_pressure = val;
                break;
            }
        }

        if (contact_id >= TOUCH_MAX_POINTS)
            continue;

        seen_id[contact_id] = true;

        /* Normalize coordinates */
        uint16_t x = touch_normalize(raw_x, x_min, x_max, dev->screen_width);
        uint16_t y = touch_normalize(raw_y, y_min, y_max, dev->screen_height);
        uint16_t pressure = (raw_pressure > 0) ? (uint16_t)raw_pressure : 0;

        touch_point_t *pt = &dev->points[contact_id];

        if (tip_switch) {
            touch_event_t evt;
            evt.timestamp = now;
            evt.point.id = contact_id;
            evt.point.x = x;
            evt.point.y = y;
            evt.point.pressure = pressure;
            evt.point.size = 0;
            evt.point.active = true;

            if (!pt->active) {
                /* New contact: DOWN event */
                evt.type = TOUCH_EVENT_DOWN;
                pt->active = true;
                pt->id = contact_id;
                pt->x = x;
                pt->y = y;
                pt->pressure = pressure;
                dev->active_count++;
            } else {
                /* Existing contact moved: MOVE event */
                evt.type = TOUCH_EVENT_MOVE;
                pt->x = x;
                pt->y = y;
                pt->pressure = pressure;
            }

            touch_push_event(dev, &evt);
        }
    }

    /* Generate UP events for contacts that disappeared */
    for (uint8_t i = 0; i < TOUCH_MAX_POINTS; i++) {
        if (dev->points[i].active && !seen_id[i]) {
            touch_event_t evt;
            evt.type = TOUCH_EVENT_UP;
            evt.timestamp = now;
            evt.point = dev->points[i];
            evt.point.active = false;
            touch_push_event(dev, &evt);

            dev->points[i].active = false;
            if (dev->active_count > 0)
                dev->active_count--;
        }
    }
}

/* ── Gesture Recognition ── */

static void touch_update_gestures(touch_dev_t *dev, const touch_event_t *evt)
{
    gesture_state_t *gs = &dev->gesture;
    uint64_t now = evt->timestamp;

    /* Single-finger gestures */
    if (evt->point.id == 0) {
        if (evt->type == TOUCH_EVENT_DOWN) {
            gs->last_down_time = now;
            gs->down_x = evt->point.x;
            gs->down_y = evt->point.y;
        }

        if (evt->type == TOUCH_EVENT_UP) {
            uint64_t duration = now - gs->last_down_time;
            uint32_t dist = touch_distance(gs->down_x, gs->down_y,
                                            evt->point.x, evt->point.y);

            if (duration >= GESTURE_LONG_PRESS_MIN &&
                dist < GESTURE_TAP_MAX_DISTANCE) {
                /* Long press */
                touch_gesture_t g;
                memset(&g, 0, sizeof(g));
                g.type = GESTURE_LONG_PRESS;
                g.center_x = evt->point.x;
                g.center_y = evt->point.y;
                g.timestamp = now;
                touch_push_gesture(dev, &g);
                gs->tap_pending = false;
            }
            else if (duration < GESTURE_TAP_MAX_DURATION &&
                     dist < GESTURE_TAP_MAX_DISTANCE) {
                /* Potential tap */
                if (gs->tap_pending &&
                    (now - gs->last_up_time) < GESTURE_DOUBLE_TAP_GAP) {
                    /* Double tap */
                    touch_gesture_t g;
                    memset(&g, 0, sizeof(g));
                    g.type = GESTURE_DOUBLE_TAP;
                    g.center_x = evt->point.x;
                    g.center_y = evt->point.y;
                    g.timestamp = now;
                    touch_push_gesture(dev, &g);
                    gs->tap_pending = false;
                    gs->tap_count = 0;
                } else {
                    /* First tap - mark as pending */
                    gs->tap_pending = true;
                    gs->tap_count = 1;
                }
            }
            else if (dist >= GESTURE_SWIPE_MIN_DISTANCE) {
                /* Swipe gesture */
                int32_t dx = (int32_t)evt->point.x - (int32_t)gs->down_x;
                int32_t dy = (int32_t)evt->point.y - (int32_t)gs->down_y;

                touch_gesture_t g;
                memset(&g, 0, sizeof(g));
                g.dx = dx;
                g.dy = dy;
                g.center_x = (gs->down_x + evt->point.x) / 2;
                g.center_y = (gs->down_y + evt->point.y) / 2;
                g.timestamp = now;

                /* Determine dominant direction */
                if (touch_abs(dx) > touch_abs(dy)) {
                    g.type = (dx > 0) ? GESTURE_SWIPE_RIGHT : GESTURE_SWIPE_LEFT;
                } else {
                    g.type = (dy > 0) ? GESTURE_SWIPE_DOWN : GESTURE_SWIPE_UP;
                }

                touch_push_gesture(dev, &g);
                gs->tap_pending = false;
            }

            gs->last_up_time = now;
        }
    }

    /* Two-finger pinch detection */
    if (dev->active_count == 2) {
        /* Find the two active points */
        touch_point_t *p0 = NULL, *p1 = NULL;
        for (uint8_t i = 0; i < TOUCH_MAX_POINTS; i++) {
            if (dev->points[i].active) {
                if (!p0)
                    p0 = &dev->points[i];
                else if (!p1)
                    p1 = &dev->points[i];
            }
        }

        if (p0 && p1) {
            uint32_t dist = touch_distance(p0->x, p0->y, p1->x, p1->y);

            if (!gs->two_finger_active) {
                /* Start tracking pinch */
                gs->two_finger_active = true;
                gs->initial_distance = dist;
                gs->pinch_center_x = (p0->x + p1->x) / 2;
                gs->pinch_center_y = (p0->y + p1->y) / 2;
            } else if (evt->type == TOUCH_EVENT_MOVE) {
                int32_t delta = (int32_t)dist - (int32_t)gs->initial_distance;

                if (touch_abs(delta) > GESTURE_PINCH_MIN_DELTA) {
                    touch_gesture_t g;
                    memset(&g, 0, sizeof(g));
                    g.type = (delta > 0) ? GESTURE_PINCH_OUT : GESTURE_PINCH_IN;
                    g.dx = delta;
                    g.center_x = (p0->x + p1->x) / 2;
                    g.center_y = (p0->y + p1->y) / 2;
                    g.timestamp = now;
                    touch_push_gesture(dev, &g);

                    /* Update baseline for continuous pinch */
                    gs->initial_distance = dist;
                }
            }
        }
    } else {
        gs->two_finger_active = false;
    }

    /* Check for pending single tap timeout */
    if (gs->tap_pending &&
        (now - gs->last_up_time) >= GESTURE_DOUBLE_TAP_GAP) {
        /* Timeout: emit single tap */
        touch_gesture_t g;
        memset(&g, 0, sizeof(g));
        g.type = GESTURE_TAP;
        g.center_x = gs->down_x;
        g.center_y = gs->down_y;
        g.timestamp = gs->last_up_time;
        touch_push_gesture(dev, &g);
        gs->tap_pending = false;
        gs->tap_count = 0;
    }
}

/* ── I2C transport: read input report ── */

static hal_status_t touch_i2c_read_report(touch_dev_t *dev)
{
    /* Read from the input register.
     * The first 2 bytes are the length prefix (little-endian). */
    uint8_t reg_addr[2];
    reg_addr[0] = (uint8_t)(dev->i2c_hid_desc.wInputRegister & 0xFF);
    reg_addr[1] = (uint8_t)(dev->i2c_hid_desc.wInputRegister >> 8);

    /* Read the length prefix first */
    uint8_t len_buf[2];
    hal_status_t st = i2c_write_read(dev->i2c_base, reg_addr, 2, len_buf, 2);
    if (st != HAL_OK)
        return st;

    uint16_t report_len = (uint16_t)len_buf[0] | ((uint16_t)len_buf[1] << 8);

    /* Length of 0 or 0xFFFF means no data */
    if (report_len == 0 || report_len == 0xFFFF)
        return HAL_NO_DEVICE;

    /* Subtract the 2-byte length prefix */
    if (report_len <= 2)
        return HAL_NO_DEVICE;
    report_len -= 2;

    /* Limit to our buffer size */
    if (report_len > sizeof(dev->report_buf))
        report_len = sizeof(dev->report_buf);

    /* Read the full report */
    st = i2c_write_read(dev->i2c_base, reg_addr, 2,
                         dev->report_buf, report_len + 2);
    if (st != HAL_OK)
        return st;

    /* Process the report data (skip the 2-byte length prefix) */
    touch_process_report(dev, dev->report_buf + 2, report_len);

    return HAL_OK;
}

/* ── USB transport: read input report via xHCI interrupt endpoint ── */

static hal_status_t touch_usb_read_report(touch_dev_t *dev)
{
    uint16_t length = 0;
    hal_status_t st = xhci_poll_interrupt(dev->usb_hc, dev->usb_slot_id,
                                           dev->report_buf, &length);
    if (st != HAL_OK)
        return st;
    if (length == 0)
        return HAL_NO_DEVICE;

    touch_process_report(dev, dev->report_buf, length);
    return HAL_OK;
}

/* ── I2C Bus Scan ── */

/* Common I2C addresses used by touch controllers:
 * Goodix:     0x5D, 0x14
 * FocalTech:  0x38
 * Atmel MXT:  0x4A, 0x4B
 * Synaptics:  0x20, 0x2C
 * ELAN:       0x10
 * Hideep:     0x6C
 * Ilitek:     0x41
 * Novatek:    0x01, 0x62
 */
static const uint8_t touch_i2c_addrs[] = {
    0x5D, 0x14, 0x38, 0x4A, 0x4B, 0x20, 0x2C,
    0x10, 0x6C, 0x41, 0x01, 0x62
};
#define TOUCH_NUM_SCAN_ADDRS (sizeof(touch_i2c_addrs) / sizeof(touch_i2c_addrs[0]))

uint8_t touch_i2c_scan(volatile void *i2c_base)
{
    for (uint32_t i = 0; i < TOUCH_NUM_SCAN_ADDRS; i++) {
        uint8_t addr = touch_i2c_addrs[i];

        /* Initialize I2C for this address */
        hal_status_t st = i2c_init(i2c_base, addr);
        if (st != HAL_OK)
            continue;

        /* Try to read the HID descriptor register (0x0001) */
        uint8_t reg[2] = { 0x01, 0x00 };
        uint8_t resp[2];

        st = i2c_write_read(i2c_base, reg, 2, resp, 2);
        if (st == HAL_OK) {
            /* Got a response -- device is present */
            hal_console_printf("[touch] Found I2C device at 0x%02x\n", addr);
            return addr;
        }

        /* Delay between probe attempts */
        hal_timer_delay_ms(5);
    }

    return 0;  /* Not found */
}

/* ── Public API Implementation ── */

hal_status_t touch_init_i2c(touch_dev_t *dev, volatile void *i2c_base,
                             uint8_t i2c_addr,
                             uint16_t screen_w, uint16_t screen_h)
{
    memset(dev, 0, sizeof(*dev));
    dev->transport = TOUCH_TRANSPORT_I2C;
    dev->i2c_base = i2c_base;
    dev->i2c_addr = i2c_addr;
    dev->screen_width = screen_w;
    dev->screen_height = screen_h;

    hal_console_printf("[touch] Initializing I2C touch at addr 0x%02x\n", i2c_addr);

    /* Initialize I2C controller */
    hal_status_t st = i2c_init(i2c_base, i2c_addr);
    if (st != HAL_OK) {
        hal_console_puts("[touch] I2C init failed\n");
        return st;
    }

    /* Read HID descriptor */
    st = i2c_hid_read_descriptor(dev);
    if (st != HAL_OK) {
        hal_console_puts("[touch] HID descriptor read failed\n");
        return st;
    }

    hal_console_printf("[touch] HID desc: vendor=%04x product=%04x\n",
                       dev->i2c_hid_desc.wVendorID,
                       dev->i2c_hid_desc.wProductID);

    /* Power on */
    st = i2c_hid_set_power(dev, I2C_HID_POWER_ON);
    if (st != HAL_OK) {
        hal_console_puts("[touch] Set power failed\n");
        return st;
    }

    /* Reset device */
    st = i2c_hid_reset(dev);
    if (st != HAL_OK) {
        hal_console_puts("[touch] Reset failed\n");
        return st;
    }

    /* Read and parse report descriptor */
    st = i2c_hid_read_report_desc(dev);
    if (st != HAL_OK) {
        hal_console_puts("[touch] Report descriptor parse failed\n");
        return st;
    }

    hal_console_printf("[touch] Multi-touch: max %u contacts\n",
                       dev->report_desc.max_contacts);

    dev->initialized = true;
    hal_console_puts("[touch] I2C touchscreen initialized\n");
    return HAL_OK;
}

hal_status_t touch_init_usb(touch_dev_t *dev, xhci_controller_t *hc,
                             uint8_t slot_id,
                             uint16_t screen_w, uint16_t screen_h)
{
    memset(dev, 0, sizeof(*dev));
    dev->transport = TOUCH_TRANSPORT_USB;
    dev->usb_hc = hc;
    dev->usb_slot_id = slot_id;
    dev->screen_width = screen_w;
    dev->screen_height = screen_h;

    hal_console_puts("[touch] Initializing USB touchscreen\n");

    /* Get device descriptor */
    usb_device_desc_t dev_desc;
    hal_status_t st = xhci_get_device_desc(hc, slot_id, &dev_desc);
    if (st != HAL_OK) {
        hal_console_puts("[touch] USB device descriptor read failed\n");
        return st;
    }

    /* Get configuration descriptor */
    uint8_t config_buf[256];
    memset(config_buf, 0, sizeof(config_buf));
    st = xhci_get_config_desc(hc, slot_id, config_buf, sizeof(config_buf));
    if (st != HAL_OK) {
        hal_console_puts("[touch] USB config descriptor read failed\n");
        return st;
    }

    /* Parse config descriptor to find HID interface with digitizer usage */
    usb_config_desc_t *cfg = (usb_config_desc_t *)config_buf;
    uint16_t total_len = cfg->wTotalLength;
    if (total_len > sizeof(config_buf))
        total_len = sizeof(config_buf);

    bool found_hid = false;
    uint16_t offset = cfg->bLength;

    while (offset + 2 <= total_len) {
        uint8_t desc_len = config_buf[offset];
        uint8_t desc_type = config_buf[offset + 1];

        if (desc_len == 0)
            break;

        /* Look for HID interface with digitizer protocol (or any HID
         * that is not keyboard/mouse -- protocol 0 = None/Custom) */
        if (desc_type == USB_DESC_INTERFACE && desc_len >= 9) {
            usb_interface_desc_t *iface = (usb_interface_desc_t *)&config_buf[offset];
            if (iface->bInterfaceClass == USB_CLASS_HID &&
                iface->bInterfaceProtocol == 0x00) {
                /* Custom protocol -- could be touch */
                found_hid = true;
            }
        }

        /* Find interrupt IN endpoint */
        if (found_hid && desc_type == USB_DESC_ENDPOINT && desc_len >= 7) {
            usb_endpoint_desc_t *ep = (usb_endpoint_desc_t *)&config_buf[offset];
            if ((ep->bmAttributes & 0x03) == 0x03 &&
                (ep->bEndpointAddress & 0x80)) {
                dev->usb_ep_num = ep->bEndpointAddress & 0x0F;
                dev->usb_ep_max_packet = ep->wMaxPacketSize;
                break;
            }
        }

        offset += desc_len;
    }

    if (!found_hid || dev->usb_ep_num == 0) {
        hal_console_puts("[touch] No HID touch interface found\n");
        return HAL_NO_DEVICE;
    }

    /* Set configuration */
    st = xhci_set_config(hc, slot_id, cfg->bConfigurationValue);
    if (st != HAL_OK)
        return st;

    /* Configure interrupt endpoint */
    st = xhci_configure_interrupt_ep(hc, slot_id, dev->usb_ep_num,
                                      dev->usb_ep_max_packet, 8);
    if (st != HAL_OK) {
        hal_console_puts("[touch] Interrupt endpoint config failed\n");
        return st;
    }

    /* Read HID report descriptor via USB control transfer.
     * GET_DESCRIPTOR (Class), wValue = 0x2200 (HID Report), wIndex = 0 */
    uint16_t hid_report_len = 256;  /* Default; ideally from HID descriptor */

    /* Try to find HID descriptor for the report descriptor length */
    offset = cfg->bLength;
    while (offset + 2 <= total_len) {
        uint8_t dl = config_buf[offset];
        uint8_t dt = config_buf[offset + 1];
        if (dl == 0) break;
        if (dt == USB_DESC_HID && dl >= 9) {
            /* Bytes 7-8 are report descriptor length */
            hid_report_len = (uint16_t)config_buf[offset + 7] |
                             ((uint16_t)config_buf[offset + 8] << 8);
            break;
        }
        offset += dl;
    }

    if (hid_report_len > sizeof(dev->report_buf))
        hid_report_len = sizeof(dev->report_buf);

    st = xhci_control_transfer(hc, slot_id,
        0x81,                             /* Device-to-Host, Standard, Interface */
        0x06,                             /* GET_DESCRIPTOR */
        0x2200,                           /* wValue: HID Report descriptor */
        0,                                /* wIndex: interface 0 */
        hid_report_len, dev->report_buf);
    if (st != HAL_OK) {
        hal_console_puts("[touch] HID report descriptor read failed\n");
        return st;
    }

    /* Parse report descriptor */
    st = touch_parse_report_desc(dev, dev->report_buf, hid_report_len);
    if (st != HAL_OK) {
        hal_console_puts("[touch] Report descriptor parse failed\n");
        return st;
    }

    hal_console_printf("[touch] USB touch: max %u contacts\n",
                       dev->report_desc.max_contacts);

    dev->initialized = true;
    hal_console_puts("[touch] USB touchscreen initialized\n");
    return HAL_OK;
}

hal_status_t touch_poll(touch_dev_t *dev)
{
    if (!dev->initialized)
        return HAL_ERROR;

    hal_status_t st;

    if (dev->transport == TOUCH_TRANSPORT_I2C) {
        st = touch_i2c_read_report(dev);
    } else {
        st = touch_usb_read_report(dev);
    }

    /* Run gesture recognition on any new events */
    /* We process events as they were enqueued during this poll */
    uint8_t tail = dev->eq_tail;
    while (tail != dev->eq_head) {
        touch_update_gestures(dev, &dev->event_queue[tail]);
        tail++;
    }

    /* Even if no report was available, check gesture timeouts */
    if (dev->gesture.tap_pending) {
        uint64_t now = hal_timer_ms();
        if ((now - dev->gesture.last_up_time) >= GESTURE_DOUBLE_TAP_GAP) {
            touch_gesture_t g;
            memset(&g, 0, sizeof(g));
            g.type = GESTURE_TAP;
            g.center_x = dev->gesture.down_x;
            g.center_y = dev->gesture.down_y;
            g.timestamp = dev->gesture.last_up_time;
            touch_push_gesture(dev, &g);
            dev->gesture.tap_pending = false;
            dev->gesture.tap_count = 0;
        }
    }

    return st;
}

bool touch_get_event(touch_dev_t *dev, touch_event_t *event)
{
    if (dev->eq_head == dev->eq_tail)
        return false;
    *event = dev->event_queue[dev->eq_tail];
    dev->eq_tail++;  /* Wraps naturally for uint8_t (256 entries) */
    return true;
}

bool touch_get_gesture(touch_dev_t *dev, touch_gesture_t *gesture)
{
    if (dev->gq_head == dev->gq_tail)
        return false;
    *gesture = dev->gesture_queue[dev->gq_tail];
    dev->gq_tail = (dev->gq_tail + 1) & 0x0F;
    return true;
}

uint8_t touch_get_points(touch_dev_t *dev, touch_point_t *out, uint8_t max)
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < TOUCH_MAX_POINTS && count < max; i++) {
        if (dev->points[i].active) {
            out[count] = dev->points[i];
            count++;
        }
    }
    return count;
}

void touch_set_resolution(touch_dev_t *dev, uint16_t w, uint16_t h)
{
    dev->screen_width = w;
    dev->screen_height = h;
}

/* ── driver_ops_t wrapper for driver_loader registration ── */

#include "../../kernel/driver_loader.h"

static touch_dev_t g_touch_dev;

static hal_status_t touch_drv_init(hal_device_t *dev)
{
    /* For platform devices (I2C), the device will be discovered via
     * Device Tree or ACPI. For USB, we look for HID devices.
     * At init time, attempt I2C scan first, then fall back to USB. */

    /* Check if this is a USB HID device */
    if (dev->bus_type == HAL_BUS_PCIE &&
        dev->class_code == 0x0C &&    /* Serial Bus */
        dev->subclass == 0x03) {      /* USB */
        /* This is a USB controller -- touch would be behind xHCI */
        hal_console_puts("[touch] USB touch: requires xHCI enumeration\n");
        return HAL_NOT_SUPPORTED;
    }

    /* For DT/ACPI-discovered I2C devices */
    if (dev->bus_type == HAL_BUS_DT || dev->bus_type == HAL_BUS_MMIO) {
        volatile void *i2c_base = (volatile void *)(uintptr_t)dev->bar[0];
        if (!i2c_base)
            return HAL_NO_DEVICE;

        /* Scan for touch controller on this I2C bus */
        uint8_t addr = touch_i2c_scan(i2c_base);
        if (addr == 0) {
            hal_console_puts("[touch] No touch controller found on I2C bus\n");
            return HAL_NO_DEVICE;
        }

        /* Default resolution -- will be updated when display is ready */
        return touch_init_i2c(&g_touch_dev, i2c_base, addr, 1920, 1080);
    }

    return HAL_NOT_SUPPORTED;
}

static void touch_drv_shutdown(void)
{
    g_touch_dev.initialized = false;
}

/* input_poll returns a synthetic keycode encoding touch events.
 * Encoding: bits [15:14] = event type (0=DOWN,1=UP,2=MOVE)
 *           bits [13:10] = contact ID (0-9)
 *           bits  [9:0]  = X coordinate (scaled to 0-1023)
 * Returns -1 if no events pending.
 *
 * This is a compatibility shim; for full touch data, use
 * touch_get_event() and touch_get_gesture() directly. */
static int touch_drv_poll(void)
{
    touch_poll(&g_touch_dev);

    touch_event_t evt;
    if (!touch_get_event(&g_touch_dev, &evt))
        return -1;

    /* Encode into a 16-bit "keycode" for the driver_ops_t interface */
    int code = 0;
    code |= ((int)evt.type & 0x03) << 14;
    code |= ((int)evt.point.id & 0x0F) << 10;
    code |= ((int)evt.point.x * 1023 / (g_touch_dev.screen_width > 0 ? g_touch_dev.screen_width : 1)) & 0x3FF;

    return code;
}

static const driver_ops_t touch_driver_ops = {
    .name       = "touch",
    .category   = DRIVER_CAT_INPUT,
    .init       = touch_drv_init,
    .shutdown   = touch_drv_shutdown,
    .input_poll = touch_drv_poll,
};

void touch_register(void)
{
    driver_register_builtin(&touch_driver_ops);
}
