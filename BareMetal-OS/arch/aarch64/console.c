/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- AArch64 Console (PL011 UART) Implementation
 * Implements hal/console.h using the ARM PL011 UART.
 *
 * PL011 register map (at MMIO base):
 *   0x000 UARTDR   - Data Register (read/write)
 *   0x004 UARTRSR  - Receive Status / Error Clear
 *   0x018 UARTFR   - Flag Register (read-only)
 *   0x024 UARTIBRD - Integer Baud Rate Divisor
 *   0x028 UARTFBRD - Fractional Baud Rate Divisor
 *   0x02C UARTLCR_H - Line Control Register
 *   0x030 UARTCR   - Control Register
 *   0x034 UARTIFLS - Interrupt FIFO Level Select
 *   0x038 UARTIMSC - Interrupt Mask Set/Clear
 *   0x044 UARTICR  - Interrupt Clear Register
 *
 * UARTFR bits:
 *   Bit 3: BUSY (UART busy transmitting)
 *   Bit 4: RXFE (Receive FIFO empty)
 *   Bit 5: TXFF (Transmit FIFO full)
 *   Bit 7: TXFE (Transmit FIFO empty)
 *
 * Default base for QEMU virt: 0x09000000
 */

#include "../../hal/hal.h"
#include <stdarg.h>

/* ------------------------------------------------------------------ */
/* PL011 Register Offsets                                              */
/* ------------------------------------------------------------------ */

#define UARTDR      0x000
#define UARTRSR     0x004
#define UARTFR      0x018
#define UARTIBRD    0x024
#define UARTFBRD    0x028
#define UARTLCR_H   0x02C
#define UARTCR      0x030
#define UARTIFLS    0x034
#define UARTIMSC    0x038
#define UARTICR     0x044

/* Flag register bits */
#define UARTFR_TXFF  (1 << 5)   /* TX FIFO full */
#define UARTFR_RXFE  (1 << 4)   /* RX FIFO empty */
#define UARTFR_BUSY  (1 << 3)   /* UART busy */

/* Control register bits */
#define UARTCR_UARTEN (1 << 0)  /* UART enable */
#define UARTCR_TXE    (1 << 8)  /* TX enable */
#define UARTCR_RXE    (1 << 9)  /* RX enable */

/* Line control bits */
#define UARTLCR_H_WLEN_8  (3 << 5)  /* 8-bit word length */
#define UARTLCR_H_FEN     (1 << 4)  /* FIFO enable */

/* ------------------------------------------------------------------ */
/* Private state                                                       */
/* ------------------------------------------------------------------ */

static volatile uint8_t *uart_base = (volatile uint8_t *)0x09000000ULL;
static hal_console_type_t console_type_val = HAL_CONSOLE_SERIAL;

/* ------------------------------------------------------------------ */
/* UART MMIO helpers                                                   */
/* ------------------------------------------------------------------ */

static inline uint32_t uart_read(uint32_t off)
{
    uint32_t v = *(volatile uint32_t *)(uart_base + off);
    __asm__ volatile("dmb ish" ::: "memory");
    return v;
}

static inline void uart_write(uint32_t off, uint32_t val)
{
    __asm__ volatile("dmb ish" ::: "memory");
    *(volatile uint32_t *)(uart_base + off) = val;
}

/* ------------------------------------------------------------------ */
/* HAL Interface Implementation                                        */
/* ------------------------------------------------------------------ */

hal_status_t hal_console_init(void)
{
    /* Disable UART while configuring */
    uart_write(UARTCR, 0);

    /* Wait for any pending TX to complete */
    while (uart_read(UARTFR) & UARTFR_BUSY)
        ;

    /* Clear all interrupts */
    uart_write(UARTICR, 0x7FF);

    /* Set baud rate: assume 24MHz UART clock, 115200 baud.
     * Divisor = 24000000 / (16 * 115200) = 13.0208
     * IBRD = 13, FBRD = round(0.0208 * 64) = 1 */
    uart_write(UARTIBRD, 13);
    uart_write(UARTFBRD, 1);

    /* 8N1, FIFO enabled */
    uart_write(UARTLCR_H, UARTLCR_H_WLEN_8 | UARTLCR_H_FEN);

    /* Mask all interrupts (we'll poll) */
    uart_write(UARTIMSC, 0);

    /* Enable UART, TX, and RX */
    uart_write(UARTCR, UARTCR_UARTEN | UARTCR_TXE | UARTCR_RXE);

    return HAL_OK;
}

void hal_console_putc(char c)
{
    /* Wait until TX FIFO has space */
    while (uart_read(UARTFR) & UARTFR_TXFF)
        ;

    uart_write(UARTDR, (uint32_t)(unsigned char)c);

    /* Auto-add CR before LF for terminal compatibility */
    if (c == '\n') {
        while (uart_read(UARTFR) & UARTFR_TXFF)
            ;
        uart_write(UARTDR, '\r');
    }
}

void hal_console_puts(const char *s)
{
    while (*s)
        hal_console_putc(*s++);
}

void hal_console_write(const char *s, uint64_t len)
{
    for (uint64_t i = 0; i < len; i++)
        hal_console_putc(s[i]);
}

char hal_console_getc(void)
{
    /* Wait until RX FIFO has data */
    while (uart_read(UARTFR) & UARTFR_RXFE)
        ;

    return (char)(uart_read(UARTDR) & 0xFF);
}

int hal_console_has_input(void)
{
    return !(uart_read(UARTFR) & UARTFR_RXFE);
}

hal_console_type_t hal_console_type(void)
{
    return console_type_val;
}

void hal_console_set_uart_base(uint64_t mmio_base)
{
    uart_base = (volatile uint8_t *)mmio_base;
}

/* ------------------------------------------------------------------ */
/* Minimal printf implementation                                       */
/* ------------------------------------------------------------------ */

static void print_dec(uint64_t val, int is_signed)
{
    if (is_signed && (int64_t)val < 0) {
        hal_console_putc('-');
        val = (uint64_t)(-(int64_t)val);
    }

    char buf[20];
    int pos = 0;

    if (val == 0) {
        hal_console_putc('0');
        return;
    }

    while (val > 0) {
        buf[pos++] = '0' + (val % 10);
        val /= 10;
    }

    while (pos > 0)
        hal_console_putc(buf[--pos]);
}

static void print_hex(uint64_t val, int width)
{
    static const char hex[] = "0123456789abcdef";
    if (width == 0) {
        /* Auto-width: skip leading zeros */
        if (val == 0) {
            hal_console_putc('0');
            return;
        }
        int started = 0;
        for (int i = 60; i >= 0; i -= 4) {
            int nibble = (val >> i) & 0xF;
            if (nibble || started) {
                hal_console_putc(hex[nibble]);
                started = 1;
            }
        }
    } else {
        for (int i = (width - 1) * 4; i >= 0; i -= 4)
            hal_console_putc(hex[(val >> i) & 0xF]);
    }
}

void hal_console_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            hal_console_putc(*fmt++);
            continue;
        }
        fmt++;

        /* Parse optional flags and width: %[0][width][l]<specifier> */
        int zero_pad = 0;
        int width = 0;
        int is_long = 0;

        /* Zero-pad flag */
        if (*fmt == '0') {
            zero_pad = 1;
            fmt++;
        }

        /* Width (single digit for simplicity) */
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* Length modifier: 'l' for long */
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }

        (void)zero_pad; /* TODO: implement zero-padding in print_hex/print_dec */

        switch (*fmt) {
        case 'd': {
            int val = va_arg(ap, int);
            print_dec((uint64_t)val, 1);
            break;
        }
        case 'u': {
            if (is_long) {
                uint64_t val = va_arg(ap, uint64_t);
                print_dec(val, 0);
            } else {
                unsigned val = va_arg(ap, unsigned);
                print_dec(val, 0);
            }
            break;
        }
        case 'x': {
            if (is_long) {
                uint64_t val = va_arg(ap, uint64_t);
                print_hex(val, width);
            } else {
                unsigned val = va_arg(ap, unsigned);
                print_hex(val, width);
            }
            break;
        }
        case 'p': {
            void *ptr = va_arg(ap, void *);
            hal_console_puts("0x");
            print_hex((uint64_t)ptr, 16);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s)
                hal_console_puts(s);
            else
                hal_console_puts("(null)");
            break;
        }
        case '%':
            hal_console_putc('%');
            break;
        case '\0':
            /* Premature end of format string */
            goto done;
        default:
            hal_console_putc('%');
            hal_console_putc(*fmt);
            break;
        }
        fmt++;
    }
done:
    va_end(ap);
}
