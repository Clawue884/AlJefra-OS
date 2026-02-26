/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Touchscreen Input Framework
 * Multi-touch support via HID-over-I2C and HID-over-USB transports.
 * Architecture-independent; uses HAL for all hardware access.
 */

#ifndef ALJEFRA_DRV_TOUCH_H
#define ALJEFRA_DRV_TOUCH_H

#include "../../hal/hal.h"
#include "xhci.h"

/* ── Touch Constants ── */

#define TOUCH_MAX_POINTS       10   /* Maximum simultaneous touch points */
#define TOUCH_EVENT_QUEUE_SIZE 256  /* Ring buffer capacity */

/* ── Touch Event Types ── */

typedef enum {
    TOUCH_EVENT_DOWN = 0,   /* Finger placed on screen */
    TOUCH_EVENT_UP   = 1,   /* Finger lifted from screen */
    TOUCH_EVENT_MOVE = 2,   /* Finger moved while touching */
} touch_event_type_t;

/* ── Gesture Types ── */

typedef enum {
    GESTURE_NONE        = 0,
    GESTURE_TAP         = 1,
    GESTURE_DOUBLE_TAP  = 2,
    GESTURE_LONG_PRESS  = 3,
    GESTURE_SWIPE_UP    = 4,
    GESTURE_SWIPE_DOWN  = 5,
    GESTURE_SWIPE_LEFT  = 6,
    GESTURE_SWIPE_RIGHT = 7,
    GESTURE_PINCH_IN    = 8,   /* Zoom out */
    GESTURE_PINCH_OUT   = 9,   /* Zoom in */
} gesture_type_t;

/* ── Touch Transport Type ── */

typedef enum {
    TOUCH_TRANSPORT_I2C = 0,   /* HID-over-I2C (common on mobile) */
    TOUCH_TRANSPORT_USB = 1,   /* HID-over-USB (via xHCI) */
} touch_transport_t;

/* ── Touch Point ── */

typedef struct {
    uint8_t   id;         /* Contact ID (0-9) */
    uint16_t  x;          /* Normalized X coordinate (0 - screen_width-1) */
    uint16_t  y;          /* Normalized Y coordinate (0 - screen_height-1) */
    uint16_t  pressure;   /* Pressure (0 = no pressure, 0xFFFF = max) */
    uint16_t  size;       /* Contact area (0 = unknown) */
    bool      active;     /* true if this slot currently has a finger */
} touch_point_t;

/* ── Touch Event ── */

typedef struct {
    touch_event_type_t type;        /* DOWN, UP, or MOVE */
    touch_point_t      point;       /* The touch point that changed */
    uint64_t           timestamp;   /* hal_timer_ms() at event time */
} touch_event_t;

/* ── Gesture Event ── */

typedef struct {
    gesture_type_t type;
    int32_t        dx;          /* Swipe delta X or pinch scale change */
    int32_t        dy;          /* Swipe delta Y */
    uint16_t       center_x;   /* Gesture center X */
    uint16_t       center_y;   /* Gesture center Y */
    uint64_t       timestamp;
} touch_gesture_t;

/* ── I2C HID Descriptor (per Microsoft HID-over-I2C specification) ── */

typedef struct __attribute__((packed)) {
    uint16_t wHIDDescLength;       /* Length of this descriptor */
    uint16_t bcdVersion;           /* HID-over-I2C spec version (typically 0x0100) */
    uint16_t wReportDescLength;    /* Length of the HID report descriptor */
    uint16_t wReportDescRegister;  /* Register to read report descriptor */
    uint16_t wInputRegister;       /* Register to read input reports */
    uint16_t wMaxInputLength;      /* Max input report length */
    uint16_t wOutputRegister;      /* Register to write output reports */
    uint16_t wMaxOutputLength;     /* Max output report length */
    uint16_t wCommandRegister;     /* Register for HID commands */
    uint16_t wDataRegister;        /* Register for command data */
    uint16_t wVendorID;            /* Vendor ID */
    uint16_t wProductID;           /* Product ID */
    uint16_t wVersionID;           /* Version ID */
    uint32_t reserved;
} i2c_hid_desc_t;

/* ── I2C HID Commands ── */

#define I2C_HID_CMD_RESET      0x0100
#define I2C_HID_CMD_GET_REPORT 0x0200
#define I2C_HID_CMD_SET_REPORT 0x0300
#define I2C_HID_CMD_SET_POWER  0x0800
#define I2C_HID_POWER_ON       0x0000
#define I2C_HID_POWER_SLEEP    0x0001

/* ── HID Report Descriptor Usage Pages & Usages (Digitizer) ── */

#define HID_USAGE_PAGE_DIGITIZER   0x0D
#define HID_USAGE_TOUCH_SCREEN     0x04
#define HID_USAGE_FINGER           0x22
#define HID_USAGE_TIP_SWITCH       0x42
#define HID_USAGE_CONTACT_ID       0x51
#define HID_USAGE_CONTACT_COUNT    0x54
#define HID_USAGE_X                0x30   /* Generic Desktop X */
#define HID_USAGE_Y                0x31   /* Generic Desktop Y */
#define HID_USAGE_TIP_PRESSURE     0xD0   /* Digitizer tip pressure (internal, page-qualified) */

/* Maximum contact points parsed from report descriptor */
#define TOUCH_MAX_REPORT_CONTACTS  10

/* ── Report field descriptor (parsed from HID report descriptor) ── */

typedef struct {
    uint8_t  usage;        /* HID usage (e.g., CONTACT_ID, TIP_SWITCH) */
    uint16_t bit_offset;   /* Bit offset within the report */
    uint16_t bit_size;     /* Field size in bits */
    int32_t  logical_min;  /* Logical minimum */
    int32_t  logical_max;  /* Logical maximum */
} touch_report_field_t;

/* Maximum fields we parse from a single contact */
#define TOUCH_MAX_FIELDS   8

/* ── Per-contact report layout (parsed) ── */

typedef struct {
    touch_report_field_t fields[TOUCH_MAX_FIELDS];
    uint8_t              field_count;
    uint16_t             contact_bit_size;   /* Total bits per contact */
} touch_contact_layout_t;

/* ── Parsed report descriptor ── */

typedef struct {
    touch_contact_layout_t  contact;
    uint8_t                 max_contacts;          /* Max contacts from descriptor */
    uint16_t                contact_count_offset;  /* Bit offset of contact count */
    uint8_t                 contact_count_bits;    /* Bit size of contact count */
    uint16_t                report_id;             /* Report ID (0 if none) */
    uint16_t                total_report_bits;     /* Total report size in bits */
} touch_report_desc_t;

/* ── Gesture recognition state ── */

typedef struct {
    /* Tap detection */
    uint64_t       last_down_time;     /* ms timestamp of last DOWN */
    uint64_t       last_up_time;       /* ms timestamp of last UP */
    uint16_t       down_x;             /* X at DOWN */
    uint16_t       down_y;             /* Y at DOWN */
    bool           tap_pending;        /* Waiting to distinguish tap vs double-tap */
    uint8_t        tap_count;          /* 1 for potential tap, 2 for double-tap */

    /* Pinch detection */
    bool           two_finger_active;
    uint32_t       initial_distance;   /* Distance between two fingers at start */
    uint16_t       pinch_center_x;
    uint16_t       pinch_center_y;
} gesture_state_t;

/* ── Gesture timing constants (milliseconds) ── */

#define GESTURE_TAP_MAX_DURATION     200   /* Max ms for a tap (down to up) */
#define GESTURE_DOUBLE_TAP_GAP       300   /* Max ms between two taps */
#define GESTURE_LONG_PRESS_MIN       500   /* Min ms for long press */
#define GESTURE_SWIPE_MIN_DISTANCE   50    /* Min pixels for swipe */
#define GESTURE_TAP_MAX_DISTANCE     20    /* Max pixel drift for tap */
#define GESTURE_PINCH_MIN_DELTA      30    /* Min pixel distance change for pinch */

/* ── Touchscreen Device State ── */

typedef struct {
    touch_transport_t   transport;

    /* I2C transport state */
    volatile void      *i2c_base;           /* I2C controller MMIO base */
    uint8_t             i2c_addr;           /* 7-bit I2C slave address */
    i2c_hid_desc_t      i2c_hid_desc;      /* HID descriptor from device */

    /* USB transport state */
    xhci_controller_t  *usb_hc;            /* xHCI controller (if USB) */
    uint8_t             usb_slot_id;        /* USB slot */
    uint8_t             usb_ep_num;         /* Interrupt endpoint */
    uint16_t            usb_ep_max_packet;  /* Endpoint max packet size */

    /* Screen dimensions (for coordinate normalization) */
    uint16_t            screen_width;
    uint16_t            screen_height;

    /* Parsed report descriptor */
    touch_report_desc_t report_desc;

    /* Live touch state */
    touch_point_t       points[TOUCH_MAX_POINTS];
    uint8_t             active_count;       /* Number of active contacts */

    /* Event ring buffer */
    touch_event_t       event_queue[TOUCH_EVENT_QUEUE_SIZE];
    uint8_t             eq_head;            /* Write index (0-255) */
    uint8_t             eq_tail;            /* Read index (0-255) */

    /* Gesture recognition state */
    gesture_state_t     gesture;

    /* Gesture output queue (small, gestures are infrequent) */
    touch_gesture_t     gesture_queue[16];
    uint8_t             gq_head;
    uint8_t             gq_tail;

    /* Report read buffer */
    uint8_t             report_buf[256];

    bool                initialized;
} touch_dev_t;

/* ── Public API ── */

/* Initialize touchscreen over I2C transport.
 * i2c_base: MMIO base of I2C controller.
 * i2c_addr: 7-bit I2C slave address of touch controller.
 * screen_w, screen_h: display resolution for coordinate normalization. */
hal_status_t touch_init_i2c(touch_dev_t *dev, volatile void *i2c_base,
                             uint8_t i2c_addr,
                             uint16_t screen_w, uint16_t screen_h);

/* Initialize touchscreen over USB transport.
 * hc: initialized xHCI controller.
 * slot_id: USB device slot from xhci_port_reset + xhci_address_device. */
hal_status_t touch_init_usb(touch_dev_t *dev, xhci_controller_t *hc,
                             uint8_t slot_id,
                             uint16_t screen_w, uint16_t screen_h);

/* Poll for new touch data. Call periodically from main loop.
 * Reads reports from device, parses contacts, enqueues events. */
hal_status_t touch_poll(touch_dev_t *dev);

/* Get next touch event (returns false if queue empty). */
bool touch_get_event(touch_dev_t *dev, touch_event_t *event);

/* Get next gesture event (returns false if queue empty). */
bool touch_get_gesture(touch_dev_t *dev, touch_gesture_t *gesture);

/* Get current state of all active touch points.
 * Returns number of active contacts (0 if none). */
uint8_t touch_get_points(touch_dev_t *dev, touch_point_t *out, uint8_t max);

/* Update screen resolution (e.g., after rotation). */
void touch_set_resolution(touch_dev_t *dev, uint16_t w, uint16_t h);

/* Scan I2C bus for HID touch devices.
 * Probes common I2C addresses used by touch controllers.
 * Returns the 7-bit address if found, 0 if not found. */
uint8_t touch_i2c_scan(volatile void *i2c_base);

/* Register touch driver with the driver_loader system. */
void touch_register(void);

#endif /* ALJEFRA_DRV_TOUCH_H */
