/* SPDX-License-Identifier: MIT */
/* AlJefra OS — PS/2 Keyboard/Mouse Driver Implementation
 * Legacy PS/2 controller driver. Uses hal_port_in/out (x86 only).
 */

#include "ps2.h"

/* ── Scancode Set 2 to ASCII mapping ── */

/* Normal (unshifted) mapping for scancode set 2 */
static const char sc2_normal[128] = {
    [0x1C] = 'a', [0x32] = 'b', [0x21] = 'c', [0x23] = 'd',
    [0x24] = 'e', [0x2B] = 'f', [0x34] = 'g', [0x33] = 'h',
    [0x43] = 'i', [0x3B] = 'j', [0x42] = 'k', [0x4B] = 'l',
    [0x3A] = 'm', [0x31] = 'n', [0x44] = 'o', [0x4D] = 'p',
    [0x15] = 'q', [0x2D] = 'r', [0x1B] = 's', [0x2C] = 't',
    [0x3C] = 'u', [0x2A] = 'v', [0x1D] = 'w', [0x22] = 'x',
    [0x35] = 'y', [0x1A] = 'z',
    [0x16] = '1', [0x1E] = '2', [0x26] = '3', [0x25] = '4',
    [0x2E] = '5', [0x36] = '6', [0x3D] = '7', [0x3E] = '8',
    [0x46] = '9', [0x45] = '0',
    [0x5A] = '\n', /* Enter */
    [0x76] = 0x1B, /* Escape */
    [0x66] = '\b', /* Backspace */
    [0x0D] = '\t', /* Tab */
    [0x29] = ' ',  /* Space */
    [0x4E] = '-', [0x55] = '=', [0x54] = '[', [0x5B] = ']',
    [0x5D] = '\\', [0x4C] = ';', [0x52] = '\'',
    [0x0E] = '`', [0x41] = ',', [0x49] = '.', [0x4A] = '/',
};

/* Shifted mapping */
static const char sc2_shifted[128] = {
    [0x1C] = 'A', [0x32] = 'B', [0x21] = 'C', [0x23] = 'D',
    [0x24] = 'E', [0x2B] = 'F', [0x34] = 'G', [0x33] = 'H',
    [0x43] = 'I', [0x3B] = 'J', [0x42] = 'K', [0x4B] = 'L',
    [0x3A] = 'M', [0x31] = 'N', [0x44] = 'O', [0x4D] = 'P',
    [0x15] = 'Q', [0x2D] = 'R', [0x1B] = 'S', [0x2C] = 'T',
    [0x3C] = 'U', [0x2A] = 'V', [0x1D] = 'W', [0x22] = 'X',
    [0x35] = 'Y', [0x1A] = 'Z',
    [0x16] = '!', [0x1E] = '@', [0x26] = '#', [0x25] = '$',
    [0x2E] = '%', [0x36] = '^', [0x3D] = '&', [0x3E] = '*',
    [0x46] = '(', [0x45] = ')',
    [0x5A] = '\n', [0x76] = 0x1B, [0x66] = '\b', [0x0D] = '\t',
    [0x29] = ' ',
    [0x4E] = '_', [0x55] = '+', [0x54] = '{', [0x5B] = '}',
    [0x5D] = '|', [0x4C] = ':', [0x52] = '"',
    [0x0E] = '~', [0x41] = '<', [0x49] = '>', [0x4A] = '?',
};

/* Scancode set 2 codes for modifier keys */
#define SC2_LSHIFT    0x12
#define SC2_RSHIFT    0x59
#define SC2_LCTRL     0x14    /* Also extended for RCTRL */
#define SC2_LALT      0x11    /* Also extended for RALT */
#define SC2_CAPSLOCK  0x58

/* ── Internal helpers ── */

static void ps2_push_key(ps2_dev_t *dev, ps2_key_event_t *evt)
{
    uint8_t next = (dev->key_head + 1) % PS2_KEY_BUF_SIZE;
    if (next == dev->key_tail)
        return;  /* Full */
    dev->key_buf[dev->key_head] = *evt;
    dev->key_head = next;
}

/* Wait until input buffer is empty (can write) */
static bool ps2_wait_input(void)
{
    for (int i = 0; i < 100000; i++) {
        if (!(hal_port_in8(PS2_STATUS_PORT) & PS2_STATUS_INPUT))
            return true;
    }
    return false;
}

/* Wait until output buffer is full (can read) */
static bool ps2_wait_output(void)
{
    for (int i = 0; i < 100000; i++) {
        if (hal_port_in8(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT)
            return true;
    }
    return false;
}

/* Send a command to the PS/2 controller */
static void ps2_ctrl_cmd(uint8_t cmd)
{
    ps2_wait_input();
    hal_port_out8(PS2_STATUS_PORT, cmd);
}

/* Send a byte to the keyboard (port 1) */
static void ps2_kb_send(uint8_t byte)
{
    ps2_wait_input();
    hal_port_out8(PS2_DATA_PORT, byte);
}

/* Read a byte from the data port (with timeout) */
static bool ps2_read_data(uint8_t *byte)
{
    if (ps2_wait_output()) {
        *byte = hal_port_in8(PS2_DATA_PORT);
        return true;
    }
    return false;
}

/* ── Public API ── */

hal_status_t ps2_init(ps2_dev_t *dev)
{
    /* On non-x86, port I/O is a no-op — this driver is inert */
    if (hal_arch() != HAL_ARCH_X86_64)
        return HAL_NOT_SUPPORTED;

    dev->initialized = false;
    dev->got_release = false;
    dev->got_extended = false;
    dev->shift_held = false;
    dev->ctrl_held = false;
    dev->alt_held = false;
    dev->caps_lock = false;
    dev->key_head = 0;
    dev->key_tail = 0;
    dev->port1_ok = false;
    dev->port2_ok = false;

    /* Disable both ports */
    ps2_ctrl_cmd(PS2_CMD_DISABLE_PORT1);
    ps2_ctrl_cmd(PS2_CMD_DISABLE_PORT2);

    /* Flush output buffer */
    while (hal_port_in8(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT)
        hal_port_in8(PS2_DATA_PORT);

    /* Read config, disable IRQs and translation */
    ps2_ctrl_cmd(PS2_CMD_READ_CONFIG);
    uint8_t config;
    if (!ps2_read_data(&config))
        return HAL_TIMEOUT;

    config &= ~(PS2_CFG_INT1 | PS2_CFG_INT2 | PS2_CFG_XLAT);
    ps2_ctrl_cmd(PS2_CMD_WRITE_CONFIG);
    ps2_kb_send(config);

    /* Controller self-test */
    ps2_ctrl_cmd(PS2_CMD_SELF_TEST);
    uint8_t result;
    if (!ps2_read_data(&result) || result != 0x55)
        return HAL_ERROR;

    /* Restore config (self-test may reset it) */
    ps2_ctrl_cmd(PS2_CMD_WRITE_CONFIG);
    ps2_kb_send(config);

    /* Test port 1 */
    ps2_ctrl_cmd(PS2_CMD_TEST_PORT1);
    if (ps2_read_data(&result) && result == 0x00)
        dev->port1_ok = true;

    if (!dev->port1_ok)
        return HAL_NO_DEVICE;

    /* Enable port 1 */
    ps2_ctrl_cmd(PS2_CMD_ENABLE_PORT1);

    /* Enable port 1 interrupt */
    ps2_ctrl_cmd(PS2_CMD_READ_CONFIG);
    if (ps2_read_data(&config)) {
        config |= PS2_CFG_INT1;
        ps2_ctrl_cmd(PS2_CMD_WRITE_CONFIG);
        ps2_kb_send(config);
    }

    /* Reset keyboard */
    ps2_kb_send(PS2_KB_CMD_RESET);
    if (ps2_read_data(&result)) {
        /* Wait for self-test pass (0xAA) or ACK (0xFA) */
        if (result == PS2_KB_ACK)
            ps2_read_data(&result); /* Read self-test result */
    }

    /* Set scancode set 2 */
    ps2_kb_send(PS2_KB_CMD_SET_SCAN);
    ps2_read_data(&result); /* ACK */
    ps2_kb_send(0x02);      /* Set 2 */
    ps2_read_data(&result); /* ACK */

    /* Enable scanning */
    ps2_kb_send(PS2_KB_CMD_ENABLE);
    ps2_read_data(&result);

    /* Register IRQ handler */
    hal_irq_register(PS2_IRQ_KEYBOARD, ps2_irq_handler, dev);
    hal_irq_enable(PS2_IRQ_KEYBOARD);

    dev->initialized = true;
    return HAL_OK;
}

void ps2_process_byte(ps2_dev_t *dev, uint8_t byte)
{
    if (!dev->initialized)
        return;

    /* Handle multi-byte sequences */
    if (byte == PS2_SC2_EXTENDED) {
        dev->got_extended = true;
        return;
    }
    if (byte == PS2_SC2_RELEASE) {
        dev->got_release = true;
        return;
    }

    bool pressed = !dev->got_release;
    bool extended = dev->got_extended;
    dev->got_release = false;
    dev->got_extended = false;

    /* Update modifier state */
    if (byte == SC2_LSHIFT || byte == SC2_RSHIFT) {
        dev->shift_held = pressed;
        return;
    }
    if (byte == SC2_LCTRL) {
        dev->ctrl_held = pressed;
        return;
    }
    if (byte == SC2_LALT) {
        dev->alt_held = pressed;
        return;
    }
    if (byte == SC2_CAPSLOCK && pressed) {
        dev->caps_lock = !dev->caps_lock;
        return;
    }

    /* Build key event */
    ps2_key_event_t evt;
    evt.scancode = byte;
    evt.pressed = pressed;
    evt.extended = extended;
    evt.ascii = 0;

    /* Map to ASCII */
    if (byte < 128) {
        bool use_shifted = dev->shift_held;
        /* Caps lock inverts shift for alphabetic keys */
        if (dev->caps_lock) {
            char normal = sc2_normal[byte];
            if (normal >= 'a' && normal <= 'z')
                use_shifted = !use_shifted;
        }
        evt.ascii = use_shifted ? sc2_shifted[byte] : sc2_normal[byte];

        /* Ctrl + letter = control character (1-26) */
        if (dev->ctrl_held && evt.ascii >= 'a' && evt.ascii <= 'z')
            evt.ascii = (char)(evt.ascii - 'a' + 1);
    }

    ps2_push_key(dev, &evt);
}

void ps2_poll(ps2_dev_t *dev)
{
    if (!dev->initialized)
        return;

    /* Read all available bytes */
    while (hal_port_in8(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT) {
        uint8_t byte = hal_port_in8(PS2_DATA_PORT);
        ps2_process_byte(dev, byte);
    }
}

bool ps2_get_key(ps2_dev_t *dev, ps2_key_event_t *event)
{
    if (dev->key_head == dev->key_tail)
        return false;
    *event = dev->key_buf[dev->key_tail];
    dev->key_tail = (dev->key_tail + 1) % PS2_KEY_BUF_SIZE;
    return true;
}

bool ps2_has_key(ps2_dev_t *dev)
{
    return dev->key_head != dev->key_tail;
}

void ps2_irq_handler(uint32_t irq, void *ctx)
{
    (void)irq;
    ps2_dev_t *dev = (ps2_dev_t *)ctx;
    if (!dev || !dev->initialized)
        return;

    uint8_t status = hal_port_in8(PS2_STATUS_PORT);
    if (status & PS2_STATUS_OUTPUT) {
        uint8_t byte = hal_port_in8(PS2_DATA_PORT);
        ps2_process_byte(dev, byte);
    }

    hal_irq_eoi(PS2_IRQ_KEYBOARD);
}
