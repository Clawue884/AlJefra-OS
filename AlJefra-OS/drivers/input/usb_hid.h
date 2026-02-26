/* SPDX-License-Identifier: MIT */
/* AlJefra OS — USB HID (Keyboard/Mouse) Driver
 * Uses xHCI driver for USB transport.
 * Architecture-independent; no inline assembly.
 */

#ifndef ALJEFRA_DRV_USB_HID_H
#define ALJEFRA_DRV_USB_HID_H

#include "../../hal/hal.h"
#include "xhci.h"

/* ── HID Class Codes ── */
#define USB_CLASS_HID         0x03
#define USB_SUBCLASS_BOOT     0x01
#define USB_PROTOCOL_KEYBOARD 0x01
#define USB_PROTOCOL_MOUSE    0x02

/* ── HID Request Types ── */
#define HID_REQ_GET_REPORT    0x01
#define HID_REQ_SET_REPORT    0x09
#define HID_REQ_GET_IDLE      0x02
#define HID_REQ_SET_IDLE      0x0A
#define HID_REQ_GET_PROTOCOL  0x03
#define HID_REQ_SET_PROTOCOL  0x0B

/* ── Boot Protocol Keyboard Report (8 bytes) ── */
typedef struct __attribute__((packed)) {
    uint8_t modifiers;    /* Modifier keys (Ctrl, Shift, Alt, GUI) */
    uint8_t reserved;     /* Reserved (always 0) */
    uint8_t keys[6];      /* Up to 6 simultaneous key codes */
} hid_keyboard_report_t;

/* ── Modifier key bits ── */
#define HID_MOD_LEFT_CTRL   (1u << 0)
#define HID_MOD_LEFT_SHIFT  (1u << 1)
#define HID_MOD_LEFT_ALT    (1u << 2)
#define HID_MOD_LEFT_GUI    (1u << 3)
#define HID_MOD_RIGHT_CTRL  (1u << 4)
#define HID_MOD_RIGHT_SHIFT (1u << 5)
#define HID_MOD_RIGHT_ALT   (1u << 6)
#define HID_MOD_RIGHT_GUI   (1u << 7)

/* ── Boot Protocol Mouse Report (3-4 bytes) ── */
typedef struct __attribute__((packed)) {
    uint8_t  buttons;     /* Button state (bit 0 = left, 1 = right, 2 = middle) */
    int8_t   x;           /* Relative X movement */
    int8_t   y;           /* Relative Y movement */
    int8_t   wheel;       /* Scroll wheel (optional) */
} hid_mouse_report_t;

#define HID_MOUSE_BTN_LEFT   (1u << 0)
#define HID_MOUSE_BTN_RIGHT  (1u << 1)
#define HID_MOUSE_BTN_MIDDLE (1u << 2)

/* ── Key event ── */
typedef struct {
    uint8_t keycode;      /* USB HID keycode */
    char    ascii;        /* ASCII character (0 if non-printable) */
    bool    pressed;      /* true = key down, false = key up */
    uint8_t modifiers;    /* Active modifiers at time of event */
} hid_key_event_t;

/* ── Mouse event ── */
typedef struct {
    int16_t dx;           /* X delta */
    int16_t dy;           /* Y delta */
    int8_t  wheel;        /* Scroll wheel delta */
    uint8_t buttons;      /* Current button state */
} hid_mouse_event_t;

/* Maximum events in the ring buffer */
#define HID_KEY_BUF_SIZE    32
#define HID_MOUSE_BUF_SIZE  16

/* ── USB HID Device State ── */
typedef struct {
    xhci_controller_t *hc;             /* xHCI controller */
    uint8_t            slot_id;         /* USB device slot */
    uint8_t            protocol;        /* KEYBOARD or MOUSE */
    uint8_t            ep_num;          /* Interrupt endpoint number */
    uint16_t           ep_max_packet;   /* Interrupt EP max packet */
    uint8_t            ep_interval;     /* Polling interval */

    /* Keyboard state */
    uint8_t            prev_keys[6];    /* Previous key report for delta */
    uint8_t            prev_modifiers;

    /* Key event ring buffer */
    hid_key_event_t    key_buf[HID_KEY_BUF_SIZE];
    uint8_t            key_head;
    uint8_t            key_tail;

    /* Mouse event ring buffer */
    hid_mouse_event_t  mouse_buf[HID_MOUSE_BUF_SIZE];
    uint8_t            mouse_head;
    uint8_t            mouse_tail;

    bool               initialized;
} usb_hid_dev_t;

/* ── Public API ── */

/* Initialize a HID device (keyboard or mouse).
 * Scans the config descriptor to find the HID interface and interrupt endpoint.
 * hc must be initialized. slot_id from xhci_port_reset + xhci_address_device. */
hal_status_t usb_hid_init(usb_hid_dev_t *hid, xhci_controller_t *hc,
                           uint8_t slot_id);

/* Poll for new HID reports. Call periodically.
 * Processes interrupt transfer data and queues key/mouse events. */
hal_status_t usb_hid_poll(usb_hid_dev_t *hid);

/* Get next key event (returns false if no events pending) */
bool usb_hid_get_key(usb_hid_dev_t *hid, hid_key_event_t *event);

/* Get next mouse event (returns false if no events pending) */
bool usb_hid_get_mouse(usb_hid_dev_t *hid, hid_mouse_event_t *event);

/* Convert USB HID keycode + modifiers to ASCII character.
 * Returns 0 for non-printable keys. */
char usb_hid_keycode_to_ascii(uint8_t keycode, uint8_t modifiers);

/* Check if this device is a keyboard */
bool usb_hid_is_keyboard(usb_hid_dev_t *hid);

/* Check if this device is a mouse */
bool usb_hid_is_mouse(usb_hid_dev_t *hid);

#endif /* ALJEFRA_DRV_USB_HID_H */
