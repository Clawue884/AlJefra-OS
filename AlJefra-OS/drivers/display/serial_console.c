/* SPDX-License-Identifier: MIT */
/* AlJefra OS — UART Serial Console Driver Implementation
 * 16550-compatible UART. Uses port I/O on x86, MMIO on ARM/RISC-V.
 */

#include "serial_console.h"

/* ── Register access helpers ── */

static inline uint8_t uart_read(serial_console_t *dev, uint32_t reg)
{
    if (dev->access == UART_ACCESS_PIO)
        return hal_port_in8(dev->pio_base + (uint16_t)reg);
    else
        return hal_mmio_read8((volatile void *)((uint8_t *)dev->mmio_base + reg));
}

static inline void uart_write(serial_console_t *dev, uint32_t reg, uint8_t val)
{
    if (dev->access == UART_ACCESS_PIO)
        hal_port_out8(dev->pio_base + (uint16_t)reg, val);
    else
        hal_mmio_write8((volatile void *)((uint8_t *)dev->mmio_base + reg), val);
}

/* ── Common initialization ── */

static void uart_configure(serial_console_t *dev, uint32_t baud_rate,
                            uint32_t clock_freq)
{
    /* Disable interrupts */
    uart_write(dev, UART_IER, 0x00);

    /* Calculate divisor: divisor = clock / (16 * baud) */
    uint16_t divisor = 1;
    if (baud_rate > 0 && clock_freq > 0)
        divisor = (uint16_t)(clock_freq / (16 * baud_rate));
    if (divisor == 0) divisor = 1;

    /* Enable DLAB to set divisor */
    uart_write(dev, UART_LCR, UART_LCR_DLAB);
    uart_write(dev, UART_DLL, (uint8_t)(divisor & 0xFF));
    uart_write(dev, UART_DLH, (uint8_t)((divisor >> 8) & 0xFF));

    /* Set 8N1, clear DLAB */
    uart_write(dev, UART_LCR, UART_LCR_8N1);

    /* Enable and clear FIFOs, 14-byte trigger */
    uart_write(dev, UART_FCR, UART_FCR_ENABLE | UART_FCR_CLR_RX |
                                UART_FCR_CLR_TX | UART_FCR_TRIG14);

    /* Set DTR, RTS, OUT2 (needed for interrupts on some hardware) */
    uart_write(dev, UART_MCR, UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2);

    dev->baud_rate = baud_rate;
    dev->clock_freq = clock_freq;
}

/* ── Public API ── */

hal_status_t serial_init_pio(serial_console_t *dev, uint16_t port,
                              uint32_t baud_rate)
{
    dev->access = UART_ACCESS_PIO;
    dev->pio_base = port;
    dev->mmio_base = NULL;
    dev->initialized = false;

    /* Scratch register test — verify UART is present */
    uart_write(dev, UART_SCR, 0xAB);
    if (uart_read(dev, UART_SCR) != 0xAB)
        return HAL_NO_DEVICE;

    uart_configure(dev, baud_rate, 1843200);  /* Standard 1.8432 MHz clock */

    dev->initialized = true;
    return HAL_OK;
}

hal_status_t serial_init_mmio(serial_console_t *dev, volatile void *base,
                               uint32_t baud_rate, uint32_t clock_freq)
{
    dev->access = UART_ACCESS_MMIO;
    dev->mmio_base = base;
    dev->pio_base = 0;
    dev->initialized = false;

    uart_configure(dev, baud_rate, clock_freq);

    dev->initialized = true;
    return HAL_OK;
}

hal_status_t serial_init_auto(serial_console_t *dev, uint32_t baud_rate)
{
    switch (hal_arch()) {
    case HAL_ARCH_X86_64:
        return serial_init_pio(dev, UART_COM1_X86, baud_rate);
    case HAL_ARCH_AARCH64:
        return serial_init_mmio(dev, (volatile void *)UART_PL011_ARM,
                                 baud_rate, 24000000);
    case HAL_ARCH_RISCV64:
        return serial_init_mmio(dev, (volatile void *)UART_NS16550_RISCV,
                                 baud_rate, 3686400);
    default:
        return HAL_NOT_SUPPORTED;
    }
}

void serial_putc(serial_console_t *dev, char c)
{
    if (!dev->initialized)
        return;

    /* Wait for Transmitter Holding Register Empty */
    uint32_t timeout = 100000;
    while (timeout-- > 0) {
        if (uart_read(dev, UART_LSR) & UART_LSR_THRE)
            break;
    }

    /* Emit CR before LF for proper line endings on serial terminals */
    if (c == '\n') {
        uart_write(dev, UART_THR, '\r');
        timeout = 100000;
        while (timeout-- > 0) {
            if (uart_read(dev, UART_LSR) & UART_LSR_THRE)
                break;
        }
    }

    uart_write(dev, UART_THR, (uint8_t)c);
}

void serial_puts(serial_console_t *dev, const char *s)
{
    while (*s)
        serial_putc(dev, *s++);
}

void serial_write(serial_console_t *dev, const char *s, uint64_t len)
{
    for (uint64_t i = 0; i < len; i++)
        serial_putc(dev, s[i]);
}

char serial_getc(serial_console_t *dev)
{
    if (!dev->initialized)
        return 0;

    /* Wait for Data Ready */
    while (!(uart_read(dev, UART_LSR) & UART_LSR_DR))
        ;

    return (char)uart_read(dev, UART_RBR);
}

bool serial_has_data(serial_console_t *dev)
{
    if (!dev->initialized)
        return false;
    return (uart_read(dev, UART_LSR) & UART_LSR_DR) != 0;
}

char serial_trygetc(serial_console_t *dev)
{
    if (!dev->initialized)
        return 0;
    if (!(uart_read(dev, UART_LSR) & UART_LSR_DR))
        return 0;
    return (char)uart_read(dev, UART_RBR);
}
