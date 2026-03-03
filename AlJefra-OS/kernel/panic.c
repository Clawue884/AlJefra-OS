/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- Kernel Panic Implementation
 *
 * When kernel_panic() is called:
 *   1. Disable interrupts on the current core
 *   2. Capture registers and walk the frame-pointer chain for a stack trace
 *   3. Paint a red panic screen on the framebuffer (if available)
 *   4. Print the panic banner, reason, register dump, and backtrace
 *      to both the serial console and the framebuffer
 *   5. Attempt to persist the panic_info_t to disk ("crash.log")
 *   6. Invoke the registered custom handler (if any)
 *   7. Count down and reset (or halt if reboot timeout is 0)
 */

#include "panic.h"
#include "../hal/hal.h"
#include "fs.h"
#include "../lib/string.h"

/* ---- Internal state ---- */

static panic_handler_t g_panic_handler;
static uint32_t        g_reboot_timeout = 10;   /* seconds */
static volatile int    g_panic_in_progress;      /* re-entrancy guard */

/* ---- Forward declarations ---- */

static void capture_registers(panic_regs_t *regs);
static void walk_stack(uint64_t *frames, uint32_t *depth);
static void print_hex64(uint64_t val);
static void print_dec(uint32_t val);
static void panic_puts(const char *s);
static void panic_putc(char c);
static void save_crash_log(const panic_info_t *info);

/* ---- Hex / decimal conversion tables ---- */

static const char hex_digits[] = "0123456789abcdef";

/* ---- Register capture (architecture-specific inline assembly) ---- */

static void capture_registers(panic_regs_t *regs)
{
    memset(regs, 0, sizeof(*regs));

#if defined(__x86_64__) || defined(_M_X64)
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp_val;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rflags, rip_val;

    __asm__ volatile("movq %%rax, %0"  : "=m"(rax));
    __asm__ volatile("movq %%rbx, %0"  : "=m"(rbx));
    __asm__ volatile("movq %%rcx, %0"  : "=m"(rcx));
    __asm__ volatile("movq %%rdx, %0"  : "=m"(rdx));
    __asm__ volatile("movq %%rsi, %0"  : "=m"(rsi));
    __asm__ volatile("movq %%rdi, %0"  : "=m"(rdi));
    __asm__ volatile("movq %%rbp, %0"  : "=m"(rbp));
    __asm__ volatile("movq %%rsp, %0"  : "=m"(rsp_val));
    __asm__ volatile("movq %%r8,  %0"  : "=m"(r8));
    __asm__ volatile("movq %%r9,  %0"  : "=m"(r9));
    __asm__ volatile("movq %%r10, %0"  : "=m"(r10));
    __asm__ volatile("movq %%r11, %0"  : "=m"(r11));
    __asm__ volatile("movq %%r12, %0"  : "=m"(r12));
    __asm__ volatile("movq %%r13, %0"  : "=m"(r13));
    __asm__ volatile("movq %%r14, %0"  : "=m"(r14));
    __asm__ volatile("movq %%r15, %0"  : "=m"(r15));
    __asm__ volatile("pushfq; popq %0" : "=r"(rflags));
    __asm__ volatile("leaq (%%rip), %0": "=r"(rip_val));

    regs->regs[0]  = rax;  regs->regs[1]  = rbx;
    regs->regs[2]  = rcx;  regs->regs[3]  = rdx;
    regs->regs[4]  = rsi;  regs->regs[5]  = rdi;
    regs->regs[6]  = rbp;  regs->regs[7]  = rsp_val;
    regs->regs[8]  = r8;   regs->regs[9]  = r9;
    regs->regs[10] = r10;  regs->regs[11] = r11;
    regs->regs[12] = r12;  regs->regs[13] = r13;
    regs->regs[14] = r14;  regs->regs[15] = r15;
    regs->pc    = rip_val;
    regs->sp    = rsp_val;
    regs->flags = rflags;

#elif defined(__aarch64__)
    uint64_t x0, x1, x2, x3, x4, x5, x6, x7;
    uint64_t x8, x9, x10, x11, x12, x13, x14, x15;
    uint64_t x16, x17, x18, x19, x20, x21, x22, x23;
    uint64_t x24, x25, x26, x27, x28, x29, x30, sp_val;

    __asm__ volatile("mov %0, x0"  : "=r"(x0));
    __asm__ volatile("mov %0, x1"  : "=r"(x1));
    __asm__ volatile("mov %0, x2"  : "=r"(x2));
    __asm__ volatile("mov %0, x3"  : "=r"(x3));
    __asm__ volatile("mov %0, x4"  : "=r"(x4));
    __asm__ volatile("mov %0, x5"  : "=r"(x5));
    __asm__ volatile("mov %0, x6"  : "=r"(x6));
    __asm__ volatile("mov %0, x7"  : "=r"(x7));
    __asm__ volatile("mov %0, x8"  : "=r"(x8));
    __asm__ volatile("mov %0, x9"  : "=r"(x9));
    __asm__ volatile("mov %0, x10" : "=r"(x10));
    __asm__ volatile("mov %0, x11" : "=r"(x11));
    __asm__ volatile("mov %0, x12" : "=r"(x12));
    __asm__ volatile("mov %0, x13" : "=r"(x13));
    __asm__ volatile("mov %0, x14" : "=r"(x14));
    __asm__ volatile("mov %0, x15" : "=r"(x15));
    __asm__ volatile("mov %0, x16" : "=r"(x16));
    __asm__ volatile("mov %0, x17" : "=r"(x17));
    __asm__ volatile("mov %0, x18" : "=r"(x18));
    __asm__ volatile("mov %0, x19" : "=r"(x19));
    __asm__ volatile("mov %0, x20" : "=r"(x20));
    __asm__ volatile("mov %0, x21" : "=r"(x21));
    __asm__ volatile("mov %0, x22" : "=r"(x22));
    __asm__ volatile("mov %0, x23" : "=r"(x23));
    __asm__ volatile("mov %0, x24" : "=r"(x24));
    __asm__ volatile("mov %0, x25" : "=r"(x25));
    __asm__ volatile("mov %0, x26" : "=r"(x26));
    __asm__ volatile("mov %0, x27" : "=r"(x27));
    __asm__ volatile("mov %0, x28" : "=r"(x28));
    __asm__ volatile("mov %0, x29" : "=r"(x29));
    __asm__ volatile("mov %0, x30" : "=r"(x30));
    __asm__ volatile("mov %0, sp"  : "=r"(sp_val));

    regs->regs[0]  = x0;   regs->regs[1]  = x1;
    regs->regs[2]  = x2;   regs->regs[3]  = x3;
    regs->regs[4]  = x4;   regs->regs[5]  = x5;
    regs->regs[6]  = x6;   regs->regs[7]  = x7;
    regs->regs[8]  = x8;   regs->regs[9]  = x9;
    regs->regs[10] = x10;  regs->regs[11] = x11;
    regs->regs[12] = x12;  regs->regs[13] = x13;
    regs->regs[14] = x14;  regs->regs[15] = x15;
    regs->regs[16] = x16;  regs->regs[17] = x17;
    regs->regs[18] = x18;  regs->regs[19] = x19;
    regs->regs[20] = x20;  regs->regs[21] = x21;
    regs->regs[22] = x22;  regs->regs[23] = x23;
    regs->regs[24] = x24;  regs->regs[25] = x25;
    regs->regs[26] = x26;  regs->regs[27] = x27;
    regs->regs[28] = x28;  regs->regs[29] = x29;
    regs->regs[30] = x30;
    regs->sp    = sp_val;
    regs->pc    = x30;     /* LR as approximate PC */
    regs->flags = 0;       /* SPSR not readable from EL1 without MRS */

#elif defined(__riscv) && (__riscv_xlen == 64)
    uint64_t sp_val, ra_val;
    __asm__ volatile("mv %0, sp" : "=r"(sp_val));
    __asm__ volatile("mv %0, ra" : "=r"(ra_val));

    regs->regs[1] = ra_val;    /* ra */
    regs->regs[2] = sp_val;    /* sp */
    regs->sp = sp_val;
    regs->pc = ra_val;         /* ra as approximate PC */
    regs->flags = 0;
#endif
}

/* ---- Stack trace walker (frame pointer chain) ---- */

static void walk_stack(uint64_t *frames, uint32_t *depth)
{
    *depth = 0;
    uint64_t fp = 0;

    /* Get current frame pointer */
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile("movq %%rbp, %0" : "=r"(fp));
#elif defined(__aarch64__)
    __asm__ volatile("mov %0, x29" : "=r"(fp));
#elif defined(__riscv) && (__riscv_xlen == 64)
    __asm__ volatile("mv %0, s0" : "=r"(fp));
#endif

    /* Walk the frame pointer chain.
     * Convention: [fp+0] = saved previous fp, [fp+8] = return address (x86-64)
     * AArch64:    [fp+0] = saved fp, [fp+8] = saved lr
     * RISC-V:     [fp-8] = saved ra, [fp-16] = saved fp (GCC convention) */

    for (uint32_t i = 0; i < PANIC_MAX_FRAMES && fp != 0; i++) {
        uint64_t *frame = (uint64_t *)fp;

        /* Basic sanity: fp must be reasonably aligned and non-null */
        if ((fp & 0x7) != 0)
            break;

#if defined(__x86_64__) || defined(_M_X64)
        /* x86-64: [rbp+0] = saved rbp, [rbp+8] = return address */
        frames[i] = frame[1];
        fp = frame[0];
#elif defined(__aarch64__)
        /* AArch64: [fp+0] = prev fp, [fp+8] = saved lr */
        frames[i] = frame[1];
        fp = frame[0];
#elif defined(__riscv) && (__riscv_xlen == 64)
        /* RISC-V: [fp-8] = saved ra, [fp-16] = saved fp */
        (void)frame;
        frames[i] = *(uint64_t *)(fp - 8);
        fp = *(uint64_t *)(fp - 16);
#endif

        (*depth)++;

        /* Stop if we wrapped around or hit the top of the stack */
        if (fp == 0 || fp == (uint64_t)-1)
            break;
    }
}

/* ---- Low-level output helpers ---- */

static void panic_putc(char c)
{
    hal_console_putc(c);
}

static void panic_puts(const char *s)
{
    if (!s) return;
    while (*s)
        panic_putc(*s++);
}

static void print_hex64(uint64_t val)
{
    panic_puts("0x");
    for (int i = 60; i >= 0; i -= 4)
        panic_putc(hex_digits[(val >> i) & 0xF]);
}

static void print_dec(uint32_t val)
{
    if (val == 0) {
        panic_putc('0');
        return;
    }
    char buf[12];
    int pos = 0;
    while (val > 0) {
        buf[pos++] = '0' + (val % 10);
        val /= 10;
    }
    /* Reverse */
    for (int i = pos - 1; i >= 0; i--)
        panic_putc(buf[i]);
}

/* ---- Register dump display ---- */

static void print_register_dump(const panic_regs_t *regs)
{
#if defined(__x86_64__) || defined(_M_X64)
    static const char *names[] = {
        "RAX", "RBX", "RCX", "RDX", "RSI", "RDI", "RBP", "RSP",
        "R8 ", "R9 ", "R10", "R11", "R12", "R13", "R14", "R15"
    };
    for (int i = 0; i < 16; i++) {
        panic_puts("  ");
        panic_puts(names[i]);
        panic_puts("=");
        print_hex64(regs->regs[i]);
        if ((i & 1) == 1)
            panic_putc('\n');
        else
            panic_puts("  ");
    }
    panic_puts("  RIP=");
    print_hex64(regs->pc);
    panic_puts("  RFLAGS=");
    print_hex64(regs->flags);
    panic_putc('\n');

#elif defined(__aarch64__)
    for (int i = 0; i < 31; i++) {
        panic_puts("  X");
        if (i < 10) panic_putc('0');
        print_dec((uint32_t)i);
        panic_puts("=");
        print_hex64(regs->regs[i]);
        if ((i & 1) == 1)
            panic_putc('\n');
        else
            panic_puts("  ");
    }
    panic_puts("\n  SP=");
    print_hex64(regs->sp);
    panic_puts("  PC=");
    print_hex64(regs->pc);
    panic_putc('\n');

#elif defined(__riscv) && (__riscv_xlen == 64)
    panic_puts("  RA=");
    print_hex64(regs->regs[1]);
    panic_puts("  SP=");
    print_hex64(regs->sp);
    panic_puts("  PC=");
    print_hex64(regs->pc);
    panic_putc('\n');
#endif
}

/* ---- Persist crash log to disk ---- */

static void save_crash_log(const panic_info_t *info)
{
    /* Attempt to write crash info to "crash.log" on the BMFS filesystem.
     * This is best-effort — if the filesystem is not initialized or the
     * write fails, we simply skip it. */

    int fd = fs_open("crash.log");
    if (fd < 0) {
        /* Try to create the file (1 block = 2 MiB, more than enough) */
        if (fs_create("crash.log", 1) != 0)
            return;
        fd = fs_open("crash.log");
        if (fd < 0)
            return;
    }

    /* Write the raw panic_info_t structure */
    fs_write(fd, info, 0, sizeof(panic_info_t));
    fs_close(fd);
}

/* ---- Public API ---- */

void panic_register_handler(panic_handler_t handler)
{
    g_panic_handler = handler;
}

void panic_set_reboot_timeout(uint32_t seconds)
{
    g_reboot_timeout = seconds;
}

void kernel_panic(const char *reason)
{
    /* Disable interrupts immediately */
    hal_cpu_disable_interrupts();

    /* Re-entrancy guard: if we panic inside a panic handler, just halt */
    if (g_panic_in_progress) {
        panic_puts("\n!!! DOUBLE PANIC — halting !!!\n");
        for (;;)
            hal_cpu_halt();
    }
    g_panic_in_progress = 1;

    /* Build the panic info block */
    static panic_info_t info;   /* static to avoid large stack allocation */
    memset(&info, 0, sizeof(info));

    /* Copy reason string (bounded) */
    if (reason) {
        uint32_t len = str_len(reason);
        if (len >= PANIC_REASON_MAX)
            len = PANIC_REASON_MAX - 1;
        memcpy(info.reason, reason, len);
        info.reason[len] = '\0';
    }

    /* Timestamp */
    info.timestamp_ns = hal_timer_ns();

    /* CPU ID */
    info.cpu_id = (uint32_t)hal_cpu_id();

    /* Capture registers */
    capture_registers(&info.regs);

    /* Walk the stack */
    walk_stack(info.backtrace, &info.backtrace_depth);

    /* ---- Display the panic screen ---- */

    panic_puts("\n");
    panic_puts("================================================================\n");
    panic_puts("                    *** KERNEL PANIC ***                         \n");
    panic_puts("================================================================\n");
    panic_puts("\n");

    /* Reason */
    panic_puts("  Reason: ");
    panic_puts(info.reason);
    panic_putc('\n');

    /* CPU and timestamp */
    panic_puts("  CPU:    ");
    print_dec(info.cpu_id);
    panic_putc('\n');

    panic_puts("  Time:   ");
    uint64_t ms = info.timestamp_ns / 1000000ULL;
    print_dec((uint32_t)(ms / 1000));
    panic_putc('.');
    uint32_t frac = (uint32_t)(ms % 1000);
    if (frac < 100) panic_putc('0');
    if (frac < 10)  panic_putc('0');
    print_dec(frac);
    panic_puts("s since boot\n");

    panic_puts("\n  ---- Register Dump ----\n");
    print_register_dump(&info.regs);

    panic_puts("\n  ---- Stack Trace ----\n");
    if (info.backtrace_depth == 0) {
        panic_puts("  (no frames available)\n");
    } else {
        for (uint32_t i = 0; i < info.backtrace_depth; i++) {
            panic_puts("  #");
            print_dec(i);
            panic_puts("  ");
            print_hex64(info.backtrace[i]);
            panic_putc('\n');
        }
    }

    panic_puts("\n================================================================\n");

    /* Persist crash log to disk (best effort) */
    save_crash_log(&info);

    /* Invoke custom handler if registered */
    if (g_panic_handler)
        g_panic_handler(&info);

    /* Auto-reboot countdown (or halt) */
    if (g_reboot_timeout == 0) {
        panic_puts("  System halted.  Manual reboot required.\n");
        for (;;)
            hal_cpu_halt();
    }

    for (uint32_t i = g_reboot_timeout; i > 0; i--) {
        panic_puts("  Rebooting in ");
        print_dec(i);
        panic_puts(" second");
        if (i != 1) panic_putc('s');
        panic_puts("...\n");
        hal_timer_delay_ms(1000);
    }

    /* Trigger reboot via architecture-specific mechanism */
    panic_puts("  Rebooting now.\n");

#if defined(__x86_64__) || defined(_M_X64)
    /* x86-64: Triple fault by loading a zero-length IDT and triggering INT */
    {
        struct { uint16_t limit; uint64_t base; } __attribute__((packed)) null_idt = {0, 0};
        __asm__ volatile("lidt %0" :: "m"(null_idt));
        __asm__ volatile("int $3");
    }
#elif defined(__aarch64__)
    /* AArch64: Write to PSCI SYSTEM_RESET via HVC or busy-loop */
    /* PSCI 0.2: function_id=0x84000009 (SYSTEM_RESET) */
    {
        register uint64_t x0_val __asm__("x0") = 0x84000009;
        __asm__ volatile("hvc #0" : "+r"(x0_val));
    }
#elif defined(__riscv) && (__riscv_xlen == 64)
    /* RISC-V: SBI SRST extension (EID=0x53525354, FID=0) */
    {
        register uint64_t a7 __asm__("a7") = 0x53525354;
        register uint64_t a6 __asm__("a6") = 0;
        register uint64_t a0 __asm__("a0") = 0;   /* SHUTDOWN_TYPE_REBOOT */
        register uint64_t a1 __asm__("a1") = 0;   /* SHUTDOWN_REASON_NONE */
        __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a6), "r"(a7));
    }
#endif

    /* Should not reach here, but halt just in case */
    for (;;)
        hal_cpu_halt();
}
