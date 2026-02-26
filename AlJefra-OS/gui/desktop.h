/* SPDX-License-Identifier: MIT */
/* AlJefra OS -- Desktop Shell Interface
 *
 * The desktop shell provides the user-facing GUI environment:
 *   - Top bar (32px): OS logo left, network status center, time right
 *   - File browser panel (left 25% of screen)
 *   - AI Chat panel (right 75% of screen)
 *   - Keyboard shortcuts: F1=help, Ctrl+R=refresh files, Escape=quit
 *
 * All state is static.  No malloc.  Call desktop_init() once after
 * gui_init() and then desktop_run() to enter the event loop.
 */

#ifndef ALJEFRA_DESKTOP_H
#define ALJEFRA_DESKTOP_H

#include "widgets.h"
#include "../kernel/ai_chat.h"
#include "../kernel/fs.h"

/* ======================================================================
 * Desktop layout constants
 * ====================================================================== */

#define DESKTOP_TOPBAR_H      32      /* Top bar height in pixels         */
#define DESKTOP_FILE_PCT      25      /* File panel: 25% of screen width  */
#define DESKTOP_CHAT_PCT      75      /* Chat panel: 75% of screen width  */
#define DESKTOP_INPUT_H       36      /* Chat input bar height            */
#define DESKTOP_SEND_W        80      /* Send button width                */
#define DESKTOP_MIN_FILE_W    200     /* Minimum file panel width         */
#define DESKTOP_MIN_CHAT_W    400     /* Minimum chat panel width         */

/* ======================================================================
 * File browser limits
 * ====================================================================== */
#define DESKTOP_MAX_FILES     256
#define DESKTOP_FILENAME_MAX  64

/* ======================================================================
 * Desktop state
 * ====================================================================== */

typedef struct {
    int  initialized;           /* desktop_init() has been called       */
    int  running;               /* Main loop is active                  */
    int  screen_w;              /* Screen width in pixels               */
    int  screen_h;              /* Screen height in pixels              */
    int  needs_redraw;          /* Full redraw needed                   */
    int  net_connected;         /* Network status flag                  */
    int  file_count;            /* Number of files in the listing       */
    uint32_t frame_count;       /* Frame counter for timing             */
} desktop_state_t;

/* ======================================================================
 * Public API
 * ====================================================================== */

/* Initialize the desktop shell.
 * Must be called after gui_init().
 * Sets up all panels, widgets, and loads the initial file list.
 * Returns 0 on success, -1 on failure. */
int desktop_init(void);

/* Enter the desktop main event loop.
 * Polls events, dispatches to widgets, redraws the screen.
 * Returns when the user exits (Escape or shutdown). */
void desktop_run(void);

/* Process a single event.
 * Returns 1 if the event was consumed, 0 otherwise. */
int desktop_handle_event(gui_event_t *evt);

/* Full-screen redraw.
 * Draws top bar, file panel, chat panel, and mouse cursor. */
void desktop_redraw(void);

/* -- File browser -- */

/* Refresh the file list from BMFS. */
void desktop_refresh_files(void);

/* Add a file entry to the browser. */
void desktop_add_file(const char *name, int size_kb);

/* -- AI Chat -- */

/* Send a user message and display the AI response. */
void desktop_send_message(const char *text);

/* Display a system message in the chat. */
void desktop_system_message(const char *text);

/* -- Status bar -- */

/* Update the network connection status indicator. */
void desktop_set_net_status(int connected);

/* Update the clock display. */
void desktop_update_clock(int hours, int minutes, int seconds);

/* Request a full redraw on the next frame. */
void desktop_request_redraw(void);

/* Get a pointer to the desktop state (read-only). */
const desktop_state_t *desktop_get_state(void);

#endif /* ALJEFRA_DESKTOP_H */
