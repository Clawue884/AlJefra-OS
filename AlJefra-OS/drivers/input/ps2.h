/* SPDX-License-Identifier: MIT */
/* AlJefra OS — PS/2 Keyboard/Mouse Driver
 * Legacy PS/2 controller driver using port I/O (x86) or MMIO.
 * On non-x86, hal_port_in/out are no-ops, so this driver does nothing.
 */

#ifndef ALJEFRA_DRV_PS2_H
#define ALJEFRA_DRV_PS2_H

#include "../../hal/hal.h"

/* ── PS/2 Controller I/O ports (x86) ── */
#define PS2_DATA_PORT       0x60    /* Data register (R/W) */
#define PS2_STATUS_PORT     0x64    /* Status register (R) / Command register (W) */

/* ── Status Register bits ── */
#define PS2_STATUS_OUTPUT   (1u << 0)   /* Output buffer full (data ready) */
#define PS2_STATUS_INPUT    (1u << 1)   /* Input buffer full (don't write) */
#define PS2_STATUS_SYSTEM   (1u << 2)   /* System flag */
#define PS2_STATUS_CMD      (1u << 3)   /* 0 = data written to 0x60, 1 = cmd to 0x64 */
#define PS2_STATUS_TIMEOUT  (1u << 6)   /* Timeout error */
#define PS2_STATUS_PARITY   (1u << 7)   /* Parity error */

/* ── PS/2 Controller Commands (write to 0x64) ── */
#define PS2_CMD_READ_CONFIG   0x20    /* Read configuration byte */
#define PS2_CMD_WRITE_CONFIG  0x60    /* Write configuration byte */
#define PS2_CMD_DISABLE_PORT2 0xA7    /* Disable second port */
#define PS2_CMD_ENABLE_PORT2  0xA8    /* Enable second port */
#define PS2_CMD_TEST_PORT2    0xA9    /* Test second port */
#define PS2_CMD_SELF_TEST     0xAA    /* Controller self-test (expect 0x55) */
#define PS2_CMD_TEST_PORT1    0xAB    /* Test first port */
#define PS2_CMD_DISABLE_PORT1 0xAD    /* Disable first port */
#define PS2_CMD_ENABLE_PORT1  0xAE    /* Enable first port */
#define PS2_CMD_WRITE_PORT2   0xD4    /* Write to second port (mouse) */

/* ── Configuration byte bits ── */
#define PS2_CFG_INT1        (1u << 0)   /* Port 1 interrupt enable */
#define PS2_CFG_INT2        (1u << 1)   /* Port 2 interrupt enable */
#define PS2_CFG_CLK1        (1u << 4)   /* Port 1 clock disable */
#define PS2_CFG_CLK2        (1u << 5)   /* Port 2 clock disable */
#define PS2_CFG_XLAT        (1u << 6)   /* Scancode translation */

/* ── Keyboard Commands (write to 0x60) ── */
#define PS2_KB_CMD_SET_LEDS   0xED
#define PS2_KB_CMD_ECHO       0xEE
#define PS2_KB_CMD_SET_SCAN   0xF0    /* Set scancode set */
#define PS2_KB_CMD_ENABLE     0xF4    /* Enable scanning */
#define PS2_KB_CMD_DISABLE    0xF5    /* Disable scanning */
#define PS2_KB_CMD_RESET      0xFF

/* ── Keyboard Responses ── */
#define PS2_KB_ACK            0xFA
#define PS2_KB_RESEND         0xFE
#define PS2_KB_SELF_TEST_OK   0xAA

/* ── Scancode Set 2 special bytes ── */
#define PS2_SC2_EXTENDED      0xE0    /* Extended key prefix */
#define PS2_SC2_RELEASE       0xF0    /* Key release prefix */

/* ── PS/2 IRQ lines ── */
#define PS2_IRQ_KEYBOARD      1       /* IRQ 1 (x86) */
#define PS2_IRQ_MOUSE         12      /* IRQ 12 (x86) */

/* ── Key event ── */
typedef struct {
    uint8_t scancode;       /* Raw scancode (set 2) */
    char    ascii;          /* ASCII (0 if non-printable) */
    bool    pressed;        /* true = down, false = up */
    bool    extended;       /* Extended key (E0 prefix) */
} ps2_key_event_t;

/* ── Key buffer ── */
#define PS2_KEY_BUF_SIZE    64

/* ── PS/2 Driver State ── */
typedef struct {
    /* Keyboard state */
    bool    got_release;        /* Next code is a release */
    bool    got_extended;       /* Next code is extended */
    bool    shift_held;
    bool    ctrl_held;
    bool    alt_held;
    bool    caps_lock;

    /* Key event ring buffer */
    ps2_key_event_t key_buf[PS2_KEY_BUF_SIZE];
    uint8_t         key_head;
    uint8_t         key_tail;

    bool    port1_ok;           /* First port passed self-test */
    bool    port2_ok;           /* Second port passed self-test */
    bool    initialized;
} ps2_dev_t;

/* ── Public API ── */

/* Initialize the PS/2 controller and keyboard.
 * Only functional on x86 (no-op on other architectures). */
hal_status_t ps2_init(ps2_dev_t *dev);

/* Process one byte of scancode data. Called from interrupt handler
 * or polled from ps2_poll(). */
void ps2_process_byte(ps2_dev_t *dev, uint8_t byte);

/* Poll for keyboard data (non-blocking).
 * Reads any available bytes from the data port. */
void ps2_poll(ps2_dev_t *dev);

/* Get next key event from buffer (returns false if empty) */
bool ps2_get_key(ps2_dev_t *dev, ps2_key_event_t *event);

/* Check if data is available */
bool ps2_has_key(ps2_dev_t *dev);

/* IRQ handler (register with hal_irq_register) */
void ps2_irq_handler(uint32_t irq, void *ctx);

#endif /* ALJEFRA_DRV_PS2_H */
