/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- Core GUI System
 * Framebuffer-based windowing primitives, event dispatch, and drawing API.
 * Dark theme matching the AlJefra website color palette.
 */

#ifndef ALJEFRA_GUI_H
#define ALJEFRA_GUI_H

#include "../hal/hal.h"
#include "../lib/string.h"

/* ======================================================================
 * Color palette  (matches website CSS custom properties)
 * ====================================================================== */
#define GUI_BG        0x0D1117   /* --bg-primary   */
#define GUI_BG2       0x161B22   /* --bg-secondary  */
#define GUI_BG3       0x21262D   /* --bg-tertiary   */
#define GUI_BORDER    0x30363D
#define GUI_TEXT      0xE6EDF3
#define GUI_TEXT2     0x8B949E   /* muted text      */
#define GUI_BLUE      0x58A6FF   /* accent blue     */
#define GUI_GREEN     0x3FB950
#define GUI_RED       0xF85149
#define GUI_YELLOW    0xD29922
#define GUI_WHITE     0xFFFFFF
#define GUI_BLACK     0x000000

/* Font metrics (same 8x16 bitmap font already in lfb.c) */
#define GUI_FONT_W    8
#define GUI_FONT_H    16

/* Maximum screen resolution supported */
#define GUI_MAX_W     3840
#define GUI_MAX_H     2160

/* ======================================================================
 * Core types
 * ====================================================================== */

/* Screen / framebuffer descriptor */
typedef struct {
    uint32_t *fb;           /* Direct pointer to pixel memory (32-bit XRGB) */
    int       w;            /* Width in pixels  */
    int       h;            /* Height in pixels */
    int       pitch;        /* Bytes per scanline */
} gui_screen_t;

/* Axis-aligned rectangle */
typedef struct {
    int x, y, w, h;
} gui_rect_t;

/* Mouse state */
typedef struct {
    int  x, y;              /* Absolute screen position */
    int  buttons;           /* Bitmask: bit0=left, bit1=right, bit2=middle */
    int  prev_buttons;      /* Previous frame button state (for click detect) */
    int  visible;           /* Non-zero if cursor should be drawn */
} gui_mouse_t;

/* ── Event system ── */

typedef enum {
    EVT_NONE = 0,
    EVT_KEY_DOWN,
    EVT_KEY_UP,
    EVT_MOUSE_MOVE,
    EVT_MOUSE_DOWN,
    EVT_MOUSE_UP,
    EVT_MOUSE_CLICK,        /* Down+Up in same spot */
    EVT_REDRAW,
    EVT_TIMER,
} gui_event_type_t;

/* Special key codes (OR'd with 0x100 to distinguish from ASCII) */
#define GUI_KEY_UP       0x100
#define GUI_KEY_DOWN     0x101
#define GUI_KEY_LEFT     0x102
#define GUI_KEY_RIGHT    0x103
#define GUI_KEY_HOME     0x104
#define GUI_KEY_END      0x105
#define GUI_KEY_PGUP     0x106
#define GUI_KEY_PGDN     0x107
#define GUI_KEY_DELETE   0x108
#define GUI_KEY_ESCAPE   0x109
#define GUI_KEY_TAB      0x10A
#define GUI_KEY_F1       0x110
#define GUI_KEY_F2       0x111
#define GUI_KEY_F3       0x112
#define GUI_KEY_F4       0x113

typedef struct {
    gui_event_type_t type;
    int              key;   /* ASCII or GUI_KEY_xxx */
    int              mx, my;/* Mouse position */
    int              mb;    /* Mouse button mask */
} gui_event_t;

/* Event queue */
#define GUI_EVT_QUEUE_SIZE  128

/* ── Clipping rectangle stack ── */
#define GUI_CLIP_STACK_MAX  16

/* ── Back-buffer for flicker-free rendering ── */
/* Allocated statically; sized for up to 1024x768x4 = ~3 MB */
#define GUI_BACKBUF_MAX_PIXELS  (1024 * 768)

/* ======================================================================
 * Public API
 * ====================================================================== */

/* Initialize the GUI system.
 * framebuffer  -- mapped linear framebuffer base (32-bit pixels)
 * width/height -- resolution
 * pitch        -- bytes per scanline (may be > width*4)                  */
void gui_init(uint32_t *framebuffer, int width, int height, int pitch);

/* Shut down GUI (restore text console if needed) */
void gui_shutdown(void);

/* ── Drawing primitives ── */

/* Fill a solid rectangle */
void gui_draw_rect(int x, int y, int w, int h, uint32_t color);

/* Draw a bordered rectangle with fill */
void gui_draw_rect_border(int x, int y, int w, int h,
                           uint32_t border, uint32_t fill);

/* Draw a single character at pixel (x,y) with foreground + background */
void gui_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);

/* Draw a single character with transparent background */
void gui_draw_char_transparent(int x, int y, char c, uint32_t fg);

/* Draw a null-terminated string (single line) */
void gui_draw_text(int x, int y, const char *text, uint32_t color);

/* Draw text with word-wrapping within max_w pixels.
 * Returns total height consumed (in pixels).                             */
int gui_draw_text_wrap(int x, int y, int max_w, const char *text,
                       uint32_t color);

/* Measure the height of wrapped text without drawing */
int gui_measure_text_wrap(int max_w, const char *text);

/* Draw a horizontal line */
void gui_draw_hline(int x, int y, int w, uint32_t color);

/* Draw a vertical line */
void gui_draw_vline(int x, int y, int h, uint32_t color);

/* Put a single pixel (respects clipping) */
void gui_putpixel(int x, int y, uint32_t color);

/* ── Mouse cursor ── */

/* Draw the 12x16 arrow cursor sprite at (x,y) */
void gui_draw_cursor(int x, int y);

/* Save the region under the cursor before drawing it */
void gui_save_under_cursor(int x, int y);

/* Restore the region from the save buffer */
void gui_restore_under_cursor(void);

/* ── Clipping ── */

/* Push a clipping rectangle (intersects with current clip) */
void gui_push_clip(gui_rect_t *r);

/* Pop the last clipping rectangle */
void gui_pop_clip(void);

/* Reset clipping to full screen */
void gui_reset_clip(void);

/* ── Double buffering ── */

/* Flip the back-buffer to the real framebuffer */
void gui_flip(void);

/* ── Event handling ── */

/* Push an event into the queue */
void gui_push_event(gui_event_t *evt);

/* Pop an event from the queue.  Returns false if empty. */
bool gui_poll_event(gui_event_t *evt);

/* ── Screen info ── */

/* Get a pointer to the global screen descriptor */
gui_screen_t *gui_get_screen(void);

/* Get a pointer to the global mouse state */
gui_mouse_t  *gui_get_mouse(void);

/* ── Utility ── */

/* strlen for GUI (avoids name clash with compiler builtins) */
int gui_strlen(const char *s);

/* strncpy for GUI */
void gui_strncpy(char *dst, const char *src, int max);

/* integer to decimal string */
void gui_itoa(int val, char *buf, int bufsz);

#endif /* ALJEFRA_GUI_H */
