/* SPDX-License-Identifier: MIT */
/* AlJefra OS — HAL Console Interface
 * Early text output for debug and boot messages.
 * Abstracts over UART (serial), VGA text, and framebuffer console.
 */

#ifndef ALJEFRA_HAL_CONSOLE_H
#define ALJEFRA_HAL_CONSOLE_H

#include <stdint.h>

/* Console backend types */
typedef enum {
    HAL_CONSOLE_SERIAL = 0,   /* UART / COM port */
    HAL_CONSOLE_VGA    = 1,   /* VGA text mode (x86) */
    HAL_CONSOLE_LFB    = 2,   /* Linear framebuffer */
} hal_console_type_t;

/* Initialize the earliest-available console.
 * On x86: tries serial COM1, then VGA.
 * On ARM/RISC-V: uses UART from device tree or hardcoded base. */
hal_status_t hal_console_init(void);

/* Output a single character */
void hal_console_putc(char c);

/* Output a null-terminated string */
void hal_console_puts(const char *s);

/* Output a string with explicit length */
void hal_console_write(const char *s, uint64_t len);

/* Formatted output (minimal printf: %s, %d, %u, %x, %p, %%) */
void hal_console_printf(const char *fmt, ...);

/* Read a single character (blocking). Returns 0 if no input available. */
char hal_console_getc(void);

/* Check if a character is available (non-blocking) */
int hal_console_has_input(void);

/* Get current console type */
hal_console_type_t hal_console_type(void);

/* Set UART base address (for device-tree discovered UARTs) */
void hal_console_set_uart_base(uint64_t mmio_base);

#endif /* ALJEFRA_HAL_CONSOLE_H */
