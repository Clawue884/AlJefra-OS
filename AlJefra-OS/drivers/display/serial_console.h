/* SPDX-License-Identifier: MIT */
/* AlJefra OS — UART Serial Console Driver
 * 16550-compatible UART driver.
 * Architecture-independent via HAL: uses port I/O on x86, MMIO elsewhere.
 */

#ifndef ALJEFRA_DRV_SERIAL_CONSOLE_H
#define ALJEFRA_DRV_SERIAL_CONSOLE_H

#include "../../hal/hal.h"

/* ── 16550 UART Register offsets ── */
#define UART_THR    0   /* Transmitter Holding Register (write, DLAB=0) */
#define UART_RBR    0   /* Receiver Buffer Register (read, DLAB=0) */
#define UART_DLL    0   /* Divisor Latch Low (DLAB=1) */
#define UART_DLH    1   /* Divisor Latch High (DLAB=1) */
#define UART_IER    1   /* Interrupt Enable Register (DLAB=0) */
#define UART_IIR    2   /* Interrupt Identification Register (read) */
#define UART_FCR    2   /* FIFO Control Register (write) */
#define UART_LCR    3   /* Line Control Register */
#define UART_MCR    4   /* Modem Control Register */
#define UART_LSR    5   /* Line Status Register */
#define UART_MSR    6   /* Modem Status Register */
#define UART_SCR    7   /* Scratch Register */

/* ── LSR bits ── */
#define UART_LSR_DR     (1u << 0)   /* Data Ready */
#define UART_LSR_OE     (1u << 1)   /* Overrun Error */
#define UART_LSR_PE     (1u << 2)   /* Parity Error */
#define UART_LSR_FE     (1u << 3)   /* Framing Error */
#define UART_LSR_THRE   (1u << 5)   /* Transmitter Holding Register Empty */
#define UART_LSR_TEMT   (1u << 6)   /* Transmitter Empty */

/* ── LCR bits ── */
#define UART_LCR_8N1    0x03        /* 8 data bits, no parity, 1 stop bit */
#define UART_LCR_DLAB   (1u << 7)  /* Divisor Latch Access Bit */

/* ── FCR bits ── */
#define UART_FCR_ENABLE (1u << 0)   /* FIFO Enable */
#define UART_FCR_CLR_RX (1u << 1)   /* Clear Receive FIFO */
#define UART_FCR_CLR_TX (1u << 2)   /* Clear Transmit FIFO */
#define UART_FCR_TRIG14 (3u << 6)   /* 14-byte trigger level */

/* ── MCR bits ── */
#define UART_MCR_DTR    (1u << 0)
#define UART_MCR_RTS    (1u << 1)
#define UART_MCR_OUT2   (1u << 3)   /* Required for interrupts on some UARTs */

/* ── Common base addresses ── */
#define UART_COM1_X86     0x03F8
#define UART_COM2_X86     0x02F8
#define UART_PL011_ARM    0x09000000ULL  /* QEMU virt ARM */
#define UART_NS16550_RISCV 0x10000000ULL /* QEMU virt RISC-V */

/* ── Access mode ── */
typedef enum {
    UART_ACCESS_PIO  = 0,   /* x86 port I/O */
    UART_ACCESS_MMIO = 1,   /* Memory-mapped I/O */
} uart_access_t;

/* ── Serial console state ── */
typedef struct {
    uart_access_t   access;
    uint16_t        pio_base;     /* Port I/O base (x86) */
    volatile void  *mmio_base;    /* MMIO base (ARM/RISC-V) */
    uint32_t        baud_rate;
    uint32_t        clock_freq;   /* Input clock frequency */
    bool            initialized;
} serial_console_t;

/* ── Public API ── */

/* Initialize with port I/O (x86 COM port) */
hal_status_t serial_init_pio(serial_console_t *dev, uint16_t port,
                              uint32_t baud_rate);

/* Initialize with MMIO (ARM/RISC-V UART) */
hal_status_t serial_init_mmio(serial_console_t *dev, volatile void *base,
                               uint32_t baud_rate, uint32_t clock_freq);

/* Auto-detect: uses port I/O on x86, MMIO on ARM/RISC-V */
hal_status_t serial_init_auto(serial_console_t *dev, uint32_t baud_rate);

/* Output a single character (blocking) */
void serial_putc(serial_console_t *dev, char c);

/* Output a null-terminated string */
void serial_puts(serial_console_t *dev, const char *s);

/* Output a string with explicit length */
void serial_write(serial_console_t *dev, const char *s, uint64_t len);

/* Read a character (blocking) */
char serial_getc(serial_console_t *dev);

/* Check if a character is available (non-blocking) */
bool serial_has_data(serial_console_t *dev);

/* Read a character (non-blocking, returns 0 if none available) */
char serial_trygetc(serial_console_t *dev);

#endif /* ALJEFRA_DRV_SERIAL_CONSOLE_H */
