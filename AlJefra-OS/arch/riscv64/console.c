/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- RISC-V 64-bit Console Implementation
 * Implements hal/console.h using NS16550A UART and SBI console fallback.
 *
 * NS16550A register map (at MMIO base, 1-byte spacing):
 *   0x00 RBR/THR - Receive Buffer / Transmit Holding
 *   0x01 IER     - Interrupt Enable
 *   0x02 IIR/FCR - Interrupt ID / FIFO Control
 *   0x03 LCR     - Line Control
 *   0x04 MCR     - Modem Control
 *   0x05 LSR     - Line Status
 *   0x06 MSR     - Modem Status
 *   0x07 SCR     - Scratch
 *
 * LSR bits:
 *   Bit 0: DR   (Data Ready)
 *   Bit 5: THRE (Transmit Holding Register Empty)
 *   Bit 6: TEMT (Transmitter Empty)
 *
 * QEMU virt UART base: 0x10000000
 *
 * SBI Legacy putchar (EID=1, deprecated but universally available):
 *   a7=1, a0=char, ecall
 * SBI Debug Console (EID=0x4442434E "DBCN"):
 *   FID 0: write, FID 1: read, FID 2: write_byte
 */

#include "../../hal/hal.h"
#include <stdarg.h>

/* ------------------------------------------------------------------ */
/* NS16550A Register Offsets                                           */
/* ------------------------------------------------------------------ */

#define UART_RBR    0x00    /* Receive Buffer (read) */
#define UART_THR    0x00    /* Transmit Holding (write) */
#define UART_IER    0x01    /* Interrupt Enable */
#define UART_FCR    0x02    /* FIFO Control (write) */
#define UART_LCR    0x03    /* Line Control */
#define UART_MCR    0x04    /* Modem Control */
#define UART_LSR    0x05    /* Line Status */

/* Divisor Latch registers (when LCR.DLAB=1) */
#define UART_DLL    0x00    /* Divisor Latch Low */
#define UART_DLM    0x01    /* Divisor Latch High */

/* LSR bits */
#define LSR_DR      (1 << 0)    /* Data Ready */
#define LSR_THRE    (1 << 5)    /* TX Holding Register Empty */

/* LCR bits */
#define LCR_8N1     0x03        /* 8 data bits, no parity, 1 stop bit */
#define LCR_DLAB    (1 << 7)    /* Divisor Latch Access Bit */

/* FCR bits */
#define FCR_FIFO_EN (1 << 0)    /* FIFO Enable */
#define FCR_RX_CLR  (1 << 1)    /* Clear RX FIFO */
#define FCR_TX_CLR  (1 << 2)    /* Clear TX FIFO */

/* ------------------------------------------------------------------ */
/* SBI console (legacy fallback)                                       */
/* ------------------------------------------------------------------ */

#define SBI_LEGACY_PUTCHAR  1
#define SBI_LEGACY_GETCHAR  2

static void sbi_putchar(char c)
{
    register uint64_t a0 __asm__("a0") = (uint64_t)(unsigned char)c;
    register uint64_t a7 __asm__("a7") = SBI_LEGACY_PUTCHAR;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

static int sbi_getchar(void)
{
    register uint64_t a0 __asm__("a0") = 0;
    register uint64_t a7 __asm__("a7") = SBI_LEGACY_GETCHAR;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return (int)(int64_t)a0;  /* -1 if no char available */
}

/* ------------------------------------------------------------------ */
/* Private state                                                       */
/* ------------------------------------------------------------------ */

static volatile uint8_t *uart_base = (volatile uint8_t *)0x10000000ULL;
static int use_ns16550 = 0;  /* 0 = SBI fallback, 1 = NS16550 MMIO */

/* ------------------------------------------------------------------ */
/* UART MMIO helpers                                                   */
/* ------------------------------------------------------------------ */

static inline uint8_t uart_read(uint32_t off)
{
    uint8_t v = *(volatile uint8_t *)(uart_base + off);
    __asm__ volatile("fence i, i" ::: "memory");
    return v;
}

static inline void uart_write(uint32_t off, uint8_t val)
{
    __asm__ volatile("fence o, o" ::: "memory");
    *(volatile uint8_t *)(uart_base + off) = val;
}

/* ------------------------------------------------------------------ */
/* HAL Interface Implementation                                        */
/* ------------------------------------------------------------------ */

hal_status_t hal_console_init(void)
{
    /* Try to detect NS16550 by reading LSR -- should have THRE set */
    uint8_t lsr = uart_read(UART_LSR);
    if (lsr == 0xFF || lsr == 0x00) {
        /* No UART detected, use SBI fallback */
        use_ns16550 = 0;
        return HAL_OK;
    }

    use_ns16550 = 1;

    /* Disable all interrupts */
    uart_write(UART_IER, 0x00);

    /* Set baud rate: 115200 @ 1.8432 MHz clock
     * Divisor = 1843200 / (16 * 115200) = 1 */
    uart_write(UART_LCR, LCR_DLAB);      /* Enable DLAB */
    uart_write(UART_DLL, 1);              /* Divisor low byte */
    uart_write(UART_DLM, 0);              /* Divisor high byte */
    uart_write(UART_LCR, LCR_8N1);       /* 8N1, disable DLAB */

    /* Enable FIFO, clear buffers */
    uart_write(UART_FCR, FCR_FIFO_EN | FCR_RX_CLR | FCR_TX_CLR);

    /* RTS/DTR */
    uart_write(UART_MCR, 0x03);

    return HAL_OK;
}

void hal_console_putc(char c)
{
    if (use_ns16550) {
        /* Wait for THRE (TX holding register empty) */
        while (!(uart_read(UART_LSR) & LSR_THRE))
            ;
        uart_write(UART_THR, (uint8_t)c);

        /* Auto CR before LF */
        if (c == '\n') {
            while (!(uart_read(UART_LSR) & LSR_THRE))
                ;
            uart_write(UART_THR, '\r');
        }
    } else {
        sbi_putchar(c);
        if (c == '\n')
            sbi_putchar('\r');
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
    if (use_ns16550) {
        while (!(uart_read(UART_LSR) & LSR_DR))
            ;
        return (char)uart_read(UART_RBR);
    } else {
        int c;
        do {
            c = sbi_getchar();
        } while (c < 0);
        return (char)c;
    }
}

int hal_console_has_input(void)
{
    if (use_ns16550) {
        return (uart_read(UART_LSR) & LSR_DR) ? 1 : 0;
    } else {
        return (sbi_getchar() >= 0) ? 1 : 0;
    }
}

hal_console_type_t hal_console_type(void)
{
    return HAL_CONSOLE_SERIAL;
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

        /* Parse optional zero-pad width: %02x, %04x, %08x, etc. */
        int width = 0;
        if (*fmt == '0') {
            fmt++;  /* skip '0' prefix -- print_hex always zero-pads */
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* Parse optional 'l' length modifier (ignored, args promoted) */
        if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') { /* ll */
                fmt++;
            }
        }

        switch (*fmt) {
        case 'd': {
            int val = va_arg(ap, int);
            print_dec((uint64_t)val, 1);
            break;
        }
        case 'u': {
            unsigned val = va_arg(ap, unsigned);
            print_dec(val, 0);
            break;
        }
        case 'x': {
            unsigned val = va_arg(ap, unsigned);
            if (width > 0)
                print_hex(val, width);
            else
                print_hex(val, 0);
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
            hal_console_puts(s ? s : "(null)");
            break;
        }
        case 'c': {
            int c = va_arg(ap, int);
            hal_console_putc((char)c);
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
