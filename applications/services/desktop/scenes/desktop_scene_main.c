#include <furi.h>
#include <furi_hal.h>
#include <applications.h>
#include <assets_icons.h>
#include <loader/loader.h>

#include "../desktop_i.h"
#include "../views/desktop_events.h"
#include "../views/desktop_view_main.h"
#include "desktop_scene.h"

#define TAG "DesktopSrv"

static inline bool desktop_scene_main_check_none(const char* str) {
    return (str[1] == '\0' && str[0] == '?');
}

static void desktop_scene_main_open_app_or_profile(Desktop* desktop, FavoriteApp* application) {
    bool load_ok = false;
    if(strlen(application->name_or_path) > 0) {
        if(!desktop_scene_main_check_none(application->name_or_path)) {

            loader_start_detached_with_gui_error(desktop->loader, application->name_or_path, NULL);
        }
        load_ok = true;
    }

    if(!load_ok) {
        loader_start_detached_with_gui_error(
            desktop->loader, EXT_PATH("apps/Fox/ffb.fap"), NULL);
    }
}

static void desktop_scene_main_start_favorite(Desktop* desktop, FavoriteApp* application) {
    if(strlen(application->name_or_path) > 0) {
        if(!desktop_scene_main_check_none(application->name_or_path)) {
            loader_start_detached_with_gui_error(desktop->loader, application->name_or_path, NULL);
        }
    } else {
        loader_start_detached_with_gui_error(desktop->loader, LOADER_APPLICATIONS_NAME, NULL);
    }
}

void desktop_scene_main_callback(DesktopEvent event, void* context) {
    Desktop* desktop = (Desktop*)context;
    if(desktop->in_transition) return;
    view_dispatcher_send_custom_event(desktop->view_dispatcher, event);
}

void desktop_scene_main_on_enter(void* context) {
    Desktop* desktop = (Desktop*)context;

    if(desktop->alarm_ringing) {
        scene_manager_next_scene(desktop->scene_manager, DesktopSceneClockLock);
        return;
    }

    DesktopMainView* main_view = desktop->main_view;

    desktop_main_set_callback(main_view, desktop_scene_main_callback, desktop);

    view_dispatcher_switch_to_view(desktop->view_dispatcher, DesktopViewIdMain);
}

bool desktop_scene_main_on_event(void* context, SceneManagerEvent event) {
    Desktop* desktop = (Desktop*)context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        static bool s_first_load_defrag_checked = false;
        if(!s_first_load_defrag_checked) {
            s_first_load_defrag_checked = true;
            desktop_ram_defrag_if_wedged(desktop);
        }
    } else if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case DesktopMainEventOpenMenu: {
            Loader* loader = furi_record_open(RECORD_LOADER);
            loader_show_menu(loader);
            furi_record_close(RECORD_LOADER);
            consumed = true;
        } break;

        case DesktopMainEventLock:
            scene_manager_set_scene_state(desktop->scene_manager, DesktopSceneLockMenu, 0);
            desktop_lock(desktop);
            consumed = true;
            break;

        case DesktopMainEventOpenLockMenu:
            scene_manager_next_scene(desktop->scene_manager, DesktopSceneLockMenu);
            consumed = true;
            break;

        case DesktopMainEventOpenClockLock:

            scene_manager_next_scene(desktop->scene_manager, DesktopSceneClockLock);
            consumed = true;
            break;

        case DesktopMainEventOpenArchive:
            /* Down-short: now a configurable favorite, default FFB. Repurposes the
             * otherwise-dead RightLong slot (Right-long is hardcoded to wallpaper).
             * open_app_or_profile() falls back to ffb.fap when the slot is cleared. */
            desktop_scene_main_open_app_or_profile(
                desktop, &desktop->settings.favorite_apps[FavoriteAppRightLong]);
            consumed = true;
            break;

        case DesktopMainEventOpenPowerOff: {
            loader_start_detached_with_gui_error(desktop->loader, "Power", "off");
            consumed = true;
            break;
        }

        case DesktopMainEventOpenFavoriteLeftShort:
            desktop_scene_main_start_favorite(
                desktop, &desktop->settings.favorite_apps[FavoriteAppLeftShort]);
            consumed = true;
            break;
        case DesktopMainEventOpenFavoriteLeftLong:
            desktop_scene_main_start_favorite(
                desktop, &desktop->settings.favorite_apps[FavoriteAppLeftLong]);
            consumed = true;
            break;
        case DesktopMainEventOpenFavoriteRightShort:
            desktop_scene_main_open_app_or_profile(
                desktop, &desktop->settings.favorite_apps[FavoriteAppRightShort]);
            consumed = true;
            break;
        case DesktopMainEventCycleWallpaper:
            desktop_cycle_wallpaper(desktop);
            consumed = true;
            break;
        case DesktopMainEventOpenFavoriteOkLong:
            desktop_scene_main_start_favorite(
                desktop, &desktop->settings.favorite_apps[FavoriteAppOkLong]);
            consumed = true;
            break;

        case DesktopLockedEventUpdate:
            desktop_view_locked_update(desktop->locked_view);
            consumed = true;
            break;

        default:
            break;
        }
    }

    return consumed;
}

void desktop_scene_main_on_exit(void* context) {
    UNUSED(context);
}
