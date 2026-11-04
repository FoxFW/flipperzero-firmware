#include <gui/gui.h>
#include <core/kernel.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/menu.h>
#include <gui/modules/submenu.h>
#include <assets_icons.h>
#include <applications.h>
#include <archive/helpers/archive_favorites.h>
#include <storage/storage.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <toolbox/api_lock.h>

#include "loader.h"
#include "loader_menu.h"
#include "loader_main_menu_layout.h"
#include "../desktop/desktop_settings.h"

#define TAG "LoaderMenu"

typedef enum {
    LoaderMenuThreadFlagShow = (1 << 0),
    LoaderMenuThreadFlagExit = (1 << 1),
} LoaderMenuThreadFlag;

#define LoaderMenuThreadFlagAny (LoaderMenuThreadFlagShow | LoaderMenuThreadFlagExit)

struct LoaderMenu {
    FuriThread* thread;
    void (*closed_cb)(void*);
    void* context;
    FuriApiLock built_lock;
    ViewDispatcher* view_dispatcher;
    const char* settings_item;
};

static const char* loader_menu_settings_return_item = NULL;

static const char loader_menu_fox_settings_marker = 0;

static int32_t loader_menu_thread(void* p);

LoaderMenu* loader_menu_alloc(void (*closed_cb)(void*), void* context) {
    LoaderMenu* loader_menu = malloc(sizeof(LoaderMenu));
    loader_menu->closed_cb = closed_cb;
    loader_menu->context = context;
    loader_menu->built_lock = api_lock_alloc_locked();
    loader_menu->thread = furi_thread_alloc_ex(TAG, 1024, loader_menu_thread, loader_menu);
    furi_thread_start(loader_menu->thread);
    api_lock_wait_unlock(loader_menu->built_lock);
    return loader_menu;
}

void loader_menu_free(LoaderMenu* loader_menu) {
    furi_assert(loader_menu);
    furi_thread_flags_set(furi_thread_get_id(loader_menu->thread), LoaderMenuThreadFlagExit);
    furi_thread_join(loader_menu->thread);
    furi_kernel_lock();
    furi_thread_free(loader_menu->thread);
    api_lock_free(loader_menu->built_lock);
    free(loader_menu);
    furi_kernel_unlock();
}

void loader_menu_show(LoaderMenu* loader_menu) {
    furi_assert(loader_menu);
    furi_thread_flags_set(furi_thread_get_id(loader_menu->thread), LoaderMenuThreadFlagShow);
}

void loader_menu_show_settings(LoaderMenu* loader_menu, const char* settings_item) {
    furi_assert(loader_menu);
    loader_menu->settings_item = settings_item;
    furi_thread_flags_set(furi_thread_get_id(loader_menu->thread), LoaderMenuThreadFlagShow);
}

const char* loader_menu_take_settings_return(void) {
    const char* settings_item = loader_menu_settings_return_item;
    loader_menu_settings_return_item = NULL;
    return settings_item;
}

bool loader_menu_settings_return_pending(void) {
    return loader_menu_settings_return_item != NULL;
}

typedef enum {
    LoaderMenuViewPrimary,
    LoaderMenuViewSettings,
} LoaderMenuView;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Menu* primary_menu;
    Submenu* settings_menu;

    /* The whole apps-menu layout, SD-backed, kept for the menu's lifetime so
     * every identifier/label pointer handed to the menu stays valid. This is the
     * only resident copy; change detection uses a cheap fingerprint of the file
     * rather than a second ~8 KB layout buffer (keeps the largest contiguous
     * free heap block as big as possible). */
    MainMenuLayout layout;

    /* Fingerprint of the layout file as last loaded, for allocation-free change
     * detection on the refresh hot path. */
    uint32_t layout_fp_hash;
    uint32_t layout_fp_len;
    bool layout_fp_absent;

    /* Positions of the Fox Settings / Settings items in the built menu, so a
     * settings sub-app return can restore the cursor. */
    size_t fox_menu_index;
    size_t settings_menu_index;

    LoaderMenu* loader_menu;
    Loader* loader;
    FuriPubSubSubscription* loader_sub;
} LoaderMenuApp;

#define LoaderMenuCustomEventRefreshLayout 0

static bool loader_menu_start(LoaderMenu* menu, const char* name) {
    Loader* loader = furi_record_open(RECORD_LOADER);
    LoaderStatus status = loader_start_with_gui_error(loader, name, NULL);
    furi_record_close(RECORD_LOADER);
    if(status == LoaderStatusOk && menu->view_dispatcher) {
        view_dispatcher_stop(menu->view_dispatcher);
    }
    return status == LoaderStatusOk;
}

/* Unified launcher for every app entry in the layout. The menu item carries
 * the entry's identifier pointer (an internal/external app name, or a .fap
 * path) as its index; the loader resolves a name or a path the same way. The
 * identifier lives in app->layout for the menu's whole lifetime. */
static void loader_menu_launch_callback(void* context, uint32_t index) {
    LoaderMenu* menu = context;
    const char* name_or_path = (const char*)index;
    loader_menu_start(menu, name_or_path);
}

static void loader_menu_applications_callback(void* context, uint32_t index) {
    UNUSED(index);
    LoaderMenu* menu = context;
    const char* name = LOADER_APPLICATIONS_NAME;
    loader_menu_start(menu, name);
}

static void
    loader_menu_settings_menu_callback(void* context, InputType input_type, uint32_t index) {
    LoaderMenu* menu = context;
    if(input_type == InputTypeShort) {
        loader_menu_settings_return_item = (const char*)index;
        if(!loader_menu_start(menu, (const char*)index)) {
            loader_menu_settings_return_item = NULL;
        }
    } else if(input_type == InputTypeLong) {
        archive_favorites_handle_setting_pin_unpin((const char*)index, NULL);
    }
}

static void loader_menu_fox_settings_callback(void* context, uint32_t index) {
    UNUSED(index);
    LoaderMenu* menu = context;
    loader_menu_settings_return_item = &loader_menu_fox_settings_marker;
    if(!loader_menu_start(menu, "/ext/apps/Fox/desktop_settings.fap")) {
        loader_menu_settings_return_item = NULL;
    }
}

static void loader_menu_switch_to_settings(void* context, uint32_t index) {
    UNUSED(index);
    LoaderMenuApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, LoaderMenuViewSettings);
}

static uint32_t loader_menu_switch_to_primary(void* context) {
    UNUSED(context);
    return LoaderMenuViewPrimary;
}

static uint32_t loader_menu_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

/* Cosmetic icon for a layout entry; launching never depends on it. */
static const Icon* loader_menu_layout_entry_icon(const MainMenuLayoutEntry* e) {
    switch(e->kind) {
    case MainMenuEntryKindApplications:
        return &A_Plugins_14;
    case MainMenuEntryKindFoxSettings:
    case MainMenuEntryKindSettings:
        return &A_Settings_14;
    case MainMenuEntryKindInternal:
        for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
            if(strcmp(FLIPPER_APPS[i].name, e->identifier) == 0) return FLIPPER_APPS[i].icon;
        }
        return &A_Plugins_14;
    case MainMenuEntryKindExternal:
        for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
            if(strcmp(FLIPPER_EXTERNAL_APPS[i].name, e->identifier) == 0)
                return FLIPPER_EXTERNAL_APPS[i].icon;
        }
        return &A_Star_14;
    case MainMenuEntryKindUser:
    default:
        return &A_Star_14;
    }
}

/* Internal/external entries whose app no longer exists in this build (e.g.
 * removed by a firmware update) are dropped so no dead item is shown. User
 * paths and specials are always kept. */
static bool loader_menu_layout_entry_known(const MainMenuLayoutEntry* e) {
    if(e->kind == MainMenuEntryKindInternal) {
        for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
            if(strcmp(FLIPPER_APPS[i].name, e->identifier) == 0) return true;
        }
        return false;
    }
    if(e->kind == MainMenuEntryKindExternal) {
        for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
            if(strcmp(FLIPPER_EXTERNAL_APPS[i].name, e->identifier) == 0) return true;
        }
        return false;
    }
    return true;
}

static void loader_menu_add_layout_entry(
    LoaderMenuApp* app,
    LoaderMenu* menu,
    MainMenuLayoutEntry* e,
    size_t* position) {
    /* label points into app->layout (persistent for the menu's lifetime), or to
     * a static literal fallback (also persistent) if the stored label is empty. */
    const char* label = e->label;
    if(label[0] == '\0') {
        switch(e->kind) {
        case MainMenuEntryKindApplications:
            label = MAIN_MENU_APPLICATIONS_NAME;
            break;
        case MainMenuEntryKindFoxSettings:
            label = "Fox Settings";
            break;
        case MainMenuEntryKindSettings:
            label = "Settings";
            break;
        default:
            label = e->identifier;
            break;
        }
    }
    const Icon* icon = loader_menu_layout_entry_icon(e);

    switch(e->kind) {
    case MainMenuEntryKindApplications:
        menu_add_item(
            app->primary_menu, label, icon, 0, loader_menu_applications_callback, (void*)menu);
        break;
    case MainMenuEntryKindFoxSettings:
        app->fox_menu_index = *position;
        menu_add_item(
            app->primary_menu, label, icon, 0, loader_menu_fox_settings_callback, (void*)menu);
        break;
    case MainMenuEntryKindSettings:
        app->settings_menu_index = *position;
        menu_add_item(
            app->primary_menu, label, icon, 0, loader_menu_switch_to_settings, app);
        break;
    case MainMenuEntryKindInternal:
    case MainMenuEntryKindExternal:
    case MainMenuEntryKindUser:
    default:
        menu_add_item(
            app->primary_menu,
            label,
            icon,
            (uint32_t)e->identifier,
            loader_menu_launch_callback,
            (void*)menu);
        break;
    }

    (*position)++;
}

static void loader_menu_build_menu(LoaderMenuApp* app, LoaderMenu* menu) {
    size_t position = 0;
    app->fox_menu_index = 0;
    app->settings_menu_index = 0;

    bool has_apps = false;
    bool has_subghz = false;
    bool has_settings = false;

    for(uint8_t k = 0; k < app->layout.count; k++) {
        MainMenuLayoutEntry* e = &app->layout.entries[k];
        bool protect = main_menu_layout_entry_is_protected(e);
        if(!e->enabled && !protect) continue;
        if(!loader_menu_layout_entry_known(e)) continue;

        if(e->kind == MainMenuEntryKindApplications) has_apps = true;
        if(e->kind == MainMenuEntryKindInternal &&
           strcmp(e->identifier, MAIN_MENU_SUBGHZ_NAME) == 0)
            has_subghz = true;
        if(e->kind == MainMenuEntryKindSettings) has_settings = true;

        loader_menu_add_layout_entry(app, menu, e, &position);
    }

    /* Belt-and-suspenders: "Apps", "Sub-GHz" and "Settings" are the only path to
     * those apps and MUST always be present, even if the layout file omitted
     * them. (A line that merely disabled them was force-enabled at parse time, so
     * this only handles a fully missing line.) String literals are static
     * storage, so the labels they hand the menu stay valid. */
    if(!has_apps) {
        menu_add_item(
            app->primary_menu,
            MAIN_MENU_APPLICATIONS_NAME,
            &A_Plugins_14,
            0,
            loader_menu_applications_callback,
            (void*)menu);
        position++;
    }
    if(!has_subghz) {
        const Icon* subghz_icon = &A_Plugins_14;
        for(size_t i = 0; i < FLIPPER_APPS_COUNT; i++) {
            if(strcmp(FLIPPER_APPS[i].name, MAIN_MENU_SUBGHZ_NAME) == 0) {
                subghz_icon = FLIPPER_APPS[i].icon;
                break;
            }
        }
        menu_add_item(
            app->primary_menu,
            MAIN_MENU_SUBGHZ_NAME,
            subghz_icon,
            (uint32_t)MAIN_MENU_SUBGHZ_NAME,
            loader_menu_launch_callback,
            (void*)menu);
        position++;
    }
    if(!has_settings) {
        app->settings_menu_index = position;
        menu_add_item(
            app->primary_menu, "Settings", &A_Settings_14, 0, loader_menu_switch_to_settings, app);
        position++;
    }
}

static void loader_menu_build_submenu(LoaderMenuApp* app, LoaderMenu* loader_menu) {
    for(size_t i = 0; i < FLIPPER_EXTSETTINGS_APPS_COUNT; i++) {
        submenu_add_item_ex(
            app->settings_menu,
            FLIPPER_EXTSETTINGS_APPS[i].name,
            (uint32_t)FLIPPER_EXTSETTINGS_APPS[i].name,
            loader_menu_settings_menu_callback,
            loader_menu);
    }
    for(size_t i = 0; i < FLIPPER_SETTINGS_APPS_COUNT; i++) {
        submenu_add_item_ex(
            app->settings_menu,
            FLIPPER_SETTINGS_APPS[i].name,
            (uint32_t)FLIPPER_SETTINGS_APPS[i].name,
            loader_menu_settings_menu_callback,
            loader_menu);
    }
}

/* Record the fingerprint of the layout file as it currently sits on disk, so a
 * later refresh can detect a change without parsing or holding a second copy. */
static void loader_menu_store_fingerprint(LoaderMenuApp* app) {
    uint32_t h = 0;
    uint32_t l = 0;
    MainMenuLayoutFpStatus fp = main_menu_layout_fingerprint(&h, &l);
    if(fp == MainMenuLayoutFpOk) {
        app->layout_fp_absent = false;
        app->layout_fp_hash = h;
        app->layout_fp_len = l;
    } else if(fp == MainMenuLayoutFpAbsent) {
        app->layout_fp_absent = true;
        app->layout_fp_hash = 0;
        app->layout_fp_len = 0;
    } else {
        /* transient read error: force a recheck on the next refresh */
        app->layout_fp_absent = false;
        app->layout_fp_hash = 0;
        app->layout_fp_len = 0;
    }
}

/* Picks up a layout edit saved by the Fox Settings "Main Menu Apps" page on the
 * next menu show, the same way pins used to refresh. The menu is built once at
 * boot and kept, so on a change we menu_reset + rebuild in place rather than
 * reallocating the menu. Change detection streams a cheap fingerprint of the
 * file (no parse, no allocation); the full layout is only re-read when the
 * fingerprint actually differs. */
static void loader_menu_refresh_layout_if_changed(LoaderMenuApp* app) {
    uint32_t hash = 0;
    uint32_t len = 0;
    MainMenuLayoutFpStatus fp = main_menu_layout_fingerprint(&hash, &len);

    bool changed;
    if(fp == MainMenuLayoutFpError) {
        /* Transient read error: keep the current menu and the stored fingerprint
         * so the next refresh retries. */
        return;
    } else if(fp == MainMenuLayoutFpAbsent) {
        changed = !app->layout_fp_absent;
    } else {
        changed = app->layout_fp_absent || hash != app->layout_fp_hash ||
                  len != app->layout_fp_len;
    }
    if(!changed) return;

    /* On a transient read failure the load returns false and leaves the file
     * intact; keep the current menu rather than collapsing it. */
    if(!main_menu_layout_load(&app->layout)) return;

    loader_menu_store_fingerprint(app);
    menu_reset(app->primary_menu);
    loader_menu_build_menu(app, app->loader_menu);
}

static bool loader_menu_custom_event_callback(void* context, uint32_t event) {
    LoaderMenuApp* app = context;
    if(event == LoaderMenuCustomEventRefreshLayout) {
        loader_menu_refresh_layout_if_changed(app);
    }
    return true;
}

static void loader_menu_loader_pubsub_callback(const void* message, void* context) {
    LoaderMenuApp* app = context;
    const LoaderEvent* event = message;
    if(event->type == LoaderEventTypeApplicationStopped) {
        view_dispatcher_send_custom_event(app->view_dispatcher, LoaderMenuCustomEventRefreshLayout);
    }
}

static LoaderMenuApp* loader_menu_app_alloc(LoaderMenu* loader_menu) {
    LoaderMenuApp* app = malloc(sizeof(LoaderMenuApp));
    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    app->primary_menu = menu_alloc();
    app->settings_menu = submenu_alloc();
    app->loader_menu = loader_menu;
    app->fox_menu_index = 0;
    app->settings_menu_index = 0;
    app->layout_fp_hash = 0;
    app->layout_fp_len = 0;
    app->layout_fp_absent = false;
    main_menu_layout_load(&app->layout);
    loader_menu_store_fingerprint(app);

    app->loader = furi_record_open(RECORD_LOADER);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, loader_menu_custom_event_callback);
    app->loader_sub = furi_pubsub_subscribe(
        loader_get_pubsub(app->loader), loader_menu_loader_pubsub_callback, app);

    DesktopSettings* settings = malloc(sizeof(DesktopSettings));
    if(settings) {
        desktop_settings_load(settings);
        menu_set_theme(app->primary_menu, settings->menu_theme);
        free(settings);
    }

    loader_menu_build_menu(app, loader_menu);
    loader_menu_build_submenu(app, loader_menu);

    View* primary_view = menu_get_view(app->primary_menu);
    view_set_context(primary_view, app->primary_menu);
    view_set_previous_callback(primary_view, loader_menu_exit);
    view_dispatcher_add_view(app->view_dispatcher, LoaderMenuViewPrimary, primary_view);

    View* settings_view = submenu_get_view(app->settings_menu);
    view_set_context(settings_view, app->settings_menu);
    view_set_previous_callback(settings_view, loader_menu_switch_to_primary);
    view_dispatcher_add_view(app->view_dispatcher, LoaderMenuViewSettings, settings_view);
    view_dispatcher_switch_to_view(app->view_dispatcher, LoaderMenuViewPrimary);

    return app;
}

static void loader_menu_app_free(LoaderMenuApp* app) {
    furi_pubsub_unsubscribe(loader_get_pubsub(app->loader), app->loader_sub);
    furi_record_close(RECORD_LOADER);

    view_dispatcher_remove_view(app->view_dispatcher, LoaderMenuViewPrimary);
    view_dispatcher_remove_view(app->view_dispatcher, LoaderMenuViewSettings);
    view_dispatcher_free(app->view_dispatcher);


    menu_free(app->primary_menu);
    submenu_free(app->settings_menu);
    furi_record_close(RECORD_GUI);
    free(app);
}

static int32_t loader_menu_thread(void* p) {
    LoaderMenu* loader_menu = p;
    furi_assert(loader_menu);


    LoaderMenuApp* app = loader_menu_app_alloc(loader_menu);
    loader_menu->view_dispatcher = app->view_dispatcher;


    api_lock_unlock(loader_menu->built_lock);

    while(true) {
        uint32_t flags =
            furi_thread_flags_wait(LoaderMenuThreadFlagAny, FuriFlagWaitAny, FuriWaitForever);
        if(flags & LoaderMenuThreadFlagExit) {
            break;
        }


        loader_menu_refresh_layout_if_changed(app);
        const char* settings_item = loader_menu->settings_item;
        loader_menu->settings_item = NULL;
        if(settings_item == &loader_menu_fox_settings_marker) {
            menu_set_selected_item(app->primary_menu, app->fox_menu_index);
            view_dispatcher_switch_to_view(app->view_dispatcher, LoaderMenuViewPrimary);
        } else if(settings_item) {
            menu_set_selected_item(app->primary_menu, app->settings_menu_index);
            submenu_set_selected_item(app->settings_menu, (uint32_t)settings_item);
            view_dispatcher_switch_to_view(app->view_dispatcher, LoaderMenuViewSettings);
        } else {
            view_dispatcher_switch_to_view(app->view_dispatcher, LoaderMenuViewPrimary);
        }
        view_dispatcher_attach_to_gui(
            app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);


        view_dispatcher_run(app->view_dispatcher);


        view_dispatcher_detach_from_gui(app->view_dispatcher);


        if(loader_menu->closed_cb) {
            loader_menu->closed_cb(loader_menu->context);
        }
    }

    loader_menu->view_dispatcher = NULL;
    loader_menu_app_free(app);


    return 0;
}
