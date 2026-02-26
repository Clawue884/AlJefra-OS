/* AlJefra OS Boot Loader Stub
 *
 * This small program replaces monitor.bin (< 6KB) and loads the full
 * AlJefra kernel from a known BMFS disk location into memory at 0x1000000
 * (16MB), then jumps to it.
 *
 * The AlJefra kernel binary is stored at sector 10 within the BMFS
 * partition (sector 32778 from disk start in the hybrid image).
 *
 * Build:
 *   gcc -m64 -march=x86-64 -mno-red-zone -ffreestanding -fno-stack-protector \
 *       -fno-pic -fno-pie -nostdinc -nostdlib -mcmodel=kernel -O2 -c loader.c
 *   ld -T payload.ld -nostdlib -o loader.elf loader.o
 *   objcopy -O binary loader.elf loader.bin
 */

typedef unsigned long  u64;
typedef unsigned int   u32;
typedef unsigned short u16;
typedef unsigned char  u8;

/* AlJefra kernel API calls */
static inline void serial_putc(char c)
{
    /* Direct COM1 I/O — same as boot_test.c */
    while (!(({u8 v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"((u16)0x3FD)); v;}) & 0x20))
        ;
    __asm__ volatile("outb %0,%1"::"a"((u8)c),"Nd"((u16)0x3F8));
}

static void serial_puts(const char *s)
{
    while (*s)
        serial_putc(*s++);
}

static void serial_hex(u64 val, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    for (int i = (digits - 1) * 4; i >= 0; i -= 4)
        serial_putc(hex[(val >> i) & 0xF]);
}

/* b_nvs_read: RAX=start_sector(4K), RCX=num_sectors, RDX=drivenum, RDI=mem */
static u64 nvs_read(void *mem, u64 start, u64 num, u64 drivenum)
{
    u64 result;
    __asm__ volatile(
        "call *0x00100030"
        : "=c"(result)
        : "a"(start), "c"(num), "d"(drivenum), "D"(mem)
        : "memory"
    );
    return result;
}

/* Configuration */
#define BMFS_DISK_OFFSET  32768   /* BMFS partition start in 4K sectors (128MB) */
#define KERNEL_BMFS_SECTOR  10    /* Sector within BMFS where kernel is stored */
#define KERNEL_SECTORS      18    /* ceil(69928 / 4096) */
#define KERNEL_LOAD_ADDR    0x1000000UL  /* 16MB — safe, identity-mapped */

__attribute__((section(".text.entry"), noreturn))
void _start(void)
{
    serial_puts("\n[LOADER] AlJefra OS Boot Loader v1.0\n");

    /* Calculate absolute disk sector for the kernel */
    u64 disk_sector = BMFS_DISK_OFFSET + KERNEL_BMFS_SECTOR;

    serial_puts("[LOADER] Reading kernel from disk sector ");
    serial_hex(disk_sector, 8);
    serial_puts(", ");
    serial_hex(KERNEL_SECTORS, 2);
    serial_puts(" sectors\n");

    /* Read the AlJefra kernel from disk to 16MB */
    void *dest = (void *)KERNEL_LOAD_ADDR;
    u64 result = nvs_read(dest, disk_sector, KERNEL_SECTORS, 0);

    serial_puts("[LOADER] Read complete, result=");
    serial_hex(result, 4);
    serial_puts("\n");

    /* Verify first bytes aren't zero (sanity check) */
    u64 first_qword = *(volatile u64 *)dest;
    serial_puts("[LOADER] First 8 bytes at 0x1000000: ");
    serial_hex(first_qword, 16);
    serial_puts("\n");

    if (first_qword == 0) {
        serial_puts("[LOADER] ERROR: Kernel appears empty! Halting.\n");
        for (;;) __asm__ volatile("hlt");
    }

    serial_puts("[LOADER] Jumping to AlJefra kernel at 0x1000000...\n");

    /* Jump to the AlJefra kernel */
    void (*kernel_entry)(void) = (void (*)(void))KERNEL_LOAD_ADDR;
    kernel_entry();

    /* Should never reach here */
    for (;;) __asm__ volatile("hlt");
}
