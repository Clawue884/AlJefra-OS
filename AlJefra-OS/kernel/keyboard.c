/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Unified Keyboard Input Implementation
 *
 * Merges PS/2 keyboard (x86-64) and USB HID keyboard into a single
 * ring-buffered input stream.  Architecture-safe: non-x86 platforms
 * silently skip PS/2 and rely on USB HID or device-tree input.
 */

#include "keyboard.h"
#include "driver_loader.h"
#include "../drivers/input/ps2.h"
#include "../drivers/input/usb_hid.h"
#include "../lib/string.h"

/* ── Scancode Set 1 (IBM PC/AT) to ASCII — used for x86 port 0x60 raw reads
 *    when the PS/2 driver's scancode set 2 path is not available.
 *    Index = scancode (make code), value = unshifted ASCII.
 * ──────────────────────────────────────────────────────────────────── */
#if defined(__x86_64__) || defined(_M_X64)

static const char sc1_normal[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=',
    [0x0E] = '\b', /* Backspace */
    [0x0F] = '\t', /* Tab */
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
    [0x1C] = '\n', /* Enter */
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'',
    [0x29] = '`',
    [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
    [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' ', /* Space */
    [0x01] = 0x1B, /* Escape */
};

static const char sc1_shifted[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+',
    [0x0E] = '\b',
    [0x0F] = '\t',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
    [0x1C] = '\n',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F',
    [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
    [0x26] = 'L', [0x27] = ':', [0x28] = '"',
    [0x29] = '~',
    [0x2B] = '|',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V',
    [0x30] = 'B', [0x31] = 'N', [0x32] = 'M',
    [0x33] = '<', [0x34] = '>', [0x35] = '?',
    [0x39] = ' ',
    [0x01] = 0x1B,
};

/* Scancode set 1 modifier scancodes */
#define SC1_LSHIFT_MAKE     0x2A
#define SC1_RSHIFT_MAKE     0x36
#define SC1_LCTRL_MAKE      0x1D
#define SC1_LALT_MAKE       0x38
#define SC1_CAPSLOCK_MAKE   0x3A
#define SC1_RELEASE_BIT     0x80

#endif /* x86_64 scancode tables */

/* ── Internal state ── */

/* Modifier tracking (used by raw port 0x60 on x86 and kb_modifiers() on all) */
static bool g_shift;
static bool g_ctrl;
static bool g_alt;
static bool g_capslock;

static keyboard_event_t g_buf[KB_BUF_SIZE];
static volatile uint32_t g_head;   /* Next write position */
static volatile uint32_t g_tail;   /* Next read position */

/* PS/2 driver instance (used on x86-64) */
static ps2_dev_t g_ps2;
static bool g_ps2_active;

/* USB HID keyboard instance (if runtime-loaded USB HID driver present) */
static usb_hid_dev_t *g_usb_hid;
static bool g_usb_hid_active;

static bool g_initialized;

/* ── Ring buffer helpers ── */

static void kb_push(const keyboard_event_t *evt)
{
    uint32_t next = (g_head + 1) & KB_BUF_MASK;
    if (next == g_tail)
        return; /* Buffer full -- drop oldest is acceptable; here we drop newest */
    g_buf[g_head] = *evt;
    g_head = next;
}

static bool kb_pop(keyboard_event_t *evt)
{
    if (g_head == g_tail)
        return false;
    *evt = g_buf[g_tail];
    g_tail = (g_tail + 1) & KB_BUF_MASK;
    return true;
}

/* ── Build modifier bitmask from current state ── */

static uint8_t kb_modifiers(void)
{
    uint8_t m = 0;
    if (g_shift)    m |= KB_MOD_SHIFT;
    if (g_ctrl)     m |= KB_MOD_CTRL;
    if (g_alt)      m |= KB_MOD_ALT;
    if (g_capslock) m |= KB_MOD_CAPSLOCK;
    return m;
}

/* ── Raw port 0x60 polling (scancode set 1, x86-64 only) ──
 *    Used as a fallback when the full PS/2 driver init fails
 *    (e.g. controller self-test timeout on some BIOS/UEFI). */

#if defined(__x86_64__) || defined(_M_X64)
static void poll_raw_port60(void)
{
    /* Check status register — bit 0 = output buffer full */
    while (hal_port_in8(0x64) & 0x01) {
        uint8_t sc = hal_port_in8(0x60);

        bool release = (sc & SC1_RELEASE_BIT) != 0;
        uint8_t code = sc & 0x7F;

        /* Update modifier state */
        if (code == SC1_LSHIFT_MAKE || code == SC1_RSHIFT_MAKE) {
            g_shift = !release;
            return;
        }
        if (code == SC1_LCTRL_MAKE) {
            g_ctrl = !release;
            return;
        }
        if (code == SC1_LALT_MAKE) {
            g_alt = !release;
            return;
        }
        if (code == SC1_CAPSLOCK_MAKE && !release) {
            g_capslock = !g_capslock;
            return;
        }

        /* Build event */
        keyboard_event_t evt;
        evt.scancode = code;
        evt.pressed = !release;
        evt.modifiers = kb_modifiers();
        evt.ascii = 0;

        if (code < 128) {
            bool use_shift = g_shift;
            /* Caps lock inverts for letters */
            if (g_capslock) {
                char normal = sc1_normal[code];
                if (normal >= 'a' && normal <= 'z')
                    use_shift = !use_shift;
            }
            evt.ascii = use_shift ? sc1_shifted[code] : sc1_normal[code];

            /* Ctrl + letter = control character (1..26) */
            if (g_ctrl && evt.ascii >= 'a' && evt.ascii <= 'z')
                evt.ascii = (char)(evt.ascii - 'a' + 1);
        }

        kb_push(&evt);
    }
}
#endif /* x86_64 */

/* ── Poll PS/2 driver (scancode set 2, x86-64 only) ── */

static void poll_ps2(void)
{
    if (!g_ps2_active)
        return;

    ps2_poll(&g_ps2);

    ps2_key_event_t pe;
    while (ps2_get_key(&g_ps2, &pe)) {
        keyboard_event_t evt;
        evt.scancode = pe.scancode;
        evt.ascii = pe.ascii;
        evt.pressed = pe.pressed;
        evt.modifiers = 0;
        if (g_ps2.shift_held) evt.modifiers |= KB_MOD_SHIFT;
        if (g_ps2.ctrl_held)  evt.modifiers |= KB_MOD_CTRL;
        if (g_ps2.alt_held)   evt.modifiers |= KB_MOD_ALT;
        if (g_ps2.caps_lock)  evt.modifiers |= KB_MOD_CAPSLOCK;
        kb_push(&evt);
    }
}

/* ── Poll USB HID keyboard ── */

static void poll_usb_hid(void)
{
    if (!g_usb_hid_active || !g_usb_hid)
        return;

    usb_hid_poll(g_usb_hid);

    hid_key_event_t he;
    while (usb_hid_get_key(g_usb_hid, &he)) {
        keyboard_event_t evt;
        evt.scancode = he.keycode;
        evt.ascii = he.ascii;
        evt.pressed = he.pressed;
        evt.modifiers = 0;
        if (he.modifiers & (HID_MOD_LEFT_SHIFT | HID_MOD_RIGHT_SHIFT))
            evt.modifiers |= KB_MOD_SHIFT;
        if (he.modifiers & (HID_MOD_LEFT_CTRL | HID_MOD_RIGHT_CTRL))
            evt.modifiers |= KB_MOD_CTRL;
        if (he.modifiers & (HID_MOD_LEFT_ALT | HID_MOD_RIGHT_ALT))
            evt.modifiers |= KB_MOD_ALT;
        kb_push(&evt);
    }
}

/* ── Poll via driver_loader input_poll() — generic fallback ── */

static void poll_driver_loader(void)
{
    const driver_ops_t *inp = driver_get_input();
    if (!inp || !inp->input_poll)
        return;

    /* input_poll returns keycode or -1 */
    int kc;
    while ((kc = inp->input_poll()) >= 0) {
        keyboard_event_t evt;
        memset(&evt, 0, sizeof(evt));
        evt.scancode = (uint8_t)(kc & 0xFF);
        evt.pressed = true;
        /* Simple ASCII pass-through for runtime-loaded input drivers */
        evt.ascii = (kc < 128) ? (char)kc : 0;
        evt.modifiers = 0;
        kb_push(&evt);
    }
}

/* ── Public API ── */

hal_status_t keyboard_init(void)
{
    g_head = 0;
    g_tail = 0;
    g_shift = false;
    g_ctrl = false;
    g_alt = false;
    g_capslock = false;
    g_ps2_active = false;
    g_usb_hid_active = false;
    g_usb_hid = NULL;
    g_initialized = false;

    bool any_source = false;

    /* ── PS/2 keyboard (x86-64 only) ── */
#if defined(__x86_64__) || defined(_M_X64)
    {
        memset(&g_ps2, 0, sizeof(g_ps2));
        hal_status_t rc = ps2_init(&g_ps2);
        if (rc == HAL_OK) {
            g_ps2_active = true;
            any_source = true;
            hal_console_puts("[keyboard] PS/2 keyboard initialized\n");
        } else {
            /* Fallback: we can still do raw port 0x60 polling
             * (many BIOSes leave the controller in scancode set 1). */
            hal_console_puts("[keyboard] PS/2 init failed, using raw port 0x60\n");
            any_source = true; /* raw polling always works on x86 */
        }
    }
#endif

    /* ── Check for USB HID keyboard via driver_loader ── */
    {
        const driver_ops_t *inp = driver_get_input();
        if (inp) {
            any_source = true;
            hal_console_puts("[keyboard] Input driver found via driver_loader\n");
        }
    }

    g_initialized = true;

    if (!any_source) {
        hal_console_puts("[keyboard] WARNING: no keyboard source detected\n");
        return HAL_NO_DEVICE;
    }

    return HAL_OK;
}

void keyboard_poll(void)
{
    if (!g_initialized)
        return;

    /* Poll all active backends */
    if (g_ps2_active) {
        poll_ps2();
    }
#if defined(__x86_64__) || defined(_M_X64)
    else {
        /* Fallback raw port 0x60 polling */
        poll_raw_port60();
    }
#endif

    if (g_usb_hid_active) {
        poll_usb_hid();
    }

    /* Always check the generic driver_loader path */
    poll_driver_loader();
}

int keyboard_available(void)
{
    return (g_head != g_tail) ? 1 : 0;
}

hal_status_t keyboard_get_event(keyboard_event_t *event)
{
    if (!event)
        return HAL_ERROR;

    /* Try polling first to catch fresh data */
    keyboard_poll();

    if (kb_pop(event))
        return HAL_OK;
    return HAL_ERROR;
}

char keyboard_getchar(void)
{
    keyboard_event_t evt;

    for (;;) {
        keyboard_poll();

        if (kb_pop(&evt)) {
            /* Only return on key-down events with printable ASCII */
            if (evt.pressed && evt.ascii != 0)
                return evt.ascii;
        }

        /* Yield CPU briefly to avoid burning 100% in a tight loop.
         * 1 ms delay gives ~1000 Hz effective poll rate — more than
         * enough for interactive typing. */
        hal_timer_delay_us(1000);
    }
}

uint32_t keyboard_buffered(void)
{
    uint32_t h = g_head;
    uint32_t t = g_tail;
    return (h - t) & KB_BUF_MASK;
}
