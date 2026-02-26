/* SPDX-License-Identifier: MIT */
/* AlJefra OS — Linear Framebuffer Display Driver
 * Pixel-level framebuffer access with bitmap font text console.
 * Architecture-independent; works with any bootloader-provided LFB.
 */

#ifndef ALJEFRA_DRV_LFB_H
#define ALJEFRA_DRV_LFB_H

#include "../../hal/hal.h"

/* ── Font dimensions (8x16 bitmap font) ── */
#define LFB_FONT_WIDTH    8
#define LFB_FONT_HEIGHT   16

/* ── Color helpers (32-bit ARGB) ── */
#define LFB_COLOR_BLACK   0x00000000
#define LFB_COLOR_WHITE   0x00FFFFFF
#define LFB_COLOR_RED     0x00FF0000
#define LFB_COLOR_GREEN   0x0000FF00
#define LFB_COLOR_BLUE    0x000000FF
#define LFB_COLOR_YELLOW  0x00FFFF00
#define LFB_COLOR_CYAN    0x0000FFFF
#define LFB_COLOR_GRAY    0x00808080

#define LFB_RGB(r,g,b)   (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

/* ── Framebuffer info (from bootloader) ── */
typedef struct {
    volatile void *base;       /* Framebuffer base address (mapped) */
    uint64_t       phys_base;  /* Physical base address */
    uint32_t       width;      /* Resolution width in pixels */
    uint32_t       height;     /* Resolution height in pixels */
    uint32_t       pitch;      /* Bytes per scanline */
    uint8_t        bpp;        /* Bits per pixel (16, 24, or 32) */
} lfb_info_t;

/* ── Text console state (overlaid on framebuffer) ── */
typedef struct {
    lfb_info_t     fb;
    uint32_t       cols;           /* Text columns */
    uint32_t       rows;           /* Text rows */
    uint32_t       cursor_x;      /* Current column (0-based) */
    uint32_t       cursor_y;      /* Current row (0-based) */
    uint32_t       fg_color;      /* Foreground color */
    uint32_t       bg_color;      /* Background color */
    bool           initialized;
} lfb_console_t;

/* ── Public API ── */

/* Initialize the framebuffer driver with info from bootloader */
hal_status_t lfb_init(lfb_console_t *con, lfb_info_t *fb);

/* ── Pixel operations ── */

/* Put a single pixel at (x, y) */
void lfb_putpixel(lfb_console_t *con, uint32_t x, uint32_t y, uint32_t color);

/* Fill a rectangle with a solid color */
void lfb_fill_rect(lfb_console_t *con, uint32_t x, uint32_t y,
                   uint32_t w, uint32_t h, uint32_t color);

/* Clear the entire screen with a color */
void lfb_clear(lfb_console_t *con, uint32_t color);

/* ── Text operations (use built-in 8x16 font) ── */

/* Draw a single character at pixel position (px, py) */
void lfb_draw_char(lfb_console_t *con, uint32_t px, uint32_t py,
                   char c, uint32_t fg, uint32_t bg);

/* Print a character to the text console (handles \n, \r, \t, scrolling) */
void lfb_putc(lfb_console_t *con, char c);

/* Print a null-terminated string */
void lfb_puts(lfb_console_t *con, const char *s);

/* Print a string with explicit length */
void lfb_write(lfb_console_t *con, const char *s, uint64_t len);

/* Set foreground/background colors */
void lfb_set_colors(lfb_console_t *con, uint32_t fg, uint32_t bg);

/* Get framebuffer info */
void lfb_get_info(lfb_console_t *con, lfb_info_t *info);

/* Get text console dimensions in characters */
void lfb_get_text_size(lfb_console_t *con, uint32_t *cols, uint32_t *rows);

#endif /* ALJEFRA_DRV_LFB_H */
