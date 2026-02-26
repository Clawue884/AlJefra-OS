/* Minimal boot test — writes directly to COM1 serial port.
 * No HAL, no dependencies. If this outputs, payload loading works. */

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static void serial_putc(char c)
{
    while (!(inb(0x3FD) & 0x20))
        __asm__ volatile ("pause");
    outb(0x3F8, (uint8_t)c);
}

static void serial_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            serial_putc('\r');
        serial_putc(*s++);
    }
}

__attribute__((section(".text.entry"), noreturn))
void _start(void)
{
    serial_puts("\n[UNIBOOT] Payload alive!\n");
    serial_puts("[UNIBOOT] _start at 0x1E0000 reached\n");
    serial_puts("[UNIBOOT] Serial COM1 working\n");

    /* Try a simple memory test */
    volatile uint32_t *test = (volatile uint32_t *)0x200000;
    *test = 0xDEADBEEF;
    if (*test == 0xDEADBEEF) {
        serial_puts("[UNIBOOT] Memory at 0x200000 OK\n");
    } else {
        serial_puts("[UNIBOOT] Memory test FAILED\n");
    }

    serial_puts("[UNIBOOT] Test complete. Halting.\n");

    for (;;)
        __asm__ volatile ("hlt");
}
