/* SPDX-License-Identifier: MIT */
/* AlJefra OS — USB HID (Keyboard/Mouse) Driver Implementation
 * Architecture-independent; uses xHCI for USB transport.
 */

#include "usb_hid.h"
#include "../../lib/string.h"

/* ── USB HID keycode to ASCII mapping (US layout) ── */

/* Unshifted ASCII table indexed by USB HID keycode (0x00 - 0x73) */
static const char hid_keymap_normal[128] = {
    [0x00] = 0,    /* No event */
    [0x04] = 'a', [0x05] = 'b', [0x06] = 'c', [0x07] = 'd',
    [0x08] = 'e', [0x09] = 'f', [0x0A] = 'g', [0x0B] = 'h',
    [0x0C] = 'i', [0x0D] = 'j', [0x0E] = 'k', [0x0F] = 'l',
    [0x10] = 'm', [0x11] = 'n', [0x12] = 'o', [0x13] = 'p',
    [0x14] = 'q', [0x15] = 'r', [0x16] = 's', [0x17] = 't',
    [0x18] = 'u', [0x19] = 'v', [0x1A] = 'w', [0x1B] = 'x',
    [0x1C] = 'y', [0x1D] = 'z',
    [0x1E] = '1', [0x1F] = '2', [0x20] = '3', [0x21] = '4',
    [0x22] = '5', [0x23] = '6', [0x24] = '7', [0x25] = '8',
    [0x26] = '9', [0x27] = '0',
    [0x28] = '\n', /* Enter */
    [0x29] = 0x1B, /* Escape */
    [0x2A] = '\b', /* Backspace */
    [0x2B] = '\t', /* Tab */
    [0x2C] = ' ',  /* Space */
    [0x2D] = '-', [0x2E] = '=', [0x2F] = '[', [0x30] = ']',
    [0x31] = '\\', [0x32] = '#', [0x33] = ';', [0x34] = '\'',
    [0x35] = '`', [0x36] = ',', [0x37] = '.', [0x38] = '/',
    /* F1-F12: 0x3A-0x45 — non-printable */
    [0x4F] = 0, /* Right arrow */
    [0x50] = 0, /* Left arrow */
    [0x51] = 0, /* Down arrow */
    [0x52] = 0, /* Up arrow */
    /* Keypad */
    [0x54] = '/', [0x55] = '*', [0x56] = '-', [0x57] = '+',
    [0x58] = '\n', /* Keypad Enter */
    [0x59] = '1', [0x5A] = '2', [0x5B] = '3', [0x5C] = '4',
    [0x5D] = '5', [0x5E] = '6', [0x5F] = '7', [0x60] = '8',
    [0x61] = '9', [0x62] = '0', [0x63] = '.',
};

/* Shifted ASCII table */
static const char hid_keymap_shifted[128] = {
    [0x04] = 'A', [0x05] = 'B', [0x06] = 'C', [0x07] = 'D',
    [0x08] = 'E', [0x09] = 'F', [0x0A] = 'G', [0x0B] = 'H',
    [0x0C] = 'I', [0x0D] = 'J', [0x0E] = 'K', [0x0F] = 'L',
    [0x10] = 'M', [0x11] = 'N', [0x12] = 'O', [0x13] = 'P',
    [0x14] = 'Q', [0x15] = 'R', [0x16] = 'S', [0x17] = 'T',
    [0x18] = 'U', [0x19] = 'V', [0x1A] = 'W', [0x1B] = 'X',
    [0x1C] = 'Y', [0x1D] = 'Z',
    [0x1E] = '!', [0x1F] = '@', [0x20] = '#', [0x21] = '$',
    [0x22] = '%', [0x23] = '^', [0x24] = '&', [0x25] = '*',
    [0x26] = '(', [0x27] = ')',
    [0x28] = '\n', [0x29] = 0x1B, [0x2A] = '\b', [0x2B] = '\t',
    [0x2C] = ' ',
    [0x2D] = '_', [0x2E] = '+', [0x2F] = '{', [0x30] = '}',
    [0x31] = '|', [0x32] = '~', [0x33] = ':', [0x34] = '"',
    [0x35] = '~', [0x36] = '<', [0x37] = '>', [0x38] = '?',
};

/* ── Helpers ── */

/* Check if a keycode is in an array of 6 keys */
static bool hid_key_in_array(uint8_t key, const uint8_t *arr)
{
    for (int i = 0; i < 6; i++) {
        if (arr[i] == key)
            return true;
    }
    return false;
}

/* Push a key event into the ring buffer */
static void hid_push_key(usb_hid_dev_t *hid, hid_key_event_t *evt)
{
    uint8_t next = (hid->key_head + 1) % HID_KEY_BUF_SIZE;
    if (next == hid->key_tail)
        return;  /* Buffer full — drop event */
    hid->key_buf[hid->key_head] = *evt;
    hid->key_head = next;
}

/* Push a mouse event */
static void hid_push_mouse(usb_hid_dev_t *hid, hid_mouse_event_t *evt)
{
    uint8_t next = (hid->mouse_head + 1) % HID_MOUSE_BUF_SIZE;
    if (next == hid->mouse_tail)
        return;
    hid->mouse_buf[hid->mouse_head] = *evt;
    hid->mouse_head = next;
}

/* ── Process keyboard report ── */

static void hid_process_keyboard(usb_hid_dev_t *hid, const uint8_t *report,
                                  uint16_t len)
{
    if (len < 8)
        return;

    const hid_keyboard_report_t *rpt = (const hid_keyboard_report_t *)report;
    uint8_t mods = rpt->modifiers;

    /* Detect newly pressed keys (in current but not in previous) */
    for (int i = 0; i < 6; i++) {
        uint8_t key = rpt->keys[i];
        if (key == 0 || key == 1)  /* 0=no event, 1=rollover error */
            continue;
        if (!hid_key_in_array(key, hid->prev_keys)) {
            /* New key press */
            hid_key_event_t evt;
            evt.keycode = key;
            evt.pressed = true;
            evt.modifiers = mods;
            evt.ascii = usb_hid_keycode_to_ascii(key, mods);
            hid_push_key(hid, &evt);
        }
    }

    /* Detect released keys (in previous but not in current) */
    for (int i = 0; i < 6; i++) {
        uint8_t key = hid->prev_keys[i];
        if (key == 0 || key == 1)
            continue;
        if (!hid_key_in_array(key, rpt->keys)) {
            hid_key_event_t evt;
            evt.keycode = key;
            evt.pressed = false;
            evt.modifiers = mods;
            evt.ascii = usb_hid_keycode_to_ascii(key, mods);
            hid_push_key(hid, &evt);
        }
    }

    /* Save current state */
    for (int i = 0; i < 6; i++)
        hid->prev_keys[i] = rpt->keys[i];
    hid->prev_modifiers = mods;
}

/* ── Process mouse report ── */

static void hid_process_mouse(usb_hid_dev_t *hid, const uint8_t *report,
                                uint16_t len)
{
    if (len < 3)
        return;

    const hid_mouse_report_t *rpt = (const hid_mouse_report_t *)report;

    hid_mouse_event_t evt;
    evt.buttons = rpt->buttons;
    evt.dx = rpt->x;
    evt.dy = rpt->y;
    evt.wheel = (len >= 4) ? rpt->wheel : 0;
    hid_push_mouse(hid, &evt);
}

/* ── Public API ── */

hal_status_t usb_hid_init(usb_hid_dev_t *hid, xhci_controller_t *hc,
                           uint8_t slot_id)
{
    memset(hid, 0, sizeof(*hid));
    hid->hc = hc;
    hid->slot_id = slot_id;
    hid->initialized = false;

    /* Get device descriptor */
    usb_device_desc_t dev_desc;
    hal_status_t st = xhci_get_device_desc(hc, slot_id, &dev_desc);
    if (st != HAL_OK)
        return st;

    /* Get configuration descriptor (first 256 bytes should be enough) */
    uint8_t config_buf[256];
    memset(config_buf, 0, sizeof(config_buf));
    st = xhci_get_config_desc(hc, slot_id, config_buf, sizeof(config_buf));
    if (st != HAL_OK)
        return st;

    /* Parse config descriptor to find HID interface + interrupt endpoint */
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

        if (desc_type == USB_DESC_INTERFACE && desc_len >= 9) {
            usb_interface_desc_t *iface = (usb_interface_desc_t *)&config_buf[offset];
            if (iface->bInterfaceClass == USB_CLASS_HID &&
                iface->bInterfaceSubClass == USB_SUBCLASS_BOOT) {
                hid->protocol = iface->bInterfaceProtocol;
                found_hid = true;
            }
        }

        if (found_hid && desc_type == USB_DESC_ENDPOINT && desc_len >= 7) {
            usb_endpoint_desc_t *ep = (usb_endpoint_desc_t *)&config_buf[offset];
            /* Interrupt IN endpoint */
            if ((ep->bmAttributes & 0x03) == 0x03 &&  /* Interrupt */
                (ep->bEndpointAddress & 0x80)) {       /* IN direction */
                hid->ep_num = ep->bEndpointAddress & 0x0F;
                hid->ep_max_packet = ep->wMaxPacketSize;
                hid->ep_interval = ep->bInterval;
                break;
            }
        }

        offset += desc_len;
    }

    if (!found_hid || hid->ep_num == 0)
        return HAL_NO_DEVICE;

    /* Set Configuration 1 */
    st = xhci_set_config(hc, slot_id, cfg->bConfigurationValue);
    if (st != HAL_OK)
        return st;

    /* Set boot protocol (class request: SET_PROTOCOL, wValue=0 for boot) */
    st = xhci_control_transfer(hc, slot_id,
        0x21,                  /* Host-to-Device, Class, Interface */
        HID_REQ_SET_PROTOCOL,
        0,                     /* wValue: 0 = Boot Protocol */
        0,                     /* wIndex: interface 0 */
        0, NULL);
    /* Ignore failure — some devices don't support SET_PROTOCOL */

    /* Set idle (rate = 0, no repeated reports when nothing changes) */
    xhci_control_transfer(hc, slot_id, 0x21, HID_REQ_SET_IDLE, 0, 0, 0, NULL);

    /* Configure interrupt endpoint in xHCI */
    st = xhci_configure_interrupt_ep(hc, slot_id, hid->ep_num,
                                      hid->ep_max_packet, hid->ep_interval);
    if (st != HAL_OK)
        return st;

    hid->initialized = true;
    return HAL_OK;
}

hal_status_t usb_hid_poll(usb_hid_dev_t *hid)
{
    if (!hid->initialized)
        return HAL_ERROR;

    uint8_t report[64];
    uint16_t length = 0;

    hal_status_t st = xhci_poll_interrupt(hid->hc, hid->slot_id,
                                           report, &length);
    if (st != HAL_OK)
        return st;

    if (length == 0)
        return HAL_OK;

    if (hid->protocol == USB_PROTOCOL_KEYBOARD) {
        hid_process_keyboard(hid, report, length);
    } else if (hid->protocol == USB_PROTOCOL_MOUSE) {
        hid_process_mouse(hid, report, length);
    }

    return HAL_OK;
}

bool usb_hid_get_key(usb_hid_dev_t *hid, hid_key_event_t *event)
{
    if (hid->key_head == hid->key_tail)
        return false;
    *event = hid->key_buf[hid->key_tail];
    hid->key_tail = (hid->key_tail + 1) % HID_KEY_BUF_SIZE;
    return true;
}

bool usb_hid_get_mouse(usb_hid_dev_t *hid, hid_mouse_event_t *event)
{
    if (hid->mouse_head == hid->mouse_tail)
        return false;
    *event = hid->mouse_buf[hid->mouse_tail];
    hid->mouse_tail = (hid->mouse_tail + 1) % HID_MOUSE_BUF_SIZE;
    return true;
}

char usb_hid_keycode_to_ascii(uint8_t keycode, uint8_t modifiers)
{
    if (keycode >= 128)
        return 0;

    bool shift = (modifiers & (HID_MOD_LEFT_SHIFT | HID_MOD_RIGHT_SHIFT)) != 0;

    if (shift)
        return hid_keymap_shifted[keycode];
    else
        return hid_keymap_normal[keycode];
}

bool usb_hid_is_keyboard(usb_hid_dev_t *hid)
{
    return hid->initialized && hid->protocol == USB_PROTOCOL_KEYBOARD;
}

bool usb_hid_is_mouse(usb_hid_dev_t *hid)
{
    return hid->initialized && hid->protocol == USB_PROTOCOL_MOUSE;
}
