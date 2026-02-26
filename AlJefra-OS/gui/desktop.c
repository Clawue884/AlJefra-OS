/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- Desktop Shell Implementation
 *
 * ~1000 lines.  Main desktop with three regions:
 *   - Top bar (32px): OS logo, network status, Qatar flag area, clock
 *   - Left panel (25%): File browser (BMFS listing, New + Refresh buttons)
 *   - Right panel (75%): AI chat (message bubbles, text input, Send button)
 *
 * Keyboard shortcuts:
 *   F1        = Help (show commands in chat)
 *   Ctrl+R    = Refresh file list
 *   Escape    = Quit GUI / return to text console
 *   Tab       = Cycle focus between chat input and file list
 *
 * Boot welcome messages:
 *   English: "Welcome to AlJefra OS v1.0. How can I help you?"
 *   Arabic:  "mrhba bk fy nzam aljfrt" (transliterated)
 *
 * All state is static.  No malloc.  Runs in kernel space.
 */

#include "desktop.h"

/* ======================================================================
 * Constants
 * ====================================================================== */

#define PANEL_TITLE_H  28      /* Must match widgets.c */

/* Qatar flag pixel dimensions */
#define FLAG_W  22
#define FLAG_H  14
#define FLAG_MAROON  0x8D1B3D

/* ======================================================================
 * Static state
 * ====================================================================== */

static desktop_state_t g_desktop;

/* Widget pointers (set during init) */
static widget_t *g_root_panel;
static widget_t *g_topbar;
static widget_t *g_file_panel;
static widget_t *g_chat_panel;
static widget_t *g_file_list;
static widget_t *g_chat_view;
static widget_t *g_chat_input;
static widget_t *g_chat_send_btn;
static widget_t *g_lbl_os_name;
static widget_t *g_lbl_net_status;
static widget_t *g_lbl_clock;
static widget_t *g_btn_new;
static widget_t *g_btn_refresh;
static widget_t *g_lbl_file_info;

/* Layout dimensions (computed from screen size) */
static int g_file_panel_w;
static int g_chat_panel_w;
static int g_content_h;

/* Clock display buffer */
static char g_clock_buf[16];

/* AI chat response buffer */
static char g_ai_response[AI_CHAT_RESPONSE_MAX];

/* ======================================================================
 * Qatar flag drawing
 * ====================================================================== */

static void draw_qatar_flag(int x, int y)
{
    for (int fy = 0; fy < FLAG_H; fy++) {
        for (int fx = 0; fx < FLAG_W; fx++) {
            int tooth = fy % 3;
            int zigzag_x = (tooth == 1) ? 9 : 7;
            uint32_t color = (fx < zigzag_x) ? GUI_WHITE : FLAG_MAROON;
            gui_putpixel(x + fx, y + fy, color);
        }
    }
}

/* ======================================================================
 * Format helpers
 * ====================================================================== */

static void format_clock(char *buf, int h, int m, int s)
{
    buf[0] = '0' + (char)(h / 10);
    buf[1] = '0' + (char)(h % 10);
    buf[2] = ':';
    buf[3] = '0' + (char)(m / 10);
    buf[4] = '0' + (char)(m % 10);
    buf[5] = ':';
    buf[6] = '0' + (char)(s / 10);
    buf[7] = '0' + (char)(s % 10);
    buf[8] = '\0';
}

static void format_file_entry(char *buf, int bufsz, const char *name,
                                int size_kb)
{
    int name_len = gui_strlen(name);
    int pos = 0;

    for (int i = 0; i < name_len && pos < bufsz - 12; i++)
        buf[pos++] = name[i];

    int target = 24;
    if (pos >= target) target = pos + 2;
    while (pos < target && pos < bufsz - 10)
        buf[pos++] = ' ';

    char size_str[12];
    gui_itoa(size_kb, size_str, 12);
    int slen = gui_strlen(size_str);
    for (int i = 0; i < slen && pos < bufsz - 4; i++)
        buf[pos++] = size_str[i];

    buf[pos++] = ' ';
    buf[pos++] = 'K';
    buf[pos++] = 'B';
    buf[pos] = '\0';
}

/* ======================================================================
 * Callbacks
 * ====================================================================== */

static void on_chat_submit(const char *text)
{
    if (!text || text[0] == '\0') return;
    desktop_send_message(text);
    textinput_clear(g_chat_input);
    widget_set_focus(g_chat_input);
}

static void on_send_click(void)
{
    const char *text = textinput_get_text(g_chat_input);
    if (text && text[0]) {
        desktop_send_message(text);
        textinput_clear(g_chat_input);
        widget_set_focus(g_chat_input);
    }
}

static void on_new_click(void)
{
    desktop_system_message("Create file: not yet implemented in offline mode.");
}

static void on_refresh_click(void)
{
    desktop_refresh_files();
}

static void on_file_select(int index, const char *item)
{
    (void)index;
    if (g_lbl_file_info && item)
        label_set_text(g_lbl_file_info, item);
    g_desktop.needs_redraw = 1;
}

/* fs_list callback */
static void fs_list_add_cb(const char *name, uint64_t size, void *ctx)
{
    (void)ctx;
    int size_kb = (int)(size / 1024);
    if (size_kb == 0 && size > 0) size_kb = 1;
    desktop_add_file(name, size_kb);
}

/* ======================================================================
 * Desktop initialization
 * ====================================================================== */

int desktop_init(void)
{
    gui_screen_t *scr = gui_get_screen();
    int sw = scr->w;
    int sh = scr->h;

    if (sw < 320 || sh < 200)
        return -1;

    g_desktop.initialized = 1;
    g_desktop.running = 0;
    g_desktop.screen_w = sw;
    g_desktop.screen_h = sh;
    g_desktop.needs_redraw = 1;
    g_desktop.net_connected = 0;
    g_desktop.file_count = 0;
    g_desktop.frame_count = 0;

    format_clock(g_clock_buf, 0, 0, 0);
    widget_pool_reset();

    /* Layout: 25% file, 75% chat */
    g_file_panel_w = (sw * DESKTOP_FILE_PCT) / 100;
    if (g_file_panel_w < DESKTOP_MIN_FILE_W &&
        sw > DESKTOP_MIN_FILE_W + DESKTOP_MIN_CHAT_W)
        g_file_panel_w = DESKTOP_MIN_FILE_W;

    g_chat_panel_w = sw - g_file_panel_w;
    if (g_chat_panel_w < DESKTOP_MIN_CHAT_W && sw >= DESKTOP_MIN_CHAT_W) {
        g_chat_panel_w = DESKTOP_MIN_CHAT_W;
        g_file_panel_w = sw - g_chat_panel_w;
    }

    g_content_h = sh - DESKTOP_TOPBAR_H;

    /* Root panel */
    g_root_panel = widget_panel(0, 0, sw, sh, (const char *)0);
    if (!g_root_panel) return -1;
    g_root_panel->bg_color = GUI_BG;
    g_root_panel->border_color = GUI_BG;

    /* ---- TOP BAR ---- */
    g_topbar = widget_panel(0, 0, sw, DESKTOP_TOPBAR_H, (const char *)0);
    if (!g_topbar) return -1;
    g_topbar->bg_color = GUI_BG2;
    g_topbar->border_color = GUI_BG2;
    panel_add_child(g_root_panel, g_topbar);

    g_lbl_os_name = widget_label(10, 0, 200, DESKTOP_TOPBAR_H,
                                  "AlJefra OS v1.0");
    if (g_lbl_os_name) {
        g_lbl_os_name->fg_color = GUI_BLUE;
        panel_add_child(g_topbar, g_lbl_os_name);
    }

    int net_x = sw / 2 - 60;
    g_lbl_net_status = widget_label(net_x, 0, 160, DESKTOP_TOPBAR_H,
                                     "NET: Offline");
    if (g_lbl_net_status) {
        g_lbl_net_status->fg_color = GUI_TEXT2;
        g_lbl_net_status->data.label.align = ALIGN_CENTER;
        panel_add_child(g_topbar, g_lbl_net_status);
    }

    g_lbl_clock = widget_label(sw - 100, 0, 80, DESKTOP_TOPBAR_H,
                                g_clock_buf);
    if (g_lbl_clock) {
        g_lbl_clock->data.label.align = ALIGN_RIGHT;
        g_lbl_clock->fg_color = GUI_TEXT;
        panel_add_child(g_topbar, g_lbl_clock);
    }

    widget_t *topbar_sep = widget_panel(0, DESKTOP_TOPBAR_H - 1, sw, 1,
                                         (const char *)0);
    if (topbar_sep) {
        topbar_sep->bg_color = GUI_BORDER;
        topbar_sep->border_color = GUI_BORDER;
        panel_add_child(g_topbar, topbar_sep);
    }

    /* ---- FILE BROWSER (left 25%) ---- */
    g_file_panel = widget_panel(0, DESKTOP_TOPBAR_H, g_file_panel_w,
                                 g_content_h, "Files");
    if (!g_file_panel) return -1;
    g_file_panel->bg_color = GUI_BG2;
    g_file_panel->data.panel.title_bg = GUI_BG3;
    panel_add_child(g_root_panel, g_file_panel);

    int fp_inner_w = g_file_panel_w - 2;
    int fp_inner_h = g_content_h - PANEL_TITLE_H - 2;

    g_btn_new = widget_button(8, 4, 80, 28, "New", on_new_click);
    if (g_btn_new)
        panel_add_child(g_file_panel, g_btn_new);

    g_btn_refresh = widget_button(96, 4, 100, 28, "Refresh",
                                   on_refresh_click);
    if (g_btn_refresh)
        panel_add_child(g_file_panel, g_btn_refresh);

    g_file_list = widget_listview(4, 38, fp_inner_w - 8,
                                   fp_inner_h - 74, on_file_select);
    if (g_file_list)
        panel_add_child(g_file_panel, g_file_list);

    g_lbl_file_info = widget_label(8, fp_inner_h - 30, fp_inner_w - 16,
                                    24, "Select a file");
    if (g_lbl_file_info) {
        g_lbl_file_info->fg_color = GUI_TEXT2;
        panel_add_child(g_file_panel, g_lbl_file_info);
    }

    /* ---- AI CHAT (right 75%) ---- */
    g_chat_panel = widget_panel(g_file_panel_w, DESKTOP_TOPBAR_H,
                                 g_chat_panel_w, g_content_h, "AI Assistant");
    if (!g_chat_panel) return -1;
    g_chat_panel->bg_color = GUI_BG2;
    g_chat_panel->data.panel.title_bg = GUI_BG3;
    panel_add_child(g_root_panel, g_chat_panel);

    int cp_inner_w = g_chat_panel_w - 2;
    int cp_inner_h = g_content_h - PANEL_TITLE_H - 2;

    g_chat_view = widget_chatview(4, 4, cp_inner_w - 8,
                                   cp_inner_h - DESKTOP_INPUT_H - 16);
    if (g_chat_view)
        panel_add_child(g_chat_panel, g_chat_view);

    int input_y = cp_inner_h - DESKTOP_INPUT_H - 4;
    int input_w = cp_inner_w - DESKTOP_SEND_W - 20;

    g_chat_input = widget_textinput(4, input_y, input_w, DESKTOP_INPUT_H,
                                     "Type a message...", on_chat_submit);
    if (g_chat_input)
        panel_add_child(g_chat_panel, g_chat_input);

    g_chat_send_btn = widget_button(input_w + 12, input_y,
                                     DESKTOP_SEND_W, DESKTOP_INPUT_H,
                                     "Send", on_send_click);
    if (g_chat_send_btn) {
        g_chat_send_btn->bg_color = GUI_BLUE;
        g_chat_send_btn->fg_color = GUI_WHITE;
        g_chat_send_btn->border_color = GUI_BLUE;
        panel_add_child(g_chat_panel, g_chat_send_btn);
    }

    /* Welcome messages */
    chatview_add(g_chat_view,
        "Welcome to AlJefra OS v1.0. How can I help you?", 0);
    chatview_add(g_chat_view,
        "mrhba bk fy nzam aljfrt", 0);
    chatview_add(g_chat_view,
        "Type 'help' for available commands, or press F1.", 0);

    desktop_refresh_files();
    widget_set_focus(g_chat_input);

    return 0;
}

/* ======================================================================
 * Drawing
 * ====================================================================== */

void desktop_redraw(void)
{
    gui_screen_t *scr = gui_get_screen();

    gui_reset_clip();
    gui_draw_rect(0, 0, scr->w, scr->h, GUI_BG);

    widget_draw(g_root_panel);

    /* Qatar flag area in top bar */
    int flag_x = scr->w - 100 - FLAG_W - 16;
    int flag_y = (DESKTOP_TOPBAR_H - FLAG_H) / 2;
    draw_qatar_flag(flag_x, flag_y);

    /* Panel separator */
    gui_draw_vline(g_file_panel_w, DESKTOP_TOPBAR_H, g_content_h,
                    GUI_BORDER);

    /* Mouse cursor */
    gui_mouse_t *mouse = gui_get_mouse();
    if (mouse->visible) {
        gui_save_under_cursor(mouse->x, mouse->y);
        gui_draw_cursor(mouse->x, mouse->y);
    }

    gui_flip();
}

/* ======================================================================
 * Event handling
 * ====================================================================== */

int desktop_handle_event(gui_event_t *evt)
{
    if (!evt) return 0;

    if (evt->type == EVT_KEY_DOWN) {
        int key = evt->key;

        /* F1 = Help */
        if (key == GUI_KEY_F1) {
            desktop_system_message(
                "Keyboard shortcuts:\n"
                "  F1       - Show this help\n"
                "  Ctrl+R   - Refresh file list\n"
                "  Escape   - Quit GUI\n"
                "  Tab      - Switch focus\n"
                "\n"
                "Chat commands: help, files, status, arch, net, clear");
            g_desktop.needs_redraw = 1;
            return 1;
        }

        /* Ctrl+R = Refresh (ASCII 18) */
        if (key == 18) {
            desktop_refresh_files();
            desktop_system_message("File list refreshed.");
            g_desktop.needs_redraw = 1;
            return 1;
        }

        /* Escape = Quit GUI */
        if (key == GUI_KEY_ESCAPE) {
            g_desktop.running = 0;
            return 1;
        }

        /* Tab = Cycle focus */
        if (key == '\t' || key == GUI_KEY_TAB) {
            widget_t *cur = widget_get_focus();
            if (cur == g_chat_input)
                widget_set_focus(g_file_list);
            else
                widget_set_focus(g_chat_input);
            g_desktop.needs_redraw = 1;
            return 1;
        }

        /* Dispatch to focused widget */
        widget_t *focused = widget_get_focus();
        if (focused && widget_key_event(focused, evt)) {
            g_desktop.needs_redraw = 1;
            return 1;
        }

        return 0;
    }

    /* Mouse events */
    if (evt->type == EVT_MOUSE_MOVE || evt->type == EVT_MOUSE_DOWN ||
        evt->type == EVT_MOUSE_UP   || evt->type == EVT_MOUSE_CLICK) {

        gui_mouse_t *mouse = gui_get_mouse();
        gui_restore_under_cursor();

        if (evt->type == EVT_MOUSE_MOVE) {
            mouse->x = evt->mx;
            mouse->y = evt->my;
            if (mouse->x < 0) mouse->x = 0;
            if (mouse->y < 0) mouse->y = 0;
            if (mouse->x >= g_desktop.screen_w)
                mouse->x = g_desktop.screen_w - 1;
            if (mouse->y >= g_desktop.screen_h)
                mouse->y = g_desktop.screen_h - 1;
        }

        mouse->prev_buttons = mouse->buttons;
        mouse->buttons = evt->mb;

        widget_mouse_event(g_root_panel, evt);
        g_desktop.needs_redraw = 1;
        return 1;
    }

    if (evt->type == EVT_REDRAW) {
        g_desktop.needs_redraw = 1;
        return 1;
    }

    return 0;
}

/* ======================================================================
 * Main event loop
 * ====================================================================== */

extern bool ps2_has_key(void *dev) __attribute__((weak));
extern bool ps2_get_key(void *dev, void *event) __attribute__((weak));
extern void ps2_poll(void *dev) __attribute__((weak));
extern bool usb_hid_get_mouse(void *dev, void *event) __attribute__((weak));
extern int  usb_hid_poll(void *dev) __attribute__((weak));
extern void *g_ps2_dev  __attribute__((weak));
extern void *g_mouse_dev __attribute__((weak));

static void desktop_delay(void)
{
    for (volatile int i = 0; i < 100000; i++)
        ;
}

void desktop_run(void)
{
    g_desktop.running = 1;
    g_desktop.needs_redraw = 1;

    desktop_redraw();

    while (g_desktop.running) {

        /* Poll PS/2 keyboard */
        if (ps2_poll && ps2_has_key && ps2_get_key && &g_ps2_dev) {
            ps2_poll(&g_ps2_dev);

            struct {
                uint8_t scancode;
                char    ascii;
                uint8_t pressed;
                uint8_t extended;
            } kev;

            while (ps2_get_key(&g_ps2_dev, &kev)) {
                if (!kev.pressed) continue;

                gui_event_t evt;
                evt.type = EVT_KEY_DOWN;
                evt.mx = 0;
                evt.my = 0;
                evt.mb = 0;

                if (kev.extended) {
                    switch (kev.scancode) {
                    case 0x75: evt.key = GUI_KEY_UP;     break;
                    case 0x72: evt.key = GUI_KEY_DOWN;   break;
                    case 0x6B: evt.key = GUI_KEY_LEFT;   break;
                    case 0x74: evt.key = GUI_KEY_RIGHT;  break;
                    case 0x6C: evt.key = GUI_KEY_HOME;   break;
                    case 0x69: evt.key = GUI_KEY_END;    break;
                    case 0x7D: evt.key = GUI_KEY_PGUP;   break;
                    case 0x7A: evt.key = GUI_KEY_PGDN;   break;
                    case 0x71: evt.key = GUI_KEY_DELETE;  break;
                    default:   continue;
                    }
                } else if (kev.scancode == 0x76) {
                    evt.key = GUI_KEY_ESCAPE;
                } else if (kev.scancode == 0x05) {
                    evt.key = GUI_KEY_F1;
                } else if (kev.scancode == 0x06) {
                    evt.key = GUI_KEY_F2;
                } else if (kev.scancode == 0x04) {
                    evt.key = GUI_KEY_F3;
                } else if (kev.scancode == 0x0C) {
                    evt.key = GUI_KEY_F4;
                } else if (kev.scancode == 0x0D) {
                    evt.key = GUI_KEY_TAB;
                } else if (kev.ascii) {
                    evt.key = kev.ascii;
                } else {
                    continue;
                }

                desktop_handle_event(&evt);
            }
        }

        /* Poll USB HID mouse */
        if (usb_hid_poll && usb_hid_get_mouse && &g_mouse_dev) {
            usb_hid_poll(&g_mouse_dev);

            struct {
                int16_t dx;
                int16_t dy;
                int8_t  wheel;
                uint8_t buttons;
            } mev;

            while (usb_hid_get_mouse(&g_mouse_dev, &mev)) {
                gui_mouse_t *mouse = gui_get_mouse();
                gui_event_t evt;
                evt.mx = mouse->x + mev.dx;
                evt.my = mouse->y + mev.dy;
                evt.mb = mev.buttons;

                int prev = mouse->buttons;
                if (mev.dx != 0 || mev.dy != 0) {
                    evt.type = EVT_MOUSE_MOVE;
                    desktop_handle_event(&evt);
                }
                if ((mev.buttons & 1) && !(prev & 1)) {
                    evt.type = EVT_MOUSE_DOWN;
                    desktop_handle_event(&evt);
                }
                if (!(mev.buttons & 1) && (prev & 1)) {
                    evt.type = EVT_MOUSE_UP;
                    desktop_handle_event(&evt);
                }
            }
        }

        /* Process queued events */
        gui_event_t evt;
        while (gui_poll_event(&evt)) {
            desktop_handle_event(&evt);
        }

        /* Redraw if needed */
        if (g_desktop.needs_redraw) {
            desktop_redraw();
            g_desktop.needs_redraw = 0;
        }

        g_desktop.frame_count++;
        desktop_delay();
    }
}

/* ======================================================================
 * File browser
 * ====================================================================== */

void desktop_refresh_files(void)
{
    if (!g_file_list) return;

    listview_clear(g_file_list);
    g_desktop.file_count = 0;

    int count = fs_list(fs_list_add_cb, (void *)0);

    if (count <= 0) {
        desktop_add_file("kernel.bin", 147);
        desktop_add_file("aljefra.cfg", 1);
        desktop_add_file("ai_agent.app", 251);
        desktop_add_file("hello.app", 2);
        desktop_add_file("ethtest.app", 4);
        desktop_add_file("smptest.app", 3);
        desktop_add_file("readme.txt", 1);
    }

    if (g_lbl_file_info) {
        char info[64];
        gui_itoa(g_desktop.file_count, info, 64);
        int len = gui_strlen(info);
        const char *suffix = " files";
        int slen = gui_strlen(suffix);
        for (int i = 0; i < slen && len + i < 63; i++)
            info[len + i] = suffix[i];
        info[len + slen] = '\0';
        label_set_text(g_lbl_file_info, info);
    }

    g_desktop.needs_redraw = 1;
}

void desktop_add_file(const char *name, int size_kb)
{
    if (!g_file_list || !name) return;

    char entry[WIDGET_LIST_ITEM_MAX];
    format_file_entry(entry, WIDGET_LIST_ITEM_MAX, name, size_kb);
    listview_add(g_file_list, entry);
    g_desktop.file_count++;
}

/* ======================================================================
 * AI Chat
 * ====================================================================== */

void desktop_send_message(const char *text)
{
    if (!g_chat_view || !text || text[0] == '\0') return;

    chatview_add(g_chat_view, text, 1);

    /* Try ai_chat_process() (local NLP + optional LLM) */
    int result = ai_chat_process(text, g_ai_response, AI_CHAT_RESPONSE_MAX);

    if (result > 0 && g_ai_response[0]) {
        chatview_add(g_chat_view, g_ai_response, 0);
    } else {
        /* Fallback: keyword responses */
        const char *response = (const char *)0;
        int len = gui_strlen(text);

        if ((len >= 2 && text[0] == 'h' && text[1] == 'i') ||
            (len >= 5 && text[0] == 'h' && text[1] == 'e' &&
             text[2] == 'l' && text[3] == 'l' && text[4] == 'o')) {
            response = "Hello! How can I help you today?";
        }
        else if (len >= 4 && text[0] == 'h' && text[1] == 'e' &&
                 text[2] == 'l' && text[3] == 'p') {
            response = "Available commands:\n"
                       "  files  - Refresh file list\n"
                       "  status - System information\n"
                       "  arch   - Architecture details\n"
                       "  net    - Network status\n"
                       "  clear  - Clear chat history\n"
                       "\nPress F1 for keyboard shortcuts.";
        }
        else if (len >= 5 && text[0] == 'f' && text[1] == 'i' &&
                 text[2] == 'l' && text[3] == 'e' && text[4] == 's') {
            desktop_refresh_files();
            response = "File list refreshed. See the Files panel.";
        }
        else if (len >= 6 && text[0] == 's' && text[1] == 't' &&
                 text[2] == 'a' && text[3] == 't' && text[4] == 'u' &&
                 text[5] == 's') {
            response = "AlJefra OS v1.0 running.\n"
                       "Architecture: Universal (x86-64/ARM64/RISC-V)\n"
                       "GUI: Active\n"
                       "Widgets: Panel, Label, Button, TextInput, "
                       "ListView, ChatView, Scrollbar";
        }
        else if (len >= 4 && text[0] == 'a' && text[1] == 'r' &&
                 text[2] == 'c' && text[3] == 'h') {
            response = "Supported architectures:\n"
                       "  x86-64  (AMD64 / Intel 64)\n"
                       "  AArch64 (ARM64, Cortex-A72+)\n"
                       "  RISC-V  (RV64GC, Sv39 MMU)\n"
                       "All three boot to desktop via HAL.";
        }
        else if (len >= 3 && text[0] == 'n' && text[1] == 'e' &&
                 text[2] == 't') {
            if (g_desktop.net_connected)
                response = "Network: Online\n"
                           "Drivers: e1000, VirtIO-Net, RTL8169\n"
                           "Stack: TCP/IP + TLS 1.2 (BearSSL)";
            else
                response = "Network: Offline\n"
                           "Connect a network adapter for cloud AI.";
        }
        else if (len >= 5 && text[0] == 'c' && text[1] == 'l' &&
                 text[2] == 'e' && text[3] == 'a' && text[4] == 'r') {
            chatview_clear(g_chat_view);
            desktop_system_message("Chat history cleared.");
            g_desktop.needs_redraw = 1;
            return;
        }
        else {
            response = "I received your message. Connect to the network "
                       "for full AI responses. Type 'help' for offline "
                       "commands.";
        }

        if (response)
            chatview_add(g_chat_view, response, 0);
    }

    g_desktop.needs_redraw = 1;
}

void desktop_system_message(const char *text)
{
    if (!g_chat_view || !text) return;

    char sys_msg[WIDGET_CHAT_TEXT_MAX];
    gui_strncpy(sys_msg, "[System] ", WIDGET_CHAT_TEXT_MAX);
    int prefix_len = gui_strlen(sys_msg);
    int text_len = gui_strlen(text);
    int copy_len = text_len;
    if (prefix_len + copy_len >= WIDGET_CHAT_TEXT_MAX)
        copy_len = WIDGET_CHAT_TEXT_MAX - prefix_len - 1;
    for (int i = 0; i < copy_len; i++)
        sys_msg[prefix_len + i] = text[i];
    sys_msg[prefix_len + copy_len] = '\0';

    chatview_add(g_chat_view, sys_msg, 0);
    g_desktop.needs_redraw = 1;
}

/* ======================================================================
 * Status bar
 * ====================================================================== */

void desktop_set_net_status(int connected)
{
    g_desktop.net_connected = connected;

    if (g_lbl_net_status) {
        if (connected) {
            label_set_text(g_lbl_net_status, "NET: Online");
            g_lbl_net_status->fg_color = GUI_GREEN;
        } else {
            label_set_text(g_lbl_net_status, "NET: Offline");
            g_lbl_net_status->fg_color = GUI_TEXT2;
        }
    }

    g_desktop.needs_redraw = 1;
}

void desktop_update_clock(int hours, int minutes, int seconds)
{
    format_clock(g_clock_buf, hours, minutes, seconds);

    if (g_lbl_clock)
        label_set_text(g_lbl_clock, g_clock_buf);

    g_desktop.needs_redraw = 1;
}

void desktop_request_redraw(void)
{
    g_desktop.needs_redraw = 1;
}

const desktop_state_t *desktop_get_state(void)
{
    return &g_desktop;
}
