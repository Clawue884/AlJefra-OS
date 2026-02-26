/* SPDX-License-Identifier: MIT */
/* AlJefra OS — x86-64 Console HAL Implementation
 * Serial COM1 (0x3F8) output with minimal printf.
 * Falls back to kernel b_output() for non-serial output.
 */

#include "../../hal/hal.h"
#include <stdarg.h>

/* Stub fallbacks (not needed when serial is available) */
static void b_output(const char *str, uint64_t nbr) { (void)str; (void)nbr; }
static uint8_t b_input(void) { return 0; }

/* -------------------------------------------------------------------------- */
/* Serial port (COM1) constants                                               */
/* -------------------------------------------------------------------------- */

#define COM1_BASE       0x3F8
#define COM1_DATA       (COM1_BASE + 0)  /* Data register (R/W) */
#define COM1_IER        (COM1_BASE + 1)  /* Interrupt Enable */
#define COM1_FCR        (COM1_BASE + 2)  /* FIFO Control (write) */
#define COM1_LCR        (COM1_BASE + 3)  /* Line Control */
#define COM1_MCR        (COM1_BASE + 4)  /* Modem Control */
#define COM1_LSR        (COM1_BASE + 5)  /* Line Status */
#define COM1_MSR        (COM1_BASE + 6)  /* Modem Status */
#define COM1_DLL        (COM1_BASE + 0)  /* Divisor Latch Low (DLAB=1) */
#define COM1_DLH        (COM1_BASE + 1)  /* Divisor Latch High (DLAB=1) */

/* LSR bits */
#define LSR_DATA_READY  (1u << 0)
#define LSR_TX_EMPTY    (1u << 5)

/* -------------------------------------------------------------------------- */
/* Port I/O helpers (inlined, not dependent on io.c)                          */
/* -------------------------------------------------------------------------- */

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* -------------------------------------------------------------------------- */
/* Console state                                                              */
/* -------------------------------------------------------------------------- */

static hal_console_type_t current_type = HAL_CONSOLE_SERIAL;
static bool console_initialized = false;
static bool serial_available = false;

/* -------------------------------------------------------------------------- */
/* Serial I/O                                                                 */
/* -------------------------------------------------------------------------- */

static void serial_init(void)
{
    /* Disable interrupts */
    outb(COM1_IER, 0x00);

    /* Set DLAB to access divisor */
    outb(COM1_LCR, 0x80);

    /* Set divisor to 1 (115200 baud) */
    outb(COM1_DLL, 0x01);
    outb(COM1_DLH, 0x00);

    /* 8 data bits, no parity, 1 stop bit (8N1), clear DLAB */
    outb(COM1_LCR, 0x03);

    /* Enable FIFO, clear TX/RX, 14-byte threshold */
    outb(COM1_FCR, 0xC7);

    /* DTR + RTS + OUT2 (required for interrupts) */
    outb(COM1_MCR, 0x0B);

    /* Test: set loopback mode and send 0xAE */
    outb(COM1_MCR, 0x1E);
    outb(COM1_DATA, 0xAE);

    /* Check if we get the byte back */
    if (inb(COM1_DATA) == 0xAE) {
        serial_available = true;
    }

    /* Restore normal operation: DTR + RTS + OUT2 */
    outb(COM1_MCR, 0x0B);
}

static void serial_putc(char c)
{
    /* Wait for TX buffer to be empty */
    while (!(inb(COM1_LSR) & LSR_TX_EMPTY)) {
        __asm__ volatile ("pause");
    }
    outb(COM1_DATA, (uint8_t)c);
}

static int serial_has_data(void)
{
    return (inb(COM1_LSR) & LSR_DATA_READY) != 0;
}

static char serial_getc(void)
{
    return (char)inb(COM1_DATA);
}

/* -------------------------------------------------------------------------- */
/* HAL Console API                                                            */
/* -------------------------------------------------------------------------- */

hal_status_t hal_console_init(void)
{
    serial_init();

    if (serial_available) {
        current_type = HAL_CONSOLE_SERIAL;
    } else {
        /* Fall back to kernel b_output (could be VGA or framebuffer) */
        current_type = HAL_CONSOLE_VGA;
    }

    console_initialized = true;
    return HAL_OK;
}

void hal_console_putc(char c)
{
    if (serial_available) {
        if (c == '\n') {
            serial_putc('\r');
        }
        serial_putc(c);
    } else {
        /* Use kernel b_output for single character */
        char buf[2] = { c, 0 };
        b_output(buf, 1);
    }
}

void hal_console_puts(const char *s)
{
    if (!s) return;

    if (serial_available) {
        while (*s) {
            if (*s == '\n')
                serial_putc('\r');
            serial_putc(*s);
            s++;
        }
    } else {
        /* Count string length */
        uint64_t len = 0;
        const char *p = s;
        while (*p++) len++;
        b_output(s, len);
    }
}

void hal_console_write(const char *s, uint64_t len)
{
    if (!s || len == 0) return;

    for (uint64_t i = 0; i < len; i++) {
        hal_console_putc(s[i]);
    }
}

/* -------------------------------------------------------------------------- */
/* Minimal printf implementation                                              */
/* Supports: %s, %d, %u, %x, %p, %l (as prefix for long), %%                */
/* -------------------------------------------------------------------------- */

/* Output a decimal number */
static void print_decimal(int64_t val)
{
    if (val < 0) {
        hal_console_putc('-');
        val = -val;
    }

    char buf[21]; /* max 20 digits for int64 + null */
    int pos = 0;

    if (val == 0) {
        hal_console_putc('0');
        return;
    }

    while (val > 0) {
        buf[pos++] = '0' + (char)(val % 10);
        val /= 10;
    }

    /* Reverse and output */
    for (int i = pos - 1; i >= 0; i--) {
        hal_console_putc(buf[i]);
    }
}

/* Output an unsigned decimal number */
static void print_unsigned(uint64_t val)
{
    char buf[21];
    int pos = 0;

    if (val == 0) {
        hal_console_putc('0');
        return;
    }

    while (val > 0) {
        buf[pos++] = '0' + (char)(val % 10);
        val /= 10;
    }

    for (int i = pos - 1; i >= 0; i--) {
        hal_console_putc(buf[i]);
    }
}

/* Output a hex number */
static void print_hex(uint64_t val, int width)
{
    static const char hex_digits[] = "0123456789abcdef";

    if (width == 0) {
        /* Auto-width: skip leading zeros */
        if (val == 0) {
            hal_console_putc('0');
            return;
        }
        char buf[17];
        int pos = 0;
        while (val > 0) {
            buf[pos++] = hex_digits[val & 0xF];
            val >>= 4;
        }
        for (int i = pos - 1; i >= 0; i--)
            hal_console_putc(buf[i]);
    } else {
        /* Fixed width, with leading zeros */
        for (int i = (width - 1) * 4; i >= 0; i -= 4) {
            hal_console_putc(hex_digits[(val >> i) & 0xF]);
        }
    }
}

void hal_console_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            hal_console_putc(*fmt);
            fmt++;
            continue;
        }

        fmt++; /* Skip '%' */

        /* Parse flags */
        int zero_pad = 0;
        if (*fmt == '0') {
            zero_pad = 1;
            fmt++;
        }

        /* Parse width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* Check for 'l' prefix (long) */
        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
            if (*fmt == 'l') {
                fmt++; /* Skip second 'l' in %lld, %llu, etc. */
            }
        }

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(args, const char *);
            hal_console_puts(s ? s : "(null)");
            break;
        }
        case 'd': {
            int64_t val;
            if (is_long) {
                val = va_arg(args, int64_t);
            } else {
                val = va_arg(args, int);
            }
            print_decimal(val);
            break;
        }
        case 'u': {
            uint64_t val;
            if (is_long) {
                val = va_arg(args, uint64_t);
            } else {
                val = va_arg(args, unsigned int);
            }
            print_unsigned(val);
            break;
        }
        case 'x': {
            uint64_t val;
            if (is_long) {
                val = va_arg(args, uint64_t);
            } else {
                val = va_arg(args, unsigned int);
            }
            print_hex(val, (zero_pad && width > 0) ? width : 0);
            break;
        }
        case 'p': {
            void *ptr = va_arg(args, void *);
            hal_console_puts("0x");
            print_hex((uint64_t)(uintptr_t)ptr, 16);
            break;
        }
        case '%':
            hal_console_putc('%');
            break;
        case 'c': {
            char c = (char)va_arg(args, int);
            hal_console_putc(c);
            break;
        }
        case '\0':
            /* Premature end of format string */
            goto done;
        default:
            /* Unknown format specifier, output literally */
            hal_console_putc('%');
            hal_console_putc(*fmt);
            break;
        }

        fmt++;
    }

done:
    va_end(args);
}

char hal_console_getc(void)
{
    if (serial_available && serial_has_data()) {
        return serial_getc();
    }
    /* Fall back to kernel input (blocking) */
    return (char)b_input();
}

int hal_console_has_input(void)
{
    if (serial_available) {
        return serial_has_data();
    }
    /* AlJefra's b_input is blocking; no non-blocking check available.
     * Return 0 to indicate "we don't know". */
    return 0;
}

hal_console_type_t hal_console_type(void)
{
    return current_type;
}

void hal_console_set_uart_base(uint64_t mmio_base)
{
    /* On x86-64, we use port I/O for COM1, not MMIO.
     * This function is primarily for ARM/RISC-V platforms where
     * UART is memory-mapped.  We acknowledge the call but don't change
     * behavior. */
    (void)mmio_base;
}
