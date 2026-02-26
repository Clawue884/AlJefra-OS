/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- Widget Toolkit
 * Statically-allocated UI widgets: panels, labels, buttons, text inputs,
 * list views, chat message views, and scrollbars.
 *
 * All widgets run in kernel space with no malloc.  Every struct lives in a
 * fixed-size pool (WIDGET_MAX_TOTAL).  Panels hold up to WIDGET_MAX_CHILDREN
 * children.  A maximum of PANEL_MAX root panels can exist simultaneously.
 */

#ifndef ALJEFRA_WIDGETS_H
#define ALJEFRA_WIDGETS_H

#include "gui.h"

/* ======================================================================
 * Limits (no dynamic allocation)
 * ====================================================================== */
#define WIDGET_MAX_TOTAL       48      /* Total widget instances            */
#define WIDGET_MAX_CHILDREN    16      /* Children per panel                */
#define PANEL_MAX              8       /* Root panels on screen             */
#define WIDGET_TITLE_MAX       64
#define WIDGET_TEXT_MAX        256
#define WIDGET_BUTTON_TEXT_MAX 64
#define WIDGET_INPUT_MAX       512
#define WIDGET_PLACEHOLDER_MAX 64
#define WIDGET_LIST_MAX        64      /* Max items in a list view          */
#define WIDGET_LIST_ITEM_MAX   64      /* Max chars per list item           */
#define WIDGET_CHAT_MSG_MAX    32      /* Max messages in chat view         */
#define WIDGET_CHAT_TEXT_MAX   256     /* Max chars per chat message        */

/* ======================================================================
 * Widget types
 * ====================================================================== */
typedef enum {
    W_PANEL,
    W_LABEL,
    W_BUTTON,
    W_TEXTINPUT,
    W_LISTVIEW,
    W_CHATVIEW,
    W_SCROLLBAR,
} widget_type_t;

/* Text alignment */
#define ALIGN_LEFT    0
#define ALIGN_CENTER  1
#define ALIGN_RIGHT   2

/* Forward declaration */
typedef struct widget widget_t;

/* ======================================================================
 * Callback types
 * ====================================================================== */
typedef void (*widget_click_fn)(void);
typedef void (*widget_submit_fn)(const char *text);
typedef void (*widget_select_fn)(int index, const char *item);

/* ======================================================================
 * Widget-specific data structures
 * ====================================================================== */

/* -- Panel -- container that holds child widgets */
typedef struct {
    widget_t *children[WIDGET_MAX_CHILDREN];
    int       count;
    char      title[WIDGET_TITLE_MAX];
    int       show_title;      /* Draw title bar if non-zero            */
    uint32_t  title_bg;        /* Title bar background color            */
} panel_data_t;

/* -- Label -- static text display */
typedef struct {
    char      text[WIDGET_TEXT_MAX];
    int       align;           /* ALIGN_LEFT / ALIGN_CENTER / ALIGN_RIGHT */
    int       wrap;            /* Word-wrap if non-zero                 */
} label_data_t;

/* -- Button -- clickable with action callback */
typedef struct {
    char      text[WIDGET_BUTTON_TEXT_MAX];
    int       hover;           /* Mouse is over this button             */
    int       pressed;         /* Mouse button is held down             */
    widget_click_fn action;    /* Click callback                        */
} button_data_t;

/* -- Text Input -- single-line editable field */
typedef struct {
    char      text[WIDGET_INPUT_MAX];
    int       cursor;          /* Cursor position in text               */
    int       scroll;          /* First visible character index         */
    int       len;             /* Current text length                   */
    char      placeholder[WIDGET_PLACEHOLDER_MAX];
    int       cursor_visible;  /* Blinking cursor state                 */
    uint32_t  cursor_timer;    /* Frame counter for blinking            */
    widget_submit_fn on_submit;/* Submit callback (on Enter)            */
} textinput_data_t;

/* -- List View -- scrollable list of selectable items */
typedef struct {
    char      items[WIDGET_LIST_MAX][WIDGET_LIST_ITEM_MAX];
    int       count;
    int       selected;        /* Currently selected index (-1 = none)  */
    int       scroll;          /* First visible item index              */
    int       hover;           /* Item under mouse (-1 = none)          */
    widget_select_fn on_select;
} listview_data_t;

/* -- Chat View -- scrollable message list (user + AI messages) */
typedef struct {
    struct {
        char  text[WIDGET_CHAT_TEXT_MAX];
        int   is_user;         /* 1 = user message, 0 = AI response    */
    } messages[WIDGET_CHAT_MSG_MAX];
    int       msg_count;
    int       scroll;          /* Pixel offset (0 = top for auto-scroll) */
    int       content_h;       /* Total computed height of all messages */
    int       auto_scroll;     /* Auto-scroll to bottom on new message */
} chatview_data_t;

/* -- Scrollbar -- vertical scrollbar */
typedef struct {
    int       total;           /* Total content size (items or pixels)  */
    int       visible;         /* Visible area size                     */
    int       position;        /* Current scroll position               */
    int       dragging;        /* Currently being dragged               */
    int       drag_offset;     /* Offset within the thumb on drag start */
    widget_t *target;          /* Widget this scrollbar controls        */
} scrollbar_data_t;

/* ======================================================================
 * Widget structure
 * ====================================================================== */
struct widget {
    widget_type_t  type;
    gui_rect_t     bounds;     /* Position and size relative to parent  */
    int            visible;
    int            focused;    /* Has keyboard focus                    */
    int            enabled;    /* Accepts input                         */
    uint32_t       bg_color;
    uint32_t       fg_color;
    uint32_t       border_color;

    /* Callbacks */
    void         (*on_click)(widget_t *w, int x, int y);
    void         (*on_key)(widget_t *w, int key);
    void         (*draw)(widget_t *w);

    /* Widget-specific data (union to save memory) */
    union {
        panel_data_t      panel;
        label_data_t      label;
        button_data_t     button;
        textinput_data_t  input;
        listview_data_t   list;
        chatview_data_t   chat;
        scrollbar_data_t  scrollbar;
    } data;

    /* Parent widget (NULL for root panels) */
    widget_t      *parent;

    /* Absolute screen position (computed during draw) */
    int            abs_x, abs_y;
};

/* ======================================================================
 * Widget constructors -- allocate from the static pool
 * ====================================================================== */

widget_t *widget_panel(int x, int y, int w, int h, const char *title);
widget_t *widget_label(int x, int y, int w, int h, const char *text);
widget_t *widget_button(int x, int y, int w, int h, const char *text,
                         widget_click_fn action);
widget_t *widget_textinput(int x, int y, int w, int h,
                            const char *placeholder,
                            widget_submit_fn on_submit);
widget_t *widget_listview(int x, int y, int w, int h,
                           widget_select_fn on_select);
widget_t *widget_chatview(int x, int y, int w, int h);
widget_t *widget_scrollbar(int x, int y, int w, int h, widget_t *target);

/* ======================================================================
 * Panel operations
 * ====================================================================== */

bool panel_add_child(widget_t *panel, widget_t *child);

/* ======================================================================
 * Label operations
 * ====================================================================== */

void label_set_text(widget_t *w, const char *text);

/* ======================================================================
 * List view operations
 * ====================================================================== */

bool listview_add(widget_t *w, const char *item);
void listview_clear(widget_t *w);
void listview_set_selected(widget_t *w, int index);

/* ======================================================================
 * Chat view operations
 * ====================================================================== */

void chatview_add(widget_t *w, const char *text, int is_user);
void chatview_scroll_to_bottom(widget_t *w);
void chatview_clear(widget_t *w);

/* ======================================================================
 * Text input operations
 * ====================================================================== */

void textinput_clear(widget_t *w);
void textinput_set_text(widget_t *w, const char *text);
const char *textinput_get_text(widget_t *w);

/* ======================================================================
 * Widget tree dispatch
 * ====================================================================== */

/* Draw a widget tree (recursively draws children for panels). */
void widget_draw(widget_t *w);

/* Dispatch a mouse event to the widget tree.
 * Returns the widget that handled it, or NULL. */
widget_t *widget_mouse_event(widget_t *root, gui_event_t *evt);

/* Dispatch a keyboard event to the currently focused widget.
 * Returns true if handled. */
bool widget_key_event(widget_t *focused, gui_event_t *evt);

/* Find the widget at screen coordinates (x, y).
 * Searches recursively through panels. */
widget_t *widget_hit_test(widget_t *root, int x, int y);

/* Set focus to a specific widget (unfocuses previous). */
void widget_set_focus(widget_t *w);

/* Get the currently focused widget. */
widget_t *widget_get_focus(void);

/* ======================================================================
 * Widget pool management
 * ====================================================================== */

/* Reset the widget allocator (free all widgets). */
void widget_pool_reset(void);

/* Get total number of allocated widgets. */
int widget_pool_count(void);

#endif /* ALJEFRA_WIDGETS_H */
