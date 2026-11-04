#pragma once

#include "desktop.h"
#include "desktop_settings.h"

#include "views/desktop_view_pin_timeout.h"
#include "views/desktop_view_pin_input.h"
#include "views/desktop_view_locked.h"
#include "views/desktop_view_main.h"
#include "views/desktop_view_lock_menu.h"
#include "views/desktop_view_debug.h"
#include "views/desktop_view_slideshow.h"
#include "views/desktop_view_clock_lock.h"

#include <gui/gui.h>
#include <gui/view_stack.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/popup.h>
#include <gui/scene_manager.h>

#include <loader/loader.h>
#include <notification/notification_app.h>

#define STATUS_BAR_Y_SHIFT 13

typedef enum {
    DesktopViewIdMain,
    DesktopViewIdLockMenu,
    DesktopViewIdLocked,
    DesktopViewIdDebug,
    DesktopViewIdPopup,
    DesktopViewIdPinInput,
    DesktopViewIdPinTimeout,
    DesktopViewIdSlideshow,
    DesktopViewIdClockLock,
    DesktopViewIdTotal,
} DesktopViewId;

typedef struct {
    uint8_t hour;
    uint8_t minute;
    bool format_12;
} DesktopClock;

struct Desktop {
    FuriThread* scene_thread;

    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;

    Popup* popup;
    DesktopLockMenuView* lock_menu;
    DesktopDebugView* debug_view;
    DesktopViewLocked* locked_view;
    DesktopMainView* main_view;
    DesktopViewPinTimeout* pin_timeout_view;
    DesktopSlideshowView* slideshow_view;
    DesktopViewPinInput* pin_input_view;
    DesktopClockLockView* clock_lock_view;

    ViewStack* main_view_stack;
    ViewStack* locked_view_stack;

    ViewPort* lock_icon_viewport;
    ViewPort* clock_viewport;
    ViewPort* stealth_mode_icon_viewport;
    ViewPort* no_sd_viewport;

    ViewPort* wifi_icon_viewport;
    FuriThread* wifi_recheck_thread;
    bool wifi_connected;
    bool cc1101_connected;
    bool pending_slideshow;

    View* wallpaper_view;
    uint8_t* wallpaper_data;
    uint8_t* wallpaper_buf[2];
    uint8_t* wallpaper_scratch;
    FuriMutex* wallpaper_mutex;


    Loader* loader;
    Storage* storage;
    NotificationApp* notification;

    FuriPubSub* status_pubsub;
    FuriPubSub* input_events_pubsub;
    FuriPubSubSubscription* input_events_subscription;

    FuriTimer* auto_lock_timer;
    FuriTimer* update_clock_timer;

    DesktopClock clock;
    DesktopSettings settings;

    bool in_transition;
    bool app_running;
    bool locked;

    FuriTimer* alarm_check_timer;
    uint16_t alarm_last_checked_stamp;

    bool alarm_ringing;
    uint8_t alarm_ringing_index;
    bool on_clock_lock_scene;
    bool clock_lock_backlight_manually_off;

    FuriTimer* ram_watchdog_timer;
    FuriTimer* fox_import_timer;
    FuriTimer* fox_setup_timer;
    FuriTimer* banner_timer;
    bool ram_watchdog_tripped;

    bool usb_msc_active;
};

void desktop_lock(Desktop* desktop);
void desktop_unlock(Desktop* desktop);
void desktop_set_stealth_mode_state(Desktop* desktop, bool enabled);

void desktop_ram_defrag_if_wedged(Desktop* desktop);
void desktop_alarm_dismiss(Desktop* desktop);
void desktop_cycle_wallpaper(Desktop* desktop);
