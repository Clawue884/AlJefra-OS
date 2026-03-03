/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- Persistent Kernel Logging Implementation
 *
 * Ring buffer of KLOG_MAX_ENTRIES log entries.  Each entry stores a
 * nanosecond timestamp, severity level, and formatted message text.
 *
 * Auto-flush: When the buffer reaches 75% capacity, klog() triggers
 * an automatic flush to disk.  The flush writes all entries sequentially
 * to "klog.dat" on the BMFS filesystem.
 *
 * Boot-time replay: klog_init() reads "klog.dat" from the previous boot
 * and prints a summary to the console so operators can see the last crash
 * or shutdown context.
 */

#include "klog.h"
#include "../hal/hal.h"
#include "fs.h"
#include "../lib/string.h"
#include <stdarg.h>

/* ---- Log entry structure ---- */

typedef struct {
    uint64_t     timestamp_ns;           /* hal_timer_ns() at log time */
    klog_level_t level;                  /* Severity */
    char         msg[KLOG_MSG_SIZE];     /* Formatted message text */
} klog_entry_t;

/* ---- On-disk header (written at the start of klog.dat) ---- */

#define KLOG_MAGIC  0x4B4C4F47   /* "KLOG" in little-endian ASCII */

typedef struct {
    uint32_t magic;              /* KLOG_MAGIC */
    uint32_t entry_count;        /* Number of valid entries following */
    uint32_t head;               /* Ring buffer head at flush time */
    uint32_t _reserved;
} klog_disk_header_t;

/* ---- Internal state ---- */

static klog_entry_t  g_ring[KLOG_MAX_ENTRIES];
static uint32_t      g_head;             /* Next write index */
static uint32_t      g_count;            /* Total entries written (saturates at MAX) */
static klog_level_t  g_min_level = KLOG_DEBUG;
static int           g_initialized;
static int           g_flushing;         /* Re-entrancy guard for auto-flush */

/* Threshold for auto-flush (75% of ring capacity) */
#define KLOG_FLUSH_THRESHOLD  ((KLOG_MAX_ENTRIES * 3) / 4)

/* ---- Printf-like formatter ---- */

/* Internal: append a character to a bounded buffer */
static inline void buf_putc(char *buf, uint32_t *pos, uint32_t max, char c)
{
    if (*pos < max - 1) {
        buf[*pos] = c;
        (*pos)++;
    }
}

/* Internal: append a string to a bounded buffer */
static void buf_puts(char *buf, uint32_t *pos, uint32_t max, const char *s)
{
    if (!s) s = "(null)";
    while (*s)
        buf_putc(buf, pos, max, *s++);
}

/* Internal: append a decimal integer (signed) */
static void buf_putd(char *buf, uint32_t *pos, uint32_t max, int64_t val)
{
    if (val < 0) {
        buf_putc(buf, pos, max, '-');
        val = -val;
    }
    if (val == 0) {
        buf_putc(buf, pos, max, '0');
        return;
    }
    char tmp[20];
    int len = 0;
    uint64_t uv = (uint64_t)val;
    while (uv > 0) {
        tmp[len++] = '0' + (uv % 10);
        uv /= 10;
    }
    for (int i = len - 1; i >= 0; i--)
        buf_putc(buf, pos, max, tmp[i]);
}

/* Internal: append an unsigned decimal integer */
static void buf_putu(char *buf, uint32_t *pos, uint32_t max, uint64_t val)
{
    if (val == 0) {
        buf_putc(buf, pos, max, '0');
        return;
    }
    char tmp[20];
    int len = 0;
    while (val > 0) {
        tmp[len++] = '0' + (val % 10);
        val /= 10;
    }
    for (int i = len - 1; i >= 0; i--)
        buf_putc(buf, pos, max, tmp[i]);
}

/* Internal: append a hex integer (no prefix) */
static void buf_putx(char *buf, uint32_t *pos, uint32_t max, uint64_t val)
{
    static const char hx[] = "0123456789abcdef";
    if (val == 0) {
        buf_putc(buf, pos, max, '0');
        return;
    }
    /* Find highest non-zero nibble */
    int shift = 60;
    while (shift > 0 && ((val >> shift) & 0xF) == 0)
        shift -= 4;
    for (; shift >= 0; shift -= 4)
        buf_putc(buf, pos, max, hx[(val >> shift) & 0xF]);
}

/* Format a message into a bounded buffer.
 * Supports: %d (int), %u (unsigned), %x (hex), %p (pointer), %s (string), %% */
static void klog_vformat(char *buf, uint32_t max, const char *fmt, va_list ap)
{
    uint32_t pos = 0;

    while (*fmt && pos < max - 1) {
        if (*fmt != '%') {
            buf_putc(buf, &pos, max, *fmt++);
            continue;
        }
        fmt++;  /* skip '%' */

        switch (*fmt) {
        case 'd': {
            int val = va_arg(ap, int);
            buf_putd(buf, &pos, max, val);
            break;
        }
        case 'u': {
            unsigned int val = va_arg(ap, unsigned int);
            buf_putu(buf, &pos, max, val);
            break;
        }
        case 'x': {
            unsigned int val = va_arg(ap, unsigned int);
            buf_putx(buf, &pos, max, val);
            break;
        }
        case 'p': {
            uint64_t val = (uint64_t)(uintptr_t)va_arg(ap, void *);
            buf_puts(buf, &pos, max, "0x");
            buf_putx(buf, &pos, max, val);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            buf_puts(buf, &pos, max, s);
            break;
        }
        case '%':
            buf_putc(buf, &pos, max, '%');
            break;
        case '\0':
            /* Trailing '%' at end of format string */
            goto done;
        default:
            /* Unknown specifier: emit literal */
            buf_putc(buf, &pos, max, '%');
            buf_putc(buf, &pos, max, *fmt);
            break;
        }
        fmt++;
    }
done:
    buf[pos] = '\0';
}

/* ---- Level name helper ---- */

static const char *level_name(klog_level_t level)
{
    switch (level) {
    case KLOG_DEBUG: return "DEBUG";
    case KLOG_INFO:  return "INFO ";
    case KLOG_WARN:  return "WARN ";
    case KLOG_ERROR: return "ERROR";
    case KLOG_FATAL: return "FATAL";
    default:         return "?????";
    }
}

/* ---- Print a single entry to console ---- */

static void print_entry(const klog_entry_t *e)
{
    /* Format: [  1234.567] INFO  message text */
    uint64_t ms = e->timestamp_ns / 1000000ULL;
    uint32_t sec = (uint32_t)(ms / 1000);
    uint32_t frac = (uint32_t)(ms % 1000);

    hal_console_printf("[%5u.%03u] %s  %s\n",
                       sec, frac, level_name(e->level), e->msg);
}

/* ---- Boot-time replay from disk ---- */

static void replay_from_disk(void)
{
    int fd = fs_open(KLOG_FILE_NAME);
    if (fd < 0)
        return;     /* No previous log file — normal for first boot */

    uint64_t size = fs_size(fd);
    if (size < sizeof(klog_disk_header_t)) {
        fs_close(fd);
        return;
    }

    /* Read header */
    klog_disk_header_t hdr;
    int64_t rd = fs_read(fd, &hdr, 0, sizeof(hdr));
    if (rd < (int64_t)sizeof(hdr) || hdr.magic != KLOG_MAGIC) {
        fs_close(fd);
        return;
    }

    if (hdr.entry_count == 0) {
        fs_close(fd);
        return;
    }

    hal_console_puts("[klog] Previous boot log:\n");

    /* Read and display entries.  Limit to what the file actually contains. */
    uint32_t to_read = hdr.entry_count;
    if (to_read > KLOG_MAX_ENTRIES)
        to_read = KLOG_MAX_ENTRIES;

    uint64_t offset = sizeof(klog_disk_header_t);
    for (uint32_t i = 0; i < to_read; i++) {
        klog_entry_t entry;
        rd = fs_read(fd, &entry, offset, sizeof(entry));
        if (rd < (int64_t)sizeof(entry))
            break;
        print_entry(&entry);
        offset += sizeof(entry);
    }

    hal_console_puts("[klog] End of previous boot log\n");
    fs_close(fd);
}

/* ---- Public API ---- */

void klog_init(void)
{
    memset(g_ring, 0, sizeof(g_ring));
    g_head = 0;
    g_count = 0;
    g_flushing = 0;
    g_initialized = 1;

    /* Try to replay previous boot log */
    replay_from_disk();
}

void klog(klog_level_t level, const char *fmt, ...)
{
    if (!g_initialized)
        return;

    /* Filter by minimum level */
    if (level < g_min_level)
        return;

    /* Fill the entry */
    klog_entry_t *e = &g_ring[g_head];
    e->timestamp_ns = hal_timer_ns();
    e->level = level;

    va_list ap;
    va_start(ap, fmt);
    klog_vformat(e->msg, KLOG_MSG_SIZE, fmt, ap);
    va_end(ap);

    /* Advance head (wraps around) */
    g_head = (g_head + 1) % KLOG_MAX_ENTRIES;
    if (g_count < KLOG_MAX_ENTRIES)
        g_count++;

    /* Auto-flush at 75% capacity (avoid re-entrancy if flush itself logs) */
    if (g_count >= KLOG_FLUSH_THRESHOLD && !g_flushing) {
        klog_flush();
    }
}

int klog_flush(void)
{
    if (!g_initialized || g_count == 0)
        return 0;

    g_flushing = 1;

    /* Open or create the log file */
    int fd = fs_open(KLOG_FILE_NAME);
    if (fd < 0) {
        /* Create with 1 reserved block (2 MiB — fits ~7600 entries) */
        if (fs_create(KLOG_FILE_NAME, 1) != 0) {
            g_flushing = 0;
            return -1;
        }
        fd = fs_open(KLOG_FILE_NAME);
        if (fd < 0) {
            g_flushing = 0;
            return -1;
        }
    }

    /* Write header */
    klog_disk_header_t hdr;
    hdr.magic = KLOG_MAGIC;
    hdr.entry_count = g_count;
    hdr.head = g_head;
    hdr._reserved = 0;

    int64_t wr = fs_write(fd, &hdr, 0, sizeof(hdr));
    if (wr < (int64_t)sizeof(hdr)) {
        fs_close(fd);
        g_flushing = 0;
        return -1;
    }

    /* Write entries in order (oldest first).
     * If the ring has wrapped, oldest is at g_head; otherwise at 0. */
    uint64_t offset = sizeof(hdr);
    uint32_t start;

    if (g_count < KLOG_MAX_ENTRIES) {
        /* Ring has not wrapped yet */
        start = 0;
    } else {
        /* Ring has wrapped: oldest entry is at g_head */
        start = g_head;
    }

    for (uint32_t i = 0; i < g_count; i++) {
        uint32_t idx = (start + i) % KLOG_MAX_ENTRIES;
        wr = fs_write(fd, &g_ring[idx], offset, sizeof(klog_entry_t));
        if (wr < (int64_t)sizeof(klog_entry_t))
            break;
        offset += sizeof(klog_entry_t);
    }

    fs_close(fd);
    g_flushing = 0;
    return 0;
}

void klog_dump(void)
{
    if (!g_initialized || g_count == 0) {
        hal_console_puts("[klog] Log is empty\n");
        return;
    }

    hal_console_printf("[klog] Dumping %u entries:\n", g_count);

    /* Print in chronological order (oldest first) */
    uint32_t start;
    if (g_count < KLOG_MAX_ENTRIES)
        start = 0;
    else
        start = g_head;

    for (uint32_t i = 0; i < g_count; i++) {
        uint32_t idx = (start + i) % KLOG_MAX_ENTRIES;
        print_entry(&g_ring[idx]);
    }

    hal_console_puts("[klog] End of log dump\n");
}

void klog_set_level(klog_level_t min_level)
{
    g_min_level = min_level;
}
