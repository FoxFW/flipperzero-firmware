#pragma once

#include "gui.h"

#include <furi.h>
#include <furi_hal_rtc.h>
#include <m-array.h>
#include <stdio.h>

#include "canvas.h"
#include "canvas_i.h"
#include "view_port.h"
#include "view_port_i.h"

#define GUI_DISPLAY_WIDTH  128
#define GUI_DISPLAY_HEIGHT 64

#define GUI_STATUS_BAR_X               0
#define GUI_STATUS_BAR_Y               0
#define GUI_STATUS_BAR_WIDTH           GUI_DISPLAY_WIDTH

#define GUI_STATUS_BAR_HEIGHT          13

#define GUI_STATUS_BAR_WORKAREA_HEIGHT 8

#define GUI_WINDOW_X      0
#define GUI_WINDOW_Y      GUI_STATUS_BAR_HEIGHT
#define GUI_WINDOW_WIDTH  GUI_DISPLAY_WIDTH
#define GUI_WINDOW_HEIGHT (GUI_DISPLAY_HEIGHT - GUI_WINDOW_Y)

#define GUI_THREAD_FLAG_DRAW  (1 << 0)
#define GUI_THREAD_FLAG_INPUT (1 << 1)
#define GUI_THREAD_FLAG_ALL   (GUI_THREAD_FLAG_DRAW | GUI_THREAD_FLAG_INPUT)

ARRAY_DEF(ViewPortArray, ViewPort*, M_PTR_OPLIST);

/* FoxFW desktop notification icon kinds. */
typedef enum {
    GuiDesktopNotifyIconNone,
    GuiDesktopNotifyIconInfo,    /* (i) in a circle */
    GuiDesktopNotifyIconWarning, /* (!) in a triangle */
} GuiDesktopNotifyIcon;

struct Gui {

    FuriThreadId thread_id;
    FuriMutex* mutex;

    bool lockdown;
    bool lockdown_inhibit;
    bool direct_draw;
    bool hide_status_bar;
    bool statusbar_show_icons;
    ViewPortArray_t layers[GuiLayerMAX];
    Canvas* canvas;

    FuriMessageQueue* input_queue;
    FuriPubSub* input_events;
    uint8_t ongoing_input;
    ViewPort* ongoing_input_view_port;

    bool screenshot_overlay_active;
    FuriTimer* screenshot_overlay_timer;

    /* FoxFW desktop notification overlay: a banner that slides over the status
     * bar, under the double border. Driven by gui_desktop_notification_set/_clear. */
    bool desktop_notify_active;
    int8_t desktop_notify_reveal; /* rows of the banner shown, 0..12 */
    GuiDesktopNotifyIcon desktop_notify_icon; /* which glyph to prepend */
    char desktop_notify_text[24];
};

ViewPort* gui_view_port_find_enabled(ViewPortArray_t array);

void gui_update(Gui* gui);

/* FoxFW: show/update the desktop notification overlay. `reveal` is how many rows
 * of the banner are visible (0 hidden .. 12 full status bar) -- step it to slide.
 * `info_icon` prepends a small (i) glyph. Thread-safe; triggers a redraw. */
void gui_desktop_notification_set(
    Gui* gui,
    const char* text,
    int8_t reveal,
    GuiDesktopNotifyIcon icon);
void gui_desktop_notification_clear(Gui* gui);

void gui_input_events_callback(const void* value, void* ctx);

size_t gui_active_view_port_count(Gui* gui, GuiLayer layer);

void gui_lock(Gui* gui);

void gui_unlock(Gui* gui);
