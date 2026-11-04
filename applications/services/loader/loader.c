#include "loader.h"
#include "loader_i.h"
#include <applications.h>
#include <storage/storage.h>
#include <furi_hal.h>
#include <assets_icons.h>

#include <dialogs/dialogs.h>
#include <toolbox/path.h>
#include <flipper_application/flipper_application.h>
#include <loader/firmware_api/firmware_api.h>
#include <furi/core/memmgr.h>
#include <furi/core/memmgr_heap.h>
#include <furi/core/kernel.h>
#include <toolbox/heap_alloc_guard.h>
#include <lib/subghz/devices/devices.h>
#include <stdio.h>
#include <gui/icon.h>

#define TAG "Loader"

#define LOADER_MAGIC_THREAD_VALUE 0xDEADBEEF

#define LOADER_LOAD_WATCHDOG_TIMEOUT_MS 7000
#define LOADER_LOAD_WARN_THRESHOLD_MS 2000
#define LOADER_STILL_LOADING_WAIT_MS 5000
#define LOADER_STILL_LOADING_SPIN_INTERVAL_MS 50
#define LOADER_STILL_LOADING_BAR_H 16

typedef struct {
    uint8_t spin_frame;
    bool buttons_visible;
    bool show_wait_button;
    bool focus_left;
} LoaderStillLoadingModel;

static const int8_t loader_still_loading_spin_dx[8] = {0, 8, 11, 8, 0, -8, -11, -8};
static const int8_t loader_still_loading_spin_dy[8] = {-11, -8, 0, 8, 11, 8, 0, -8};

static void loader_still_loading_draw_spinner(Canvas* canvas, uint8_t frame) {
    canvas_set_color(canvas, ColorBlack);
    for(uint8_t i = 0; i < 8; i++) {
        int32_t x = 64 + loader_still_loading_spin_dx[i];
        int32_t y = 32 + loader_still_loading_spin_dy[i];
        uint8_t age = (uint8_t)((8u + frame - i) % 8u);
        if(age == 0) {
            canvas_draw_disc(canvas, x, y, 3);
        } else if(age == 1) {
            canvas_draw_disc(canvas, x, y, 2);
        } else if(age == 2) {
            canvas_draw_disc(canvas, x, y, 1);
        } else {
            canvas_draw_dot(canvas, x, y);
        }
    }
}

static void loader_still_loading_draw_two_buttons(
    Canvas* canvas, bool focus_left, const char* left_label, const char* right_label) {
    int32_t bar_y = 64 - LOADER_STILL_LOADING_BAR_H;
    int32_t btn_gap = 4;
    int32_t btn_w = (128 - btn_gap * 3) / 2;
    int32_t left_x = btn_gap;
    int32_t right_x = btn_gap * 2 + btn_w;

    canvas_set_color(canvas, ColorBlack);
    if(focus_left) {
        canvas_draw_rbox(canvas, left_x, bar_y, btn_w, LOADER_STILL_LOADING_BAR_H, 3);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str_aligned(
            canvas,
            left_x + btn_w / 2,
            bar_y + LOADER_STILL_LOADING_BAR_H / 2,
            AlignCenter,
            AlignCenter,
            left_label);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rframe(canvas, right_x, bar_y, btn_w, LOADER_STILL_LOADING_BAR_H, 3);
        canvas_draw_str_aligned(
            canvas,
            right_x + btn_w / 2,
            bar_y + LOADER_STILL_LOADING_BAR_H / 2,
            AlignCenter,
            AlignCenter,
            right_label);
    } else {
        canvas_draw_rframe(canvas, left_x, bar_y, btn_w, LOADER_STILL_LOADING_BAR_H, 3);
        canvas_draw_str_aligned(
            canvas,
            left_x + btn_w / 2,
            bar_y + LOADER_STILL_LOADING_BAR_H / 2,
            AlignCenter,
            AlignCenter,
            left_label);
        canvas_draw_rbox(canvas, right_x, bar_y, btn_w, LOADER_STILL_LOADING_BAR_H, 3);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str_aligned(
            canvas,
            right_x + btn_w / 2,
            bar_y + LOADER_STILL_LOADING_BAR_H / 2,
            AlignCenter,
            AlignCenter,
            right_label);
        canvas_set_color(canvas, ColorBlack);
    }
}

static void loader_still_loading_draw_one_button(Canvas* canvas, const char* label) {
    int32_t bar_y = 64 - LOADER_STILL_LOADING_BAR_H;
    const Icon* icon = &I_ButtonCenter_7x7;
    int32_t icon_w = icon_get_width(icon);
    int32_t icon_h = icon_get_height(icon);
    int32_t icon_gap = 3;
    int32_t pad_x = 10;
    int32_t content_w = icon_w + icon_gap + (int32_t)canvas_string_width(canvas, label);
    int32_t btn_w = content_w + pad_x * 2;
    int32_t x = (128 - btn_w) / 2;

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rbox(canvas, x, bar_y, btn_w, LOADER_STILL_LOADING_BAR_H, 3);
    canvas_set_color(canvas, ColorWhite);

    int32_t gx = x + (btn_w - content_w) / 2;
    int32_t gy_icon = bar_y + (LOADER_STILL_LOADING_BAR_H - icon_h) / 2;
    canvas_draw_icon(canvas, gx, gy_icon, icon);
    canvas_draw_str_aligned(
        canvas,
        gx + icon_w + icon_gap,
        bar_y + LOADER_STILL_LOADING_BAR_H / 2,
        AlignLeft,
        AlignCenter,
        label);

    canvas_set_color(canvas, ColorBlack);
}

static void loader_still_loading_draw_callback(Canvas* canvas, void* model) {
    LoaderStillLoadingModel* m = model;
    canvas_clear(canvas);
    loader_still_loading_draw_spinner(canvas, m->spin_frame);

    if(!m->buttons_visible) {
        return;
    }

    if(m->show_wait_button) {
        loader_still_loading_draw_two_buttons(canvas, m->focus_left, "Restart", "Wait");
    } else {
        loader_still_loading_draw_one_button(canvas, "Restart");
    }
}

static void loader_still_loading_spin_timer_callback(void* context) {
    Loader* loader = context;
    with_view_model(
        loader->still_loading_view,
        LoaderStillLoadingModel* model,
        { model->spin_frame = (uint8_t)((model->spin_frame + 1u) % 8u); },
        true);
}

static void loader_still_loading_wait_timer_callback(void* context) {
    Loader* loader = context;
    with_view_model(
        loader->still_loading_view,
        LoaderStillLoadingModel* model,
        {
            model->buttons_visible = true;
            model->show_wait_button = false;
        },
        true);
}

static void loader_still_loading_enter_callback(void* context) {
    Loader* loader = context;
    with_view_model(
        loader->still_loading_view,
        LoaderStillLoadingModel* model,
        {
            model->spin_frame = 0;
            model->buttons_visible = true;
            model->show_wait_button = true;
            model->focus_left = false;
        },
        false);
    furi_timer_start(
        loader->still_loading_spin_timer,
        furi_ms_to_ticks(LOADER_STILL_LOADING_SPIN_INTERVAL_MS));
}

static void loader_still_loading_exit_callback(void* context) {
    Loader* loader = context;
    furi_timer_stop(loader->still_loading_spin_timer);
    furi_timer_stop(loader->still_loading_wait_timer);
}

static bool loader_still_loading_input_callback(InputEvent* event, void* context) {
    Loader* loader = context;
    if(event->type != InputTypeShort) {
        return true;
    }

    bool do_reset = false;
    bool start_wait_timer = false;

    with_view_model(
        loader->still_loading_view,
        LoaderStillLoadingModel* model,
        {
            if(model->buttons_visible && model->show_wait_button) {
                switch(event->key) {
                case InputKeyLeft:
                    model->focus_left = true;
                    break;
                case InputKeyRight:
                    model->focus_left = false;
                    break;
                case InputKeyOk:
                    if(model->focus_left) {
                        do_reset = true;
                    } else {
                        model->buttons_visible = false;
                        start_wait_timer = true;
                    }
                    break;
                default:
                    break;
                }
            } else if(model->buttons_visible && !model->show_wait_button) {
                if(event->key == InputKeyOk) {
                    do_reset = true;
                }
            }
        },
        true);

    if(start_wait_timer) {
        furi_timer_start(
            loader->still_loading_wait_timer, furi_ms_to_ticks(LOADER_STILL_LOADING_WAIT_MS));
    }

    if(do_reset) {
        furi_hal_power_reset();
    }

    return true;
}

static void loader_load_watchdog_callback(void* context) {
    Loader* loader = context;
    uint32_t elapsed = furi_get_tick() - loader->load_watchdog_started_tick;
    FURI_LOG_E(
        TAG,
        "Loading spinner still visible after %lums for \"%s\" - switching to still-loading "
        "screen (underlying load may still be in progress and will open normally if/when it "
        "finishes)",
        (unsigned long)elapsed,
        loader->load_watchdog_app_name);
    view_holder_set_view(loader->view_holder, loader->still_loading_view);
}

static void loader_load_watchdog_arm(Loader* loader, const char* app_name) {
    strncpy(
        loader->load_watchdog_app_name,
        app_name ? app_name : "?",
        sizeof(loader->load_watchdog_app_name) - 1);
    loader->load_watchdog_app_name[sizeof(loader->load_watchdog_app_name) - 1] = '\0';
    loader->load_watchdog_started_tick = furi_get_tick();
    furi_timer_start(loader->load_watchdog, furi_ms_to_ticks(LOADER_LOAD_WATCHDOG_TIMEOUT_MS));
}

static void loader_load_watchdog_disarm(Loader* loader) {
    furi_timer_stop(loader->load_watchdog);
}

static void loader_cover_release(Loader* loader) {
    if(loader_menu_settings_return_pending()) {
        view_holder_set_view(loader->view_holder, empty_screen_get_view(loader->empty_screen));
        view_holder_send_to_back(loader->view_holder);
    } else {
        view_holder_set_view(loader->view_holder, NULL);
    }
}

static const char* loader_find_external_application_by_name(const char* app_name) {
    for(size_t i = 0; i < FLIPPER_EXTERNAL_APPS_COUNT; i++) {
        if(strcmp(FLIPPER_EXTERNAL_APPS[i].name, app_name) == 0) {
            return FLIPPER_EXTERNAL_APPS[i].path;
        }
    }

    for(size_t i = 0; i < FLIPPER_EXTSETTINGS_APPS_COUNT; i++) {
        if(strcmp(FLIPPER_EXTSETTINGS_APPS[i].name, app_name) == 0) {
            return FLIPPER_EXTSETTINGS_APPS[i].path;
        }
    }

    return NULL;
}

static LoaderMessageLoaderStatusResult loader_start_internal(
    Loader* loader,
    const char* name,
    const char* args,
    FuriString* error_message) {
    LoaderMessage message;
    LoaderMessageLoaderStatusResult result;

    message.type = LoaderMessageTypeStartByName;
    message.start.name = name;
    message.start.args = args;
    message.start.error_message = error_message;
    message.api_lock = api_lock_alloc_locked();
    message.status_value = &result;
    furi_message_queue_put(loader->queue, &message, FuriWaitForever);
    api_lock_wait_unlock_and_free(message.api_lock);

    return result;
}

typedef struct {
    const char* error;
    const char* description;
    const char* url;
    const Icon* icon;
} LoaderError;

static const LoaderError err_app_not_found =
    {"App Not Found", "Update firmware or app", "err_01", &I_err_01};
static const LoaderError err_invalid_flie = {"Invalid File", "Update the app", "err_02", &I_err_02};
static const LoaderError err_invalid_manifest =
    {"Invalid Manifest", "Update firmware or app", "err_03", &I_err_03};
static const LoaderError err_missing_imports =
    {"Missing Imports", "Update firmware or app", "err_04", &I_err_04};
static const LoaderError err_hw_target_mismatch =
    {"HW Target\nMismatch", "App not supported", "err_05", &I_err_05};

static void loader_dialog_prepare_and_show(DialogsApp* dialogs, const LoaderError* err) {
    FuriString* header = furi_string_alloc_printf("Error: %s", err->error);
    FuriString* text =
        furi_string_alloc_printf("%s\nLearn more:\nr.flipper.net/%s", err->description, err->url);
    DialogMessage* message = dialog_message_alloc();

    dialog_message_set_header(message, furi_string_get_cstr(header), 64, 0, AlignCenter, AlignTop);
    dialog_message_set_text(message, furi_string_get_cstr(text), 0, 63, AlignLeft, AlignBottom);
    dialog_message_set_icon(message, err->icon, 128 - 25, 64 - 25);
    dialog_message_show(dialogs, message);

    dialog_message_free(message);
    furi_string_free(header);
    furi_string_free(text);
}

static void loader_show_gui_error(
    LoaderMessageLoaderStatusResult status,
    const char* name,
    FuriString* error_message) {
    furi_check(name);
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* message = dialog_message_alloc();

    if(status.value == LoaderStatusErrorUnknownApp &&
       loader_find_external_application_by_name(name) != NULL) {

        const char* header = NULL;
        const char* text = NULL;
        Storage* storage = furi_record_open(RECORD_STORAGE);
        if(storage_sd_status(storage) == FSE_OK) {
            header = "Update needed";
            text = "Update firmware\nto run this app";
        } else {
            header = "SD card needed";
            text = "Install SD card\nto run this app";
        }
        furi_record_close(RECORD_STORAGE);
        dialog_message_set_header(message, header, 64, 3, AlignCenter, AlignTop);

        dialog_message_set_text(message, text, 3, 26, AlignLeft, AlignTop);
        dialog_message_show(dialogs, message);
    } else if(status.value == LoaderStatusErrorUnknownApp) {
        loader_dialog_prepare_and_show(dialogs, &err_app_not_found);
    } else if(status.value == LoaderStatusErrorInternal) {

        switch(status.error) {
        case LoaderStatusErrorInvalidFile:
            loader_dialog_prepare_and_show(dialogs, &err_invalid_flie);
            break;
        case LoaderStatusErrorInvalidManifest:
            loader_dialog_prepare_and_show(dialogs, &err_invalid_manifest);
            break;
        case LoaderStatusErrorMissingImports:
            loader_dialog_prepare_and_show(dialogs, &err_missing_imports);
            break;
        case LoaderStatusErrorHWMismatch:
            loader_dialog_prepare_and_show(dialogs, &err_hw_target_mismatch);
            break;

        case LoaderStatusErrorOutOfMemory:
            dialog_message_set_header(
                message, "Error: Out of Memory", 64, 0, AlignCenter, AlignTop);
            dialog_message_set_text(
                message,
                "Not enough RAM to run the\napp. Please reboot the device",
                64,
                13,
                AlignCenter,
                AlignTop);
            dialog_message_set_buttons(message, NULL, NULL, "Reboot");
            if(dialog_message_show(dialogs, message) == DialogMessageButtonRight) {
                furi_hal_power_reset();
            }
            break;
        default:

            dialog_message_set_header(message, "Error", 64, 0, AlignCenter, AlignTop);

            furi_string_replace(error_message, ":", "\n");
            furi_string_replace(error_message, "/ext/apps/", "");
            furi_string_replace(error_message, ", ", "\n");
            furi_string_replace(error_message, ": ", "\n");

            dialog_message_set_text(
                message, furi_string_get_cstr(error_message), 64, 35, AlignCenter, AlignCenter);
            dialog_message_show(dialogs, message);
            break;
        }
    }

    dialog_message_free(message);
    furi_record_close(RECORD_DIALOGS);
}

static void loader_generic_synchronous_request(Loader* loader, LoaderMessage* message) {
    furi_check(loader);
    message->api_lock = api_lock_alloc_locked();
    furi_message_queue_put(loader->queue, message, FuriWaitForever);
    api_lock_wait_unlock_and_free(message->api_lock);
}

LoaderStatus
    loader_start(Loader* loader, const char* name, const char* args, FuriString* error_message) {
    furi_check(loader);
    furi_check(name);

    LoaderMessageLoaderStatusResult result =
        loader_start_internal(loader, name, args, error_message);
    return result.value;
}

LoaderStatus loader_start_with_gui_error(Loader* loader, const char* name, const char* args) {
    furi_check(loader);
    furi_check(name);

    FuriString* error_message = furi_string_alloc();
    LoaderMessageLoaderStatusResult result =
        loader_start_internal(loader, name, args, error_message);
    loader_show_gui_error(result, name, error_message);
    furi_string_free(error_message);
    return result.value;
}

void loader_start_detached_with_gui_error(Loader* loader, const char* name, const char* args) {
    furi_check(loader);
    furi_check(name);

    LoaderMessage message = {
        .type = LoaderMessageTypeStartByNameDetachedWithGuiError,
        .start.name = strdup(name),
        .start.args = args ? strdup(args) : NULL,
    };
    furi_message_queue_put(loader->queue, &message, FuriWaitForever);
}

bool loader_lock(Loader* loader) {
    LoaderMessageBoolResult result;
    LoaderMessage message = {
        .type = LoaderMessageTypeLock,
        .bool_value = &result,
    };
    loader_generic_synchronous_request(loader, &message);
    return result.value;
}

void loader_unlock(Loader* loader) {
    furi_check(loader);

    LoaderMessage message;
    message.type = LoaderMessageTypeUnlock;

    furi_message_queue_put(loader->queue, &message, FuriWaitForever);
}

bool loader_is_locked(Loader* loader) {
    LoaderMessageBoolResult result;
    LoaderMessage message = {
        .type = LoaderMessageTypeIsLocked,
        .bool_value = &result,
    };
    loader_generic_synchronous_request(loader, &message);
    return result.value;
}

void loader_show_menu(Loader* loader) {
    furi_check(loader);

    LoaderMessage message;
    message.type = LoaderMessageTypeShowMenu;

    furi_message_queue_put(loader->queue, &message, FuriWaitForever);
}

void loader_ensure_menu_built(Loader* loader) {
    furi_check(loader);
    LoaderMessage message = {
        .api_lock = loader->ensure_menu_lock,
        .type = LoaderMessageTypeEnsureMenuBuilt,
    };
    const uint32_t timeout = furi_ms_to_ticks(2000);

    api_lock_relock(loader->ensure_menu_lock);
    if(furi_message_queue_put(loader->queue, &message, timeout) != FuriStatusOk) {
        FURI_LOG_W(TAG, "ensure_menu_built timed out");
        return;
    }
    if(api_lock_wait_unlock_with_timeout(loader->ensure_menu_lock, timeout) & FuriFlagError) {
        FURI_LOG_W(TAG, "ensure_menu_built timed out");
    }
}

void loader_release_hidden_menu(Loader* loader) {
    furi_check(loader);

    LoaderMessage message;
    message.type = LoaderMessageTypeReleaseHiddenMenu;

    furi_message_queue_put(loader->queue, &message, FuriWaitForever);
}

FuriPubSub* loader_get_pubsub(Loader* loader) {
    furi_check(loader);

    return loader->pubsub;
}

bool loader_signal(Loader* loader, uint32_t signal, void* arg) {
    LoaderMessageBoolResult result;
    LoaderMessage message = {
        .type = LoaderMessageTypeSignal,
        .signal.signal = signal,
        .signal.arg = arg,
        .bool_value = &result,
    };
    loader_generic_synchronous_request(loader, &message);
    return result.value;
}

bool loader_get_application_name(Loader* loader, FuriString* name) {
    LoaderMessageBoolResult result;
    LoaderMessage message = {
        .type = LoaderMessageTypeGetApplicationName,
        .application_name = name,
        .bool_value = &result,
    };
    loader_generic_synchronous_request(loader, &message);
    return result.value;
}

bool loader_get_application_id(Loader* loader, FuriString* appid) {
    LoaderMessageBoolResult result;
    LoaderMessage message = {
        .type = LoaderMessageTypeGetApplicationId,
        .application_name = appid,
        .bool_value = &result,
    };
    loader_generic_synchronous_request(loader, &message);
    return result.value;
}

bool loader_get_application_launch_path(Loader* loader, FuriString* name) {
    LoaderMessageBoolResult result;
    LoaderMessage message = {
        .type = LoaderMessageTypeGetApplicationLaunchPath,
        .application_name = name,
        .bool_value = &result,
    };
    loader_generic_synchronous_request(loader, &message);
    return result.value;
}

void loader_enqueue_launch(
    Loader* loader,
    const char* name,
    const char* args,
    LoaderDeferredLaunchFlag flags) {
    LoaderMessage message = {
        .type = LoaderMessageTypeEnqueueLaunch,
        .defer_start =
            {
                .name_or_path = strdup(name),
                .args = args ? strdup(args) : NULL,
                .flags = flags,
            },
    };
    loader_generic_synchronous_request(loader, &message);
}

void loader_clear_launch_queue(Loader* loader) {
    LoaderMessage message = {
        .type = LoaderMessageTypeClearLaunchQueue,
    };
    loader_generic_synchronous_request(loader, &message);
}

static void loader_menu_closed_callback(void* context) {
    Loader* loader = context;
    LoaderMessage message;
    message.type = LoaderMessageTypeMenuClosed;
    furi_message_queue_put(loader->queue, &message, FuriWaitForever);
}

static void loader_applications_closed_callback(void* context) {
    Loader* loader = context;
    LoaderMessage message;
    message.type = LoaderMessageTypeApplicationsClosed;
    furi_message_queue_put(loader->queue, &message, FuriWaitForever);
}

static void
    loader_thread_state_callback(FuriThread* thread, FuriThreadState thread_state, void* context) {
    UNUSED(thread);
    furi_assert(context);

    if(thread_state == FuriThreadStateStopped) {
        Loader* loader = context;

        furi_hal_power_set_force_light_sleep(false);

        LoaderMessage message;
        message.type = LoaderMessageTypeAppClosed;
        furi_message_queue_put(loader->queue, &message, FuriWaitForever);
    }
}

static Loader* loader_alloc(void) {
    Loader* loader = malloc(sizeof(Loader));
    loader->pubsub = furi_pubsub_alloc();
    loader->queue = furi_message_queue_alloc(1, sizeof(LoaderMessage));
    loader->ensure_menu_lock = api_lock_alloc_locked();
    loader->gui = furi_record_open(RECORD_GUI);
    loader->view_holder = view_holder_alloc();
    loader->loading = loading_alloc();
    loader->empty_screen = empty_screen_alloc();
    view_holder_attach_to_gui(loader->view_holder, loader->gui);
    loader->load_watchdog =
        furi_timer_alloc(loader_load_watchdog_callback, FuriTimerTypeOnce, loader);
    loader->still_loading_wait_timer =
        furi_timer_alloc(loader_still_loading_wait_timer_callback, FuriTimerTypeOnce, loader);
    loader->still_loading_spin_timer = furi_timer_alloc(
        loader_still_loading_spin_timer_callback, FuriTimerTypePeriodic, loader);
    loader->still_loading_view = view_alloc();
    view_allocate_model(
        loader->still_loading_view, ViewModelTypeLocking, sizeof(LoaderStillLoadingModel));
    view_set_draw_callback(loader->still_loading_view, loader_still_loading_draw_callback);
    view_set_input_callback(loader->still_loading_view, loader_still_loading_input_callback);
    view_set_enter_callback(loader->still_loading_view, loader_still_loading_enter_callback);
    view_set_exit_callback(loader->still_loading_view, loader_still_loading_exit_callback);
    view_set_context(loader->still_loading_view, loader);
    return loader;
}

static FlipperInternalApplication const* loader_find_application_by_name_in_list(
    const char* name,
    const FlipperInternalApplication* list,
    const uint32_t n_apps) {
    for(size_t i = 0; i < n_apps; i++) {
        if((strcmp(name, list[i].name) == 0) || (strcmp(name, list[i].appid) == 0)) {
            return &list[i];
        }
    }
    return NULL;
}

static const FlipperInternalApplication* loader_find_application_by_name(const char* name) {
    const struct {
        const FlipperInternalApplication* list;
        const uint32_t count;
    } lists[] = {
        {FLIPPER_APPS, FLIPPER_APPS_COUNT},
        {FLIPPER_SETTINGS_APPS, FLIPPER_SETTINGS_APPS_COUNT},
        {FLIPPER_SYSTEM_APPS, FLIPPER_SYSTEM_APPS_COUNT},
        {FLIPPER_DEBUG_APPS, FLIPPER_DEBUG_APPS_COUNT},
    };

    for(size_t i = 0; i < COUNT_OF(lists); i++) {
        const FlipperInternalApplication* application =
            loader_find_application_by_name_in_list(name, lists[i].list, lists[i].count);
        if(application) {
            return application;
        }
    }

    return NULL;
}

static void loader_start_app_thread(Loader* loader, FlipperInternalApplicationFlag flags) {

    FuriHalRtcHeapTrackMode mode = furi_hal_rtc_get_heap_track_mode();
    if(mode > FuriHalRtcHeapTrackModeNone) {
        furi_thread_enable_heap_trace(loader->app.thread);
    } else {
        furi_thread_disable_heap_trace(loader->app.thread);
    }

    if(!(flags & FlipperInternalApplicationFlagInsomniaSafe)) {
        furi_hal_power_insomnia_enter();
        loader->app.insomniac = true;
    } else {
        loader->app.insomniac = false;
    }

    furi_thread_set_state_context(loader->app.thread, loader);
    furi_thread_set_state_callback(loader->app.thread, loader_thread_state_callback);

    furi_hal_power_set_force_light_sleep(true);

    furi_thread_start(loader->app.thread);
}

static void loader_start_internal_app(
    Loader* loader,
    const FlipperInternalApplication* app,
    const char* args) {
    FURI_LOG_I(TAG, "Starting %s", app->name);

    furi_assert(loader->app.args == NULL);
    if(args && strlen(args) > 0) {
        loader->app.args = strdup(args);
    }

    loader->app.thread =
        furi_thread_alloc_ex(app->name, app->stack_size, app->app, loader->app.args);
    furi_thread_set_appid(loader->app.thread, app->appid);

    loader_start_app_thread(loader, app->flags);
}

static void loader_log_status_error(
    LoaderStatus status,
    FuriString* error_message,
    const char* format,
    va_list args) {
    if(error_message) {
        furi_string_vprintf(error_message, format, args);
        FURI_LOG_E(TAG, "Status [%d]: %s", status, furi_string_get_cstr(error_message));
    } else {
        FURI_LOG_E(TAG, "Status [%d]", status);
    }
}

static LoaderStatus loader_make_status_error(
    LoaderStatus status,
    FuriString* error_message,
    const char* format,
    ...) {
    va_list args;
    va_start(args, format);
    loader_log_status_error(status, error_message, format, args);
    va_end(args);
    return status;
}

static LoaderStatus loader_make_success_status(FuriString* error_message) {
    if(error_message) {
        furi_string_set(error_message, "App started");
    }

    return LoaderStatusOk;
}

static LoaderStatusError
    loader_status_error_from_preload_status(FlipperApplicationPreloadStatus status) {
    switch(status) {
    case FlipperApplicationPreloadStatusInvalidFile:
        return LoaderStatusErrorInvalidFile;
    case FlipperApplicationPreloadStatusNotEnoughMemory:
        return LoaderStatusErrorOutOfMemory;
    case FlipperApplicationPreloadStatusInvalidManifest:
        return LoaderStatusErrorInvalidManifest;
    case FlipperApplicationPreloadStatusApiTooOld:
        return LoaderStatusErrorOutdatedApp;
    case FlipperApplicationPreloadStatusApiTooNew:
        return LoaderStatusErrorOutdatedFirmware;
    case FlipperApplicationPreloadStatusTargetMismatch:
        return LoaderStatusErrorHWMismatch;
    default:
        return LoaderStatusErrorUnknown;
    }
}

static LoaderStatusError
    loader_status_error_from_load_status(FlipperApplicationLoadStatus status) {
    switch(status) {
    case FlipperApplicationLoadStatusMissingImports:
        return LoaderStatusErrorMissingImports;
    default:
        return LoaderStatusErrorUnknown;
    }
}

static void loader_wait_for_storage_ready(void) {
    while(!furi_record_exists(RECORD_STORAGE)) {
        furi_delay_ms(10);
    }
}

static LoaderMessageLoaderStatusResult loader_start_external_app(
    Loader* loader,
    Storage* storage,
    const char* path,
    const char* args,
    FuriString* error_message,
    bool ignore_api_mismatch) {
    LoaderMessageLoaderStatusResult result;
    result.value = loader_make_success_status(error_message);
    result.error = LoaderStatusErrorUnknown;

    do {
        heap_alloc_guard_lock();

        loader->app.fap = flipper_application_alloc(storage, firmware_api_interface);
        size_t start = furi_get_tick();

        FURI_LOG_I(
            TAG,
            "Loading %s: free heap %zu, max free block %zu",
            path,
            memmgr_get_free_heap(),
            memmgr_heap_get_max_free_block());

        FlipperApplicationPreloadStatus preload_res =
            flipper_application_preload(loader->app.fap, path);

        heap_alloc_guard_unlock();

        {
            size_t preload_ms = furi_get_tick() - start;
            if(preload_ms >= LOADER_LOAD_WARN_THRESHOLD_MS) {
                FURI_LOG_W(TAG, "Slow preload: %zums for %s", preload_ms, path);
            }
        }
        if(preload_res != FlipperApplicationPreloadStatusSuccess) {
            if((preload_res == FlipperApplicationPreloadStatusApiTooOld) ||
               (preload_res == FlipperApplicationPreloadStatusApiTooNew)) {
                if(!ignore_api_mismatch) {
                    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);

                    const FlipperApplicationManifest* manifest =
                        flipper_application_get_manifest(loader->app.fap);

                    bool app_newer = preload_res == FlipperApplicationPreloadStatusApiTooNew;
                    const char* header = app_newer ? "App Too New" : "App Too Old";
                    char text[63];
                    snprintf(
                        text,
                        sizeof(text),
                        "APP:%i %c FW:%i\nThis app might not work\nContinue anyways?",
                        manifest->base.api_version.major,
                        app_newer ? '>' : '<',
                        firmware_api_interface->api_version_major);

                    DialogMessage* message = dialog_message_alloc();
                    dialog_message_set_header(message, header, 64, 0, AlignCenter, AlignTop);
                    dialog_message_set_buttons(message, NULL, NULL, "Continue");
                    dialog_message_set_text(message, text, 64, 32, AlignCenter, AlignCenter);
                    if(dialog_message_show(dialogs, message) == DialogMessageButtonRight) {
                        result.value = loader_make_status_error(
                            LoaderStatusErrorApiMismatch, error_message, "API Mismatch");
                        result.error = loader_status_error_from_preload_status(preload_res);
                    } else {
                        result.value = loader_make_status_error(
                            LoaderStatusErrorApiMismatchExit, error_message, "API Mismatch");
                        result.error = loader_status_error_from_preload_status(preload_res);
                    }
                    dialog_message_free(message);
                    furi_record_close(RECORD_DIALOGS);
                    break;
                }
            } else {
                const char* err_msg = flipper_application_preload_status_to_string(preload_res);
                result.value = loader_make_status_error(
                    LoaderStatusErrorInternal,
                    error_message,
                    "Preload failed, %s: %s",
                    path,
                    err_msg);
                result.error = loader_status_error_from_preload_status(preload_res);
                break;
            }
        }

        heap_alloc_guard_lock();

        size_t map_start = furi_get_tick();
        FlipperApplicationLoadStatus load_status =
            flipper_application_map_to_memory(loader->app.fap);
        {
            size_t map_ms = furi_get_tick() - map_start;
            if(map_ms >= LOADER_LOAD_WARN_THRESHOLD_MS) {
                FURI_LOG_W(TAG, "Slow map_to_memory: %zums for %s", map_ms, path);
            }
        }
        if(load_status != FlipperApplicationLoadStatusSuccess) {
            heap_alloc_guard_unlock();
            const char* err_msg = flipper_application_load_status_to_string(load_status);
            result.value = loader_make_status_error(
                LoaderStatusErrorInternal, error_message, "Load failed, %s: %s", path, err_msg);
            result.error = loader_status_error_from_load_status(load_status);
            break;
        }

        size_t total_ms = furi_get_tick() - start;
        if(total_ms >= LOADER_LOAD_WARN_THRESHOLD_MS) {
            FURI_LOG_W(TAG, "Slow total load: %zums for %s", total_ms, path);
        }
        FURI_LOG_I(TAG, "Loaded in %zums", total_ms);

        if(flipper_application_is_plugin(loader->app.fap)) {
            heap_alloc_guard_unlock();
            result.value = loader_make_status_error(
                LoaderStatusErrorInternal, error_message, "Plugin %s is not runnable", path);
            break;
        }

        loader->app.thread = flipper_application_alloc_thread(loader->app.fap, args);
        FuriString* app_name = furi_string_alloc();
        path_extract_filename_no_ext(path, app_name);
        furi_thread_set_appid(loader->app.thread, furi_string_get_cstr(app_name));
        furi_string_free(app_name);

        if(furi_hal_debug_is_gdb_session_active()) {
            FURI_LOG_W(TAG, "Triggering BP for debugger");

            __asm volatile("bkpt 0");
        }

        loader_start_app_thread(loader, FlipperInternalApplicationFlagDefault);

        heap_alloc_guard_unlock();
    } while(0);

    if(result.value != LoaderStatusOk) {
        heap_alloc_guard_lock();
        flipper_application_free(loader->app.fap);
        heap_alloc_guard_unlock();
        loader->app.fap = NULL;
        LoaderEvent event;
        event.type = LoaderEventTypeApplicationLoadFailed;
        furi_pubsub_publish(loader->pubsub, &event);
    }

    return result;
}

static void loader_do_menu_show(Loader* loader) {
    if(!loader->loader_menu) {
        loader->loader_menu = loader_menu_alloc(loader_menu_closed_callback, loader);
    }
    loader_menu_take_settings_return();
    loader_menu_show(loader->loader_menu);
    loader->menu_shown = true;
}

static bool loader_do_menu_show_settings_if_pending(Loader* loader) {
    if(loader->app.thread == (FuriThread*)LOADER_MAGIC_THREAD_VALUE) {
        loader_menu_take_settings_return();
        view_holder_set_view(loader->view_holder, NULL);
        return false;
    }
    if(loader->app.thread || loader->menu_shown) {
        return false;
    }
    const char* settings_item = loader_menu_take_settings_return();
    if(!settings_item) {
        return false;
    }
    if(!loader->loader_menu) {
        loader->loader_menu = loader_menu_alloc(loader_menu_closed_callback, loader);
    }
    loader_menu_show_settings(loader->loader_menu, settings_item);
    loader->menu_shown = true;
    return true;
}

static void loader_do_menu_closed(Loader* loader) {
    /* Anti-fragmentation: do NOT free the menu here. It is built once at boot
     * and kept for the firmware lifetime. The menu thread has already parked
     * itself (its view_dispatcher_run returned) and is waiting for the next
     * show flag; we only need to drop the "shown" state. Freeing/rebuilding on
     * every close was the main source of heap fragmentation. */
    loader->menu_shown = false;
    if(!loader_do_menu_show_settings_if_pending(loader) && !loader->app.thread) {
        view_holder_set_view(loader->view_holder, NULL);
    }
}

static void loader_do_ensure_menu_built(Loader* loader) {
    if(!loader->loader_menu) {
        loader->loader_menu = loader_menu_alloc(loader_menu_closed_callback, loader);
    }
}

static void loader_do_release_hidden_menu(Loader* loader) {
    /* Anti-fragmentation: the menu is built once at boot and kept permanently,
     * so there is nothing to release on qFlipper/CLI disconnect. Freeing it
     * here (when it happened to not be shown) was a key fragmentation trigger:
     * the connect-built, disconnect-freed menu stranded a small survivor block
     * high in the heap and split the contiguous free tail. */
    UNUSED(loader);
}

static void loader_do_applications_show(Loader* loader) {
    if(!loader->loader_applications) {
        loader->loader_applications =
            loader_applications_alloc(loader_applications_closed_callback, loader);
    }
}

static void loader_do_applications_closed(Loader* loader) {
    if(loader->loader_applications) {
        loader_applications_free(loader->loader_applications);
        loader->loader_applications = NULL;
    }
}

static bool loader_do_is_locked(Loader* loader) {
    return loader->app.thread != NULL;
}

static LoaderMessageLoaderStatusResult loader_do_start_by_name(
    Loader* loader,
    const char* name,
    const char* args,
    FuriString* error_message) {
    LoaderMessageLoaderStatusResult status;
    status.value = loader_make_success_status(error_message);
    status.error = LoaderStatusErrorUnknown;

    if(name == NULL) return status;

    do {

        if(loader_do_is_locked(loader)) {
            if(loader->app.thread == (FuriThread*)LOADER_MAGIC_THREAD_VALUE) {
                status.value = loader_make_status_error(
                    LoaderStatusErrorAppStarted, error_message, "Loader is locked");
            } else {
                const char* current_thread_name =
                    furi_thread_get_name(furi_thread_get_id(loader->app.thread));

                status.value = loader_make_status_error(
                    LoaderStatusErrorAppStarted,
                    error_message,
                    "Loader is locked, please close the \"%s\" first",
                    current_thread_name);
            }
            break;
        }

        if(strcmp(name, LOADER_APPLICATIONS_NAME) == 0) {
            heap_alloc_guard_lock();
            loader_do_applications_show(loader);
            heap_alloc_guard_unlock();
            status.value = loader_make_success_status(error_message);
            break;
        }

        LoaderEvent event;
        event.type = LoaderEventTypeApplicationBeforeLoad;

        furi_pubsub_publish(loader->pubsub, &event);

        {
            const FlipperInternalApplication* app = loader_find_application_by_name(name);
            if(app) {
                heap_alloc_guard_lock();
                loader_start_internal_app(loader, app, args);
                heap_alloc_guard_unlock();
                status.value = loader_make_success_status(error_message);
                break;
            }
        }

        {
            const char* path = loader_find_external_application_by_name(name);
            if(path) {
                name = path;
            }
        }

        {
            Storage* storage = furi_record_open(RECORD_STORAGE);
            if(storage_file_exists(storage, name)) {
                status =
                    loader_start_external_app(loader, storage, name, args, error_message, false);
                if(status.value == LoaderStatusErrorApiMismatch) {
                    status = loader_start_external_app(
                        loader, storage, name, args, error_message, true);
                }
                furi_record_close(RECORD_STORAGE);
                break;
            }
            furi_record_close(RECORD_STORAGE);
        }

        status.value = loader_make_status_error(
            LoaderStatusErrorUnknownApp, error_message, "Application \"%s\" not found", name);
    } while(false);

    if(status.value == LoaderStatusOk) {
        if(loader->app.launch_path) {
            furi_string_free(loader->app.launch_path);
        }
        loader->app.launch_path = furi_string_alloc_set_str(name);
    }

    return status;
}

static bool loader_do_lock(Loader* loader) {
    if(loader->app.thread) {
        return false;
    }

    loader->app.thread = (FuriThread*)LOADER_MAGIC_THREAD_VALUE;
    return true;
}

static void loader_do_unlock(Loader* loader) {
    furi_check(loader->app.thread == (FuriThread*)LOADER_MAGIC_THREAD_VALUE);
    loader->app.thread = NULL;
}

static void loader_do_emit_queue_empty_event(Loader* loader) {
    if(loader_do_is_locked(loader)) return;
    FURI_LOG_I(TAG, "Launch queue empty");
    LoaderEvent event;
    event.type = LoaderEventTypeNoMoreAppsInQueue;
    furi_pubsub_publish(loader->pubsub, &event);
}

static bool loader_do_deferred_launch(Loader* loader, LoaderDeferredLaunchRecord* record, bool was_queued);

static void loader_do_next_deferred_launch_if_available(Loader* loader) {
    LoaderDeferredLaunchRecord record;
    if(loader_queue_pop(&loader->launch_queue, &record)) {
        loader_do_deferred_launch(loader, &record, true );
        loader_queue_item_clear(&record);
    } else {

        loader_load_watchdog_disarm(loader);
        loader_cover_release(loader);

        loader_do_emit_queue_empty_event(loader);

    }
}

#define LOADER_WHEEL_STACK_SIZE_THRESHOLD (4 * 1024)

static const char* const loader_wheel_force_include[] = {
    "subghz",
    "subghz_randomattack",
    "fox_rf_jammer",
};

static const char* const loader_wheel_force_exclude[] = {
    "ffb",
    "fox_xbm_converter",
    "fox_hitag2_hell",
    "fox_chill",
    "subghz_frequency_analyzer",
    "subghz_modulation_analyzer",
    "subghz_raw_edit",
};

static bool loader_wheel_name_matches_appid(const char* app_name, const char* appid) {
    if(strcmp(app_name, appid) == 0) return true;

    size_t name_len = strlen(app_name);
    size_t id_len = strlen(appid);
    size_t suffix_len = id_len + 4;
    if(name_len < suffix_len) return false;

    const char* suffix = app_name + (name_len - suffix_len);
    if(suffix != app_name && *(suffix - 1) != '/') return false;
    return strncmp(suffix, appid, id_len) == 0 && strcmp(suffix + id_len, ".fap") == 0;
}

static bool loader_wheel_should_skip(const char* app_name) {
    if(!app_name) return false;

    for(size_t i = 0; i < COUNT_OF(loader_wheel_force_include); i++) {
        if(loader_wheel_name_matches_appid(app_name, loader_wheel_force_include[i])) return false;
    }
    for(size_t i = 0; i < COUNT_OF(loader_wheel_force_exclude); i++) {
        if(loader_wheel_name_matches_appid(app_name, loader_wheel_force_exclude[i])) return true;
    }

    const FlipperInternalApplication* internal_app = loader_find_application_by_name(app_name);
    if(internal_app) {
        return internal_app->stack_size < LOADER_WHEEL_STACK_SIZE_THRESHOLD;
    }

    return false;
}

static bool loader_do_deferred_launch(Loader* loader, LoaderDeferredLaunchRecord* record, bool was_queued) {
    furi_assert(loader);
    furi_assert(record);

    bool is_successful = false;
    FuriString* error_message = furi_string_alloc();

    bool skip_loading_view = loader_wheel_should_skip(record->name_or_path);

    bool keep_loading_view = false;
    if(was_queued && record->name_or_path && strcmp(record->name_or_path, "subghz") == 0) {
        keep_loading_view = true;
    }

    if(!skip_loading_view) {
        view_holder_set_view(loader->view_holder, loading_get_view(loader->loading));
        view_holder_send_to_front(loader->view_holder);
    }
    loader_load_watchdog_arm(loader, record->name_or_path);

    do {
        const char* app_name_str = record->name_or_path;
        const char* app_args = record->args;
        FURI_LOG_I(TAG, "Deferred launch: %s", app_name_str);

        LoaderMessageLoaderStatusResult result =
            loader_do_start_by_name(loader, app_name_str, app_args, error_message);
        if(result.value == LoaderStatusOk) {
            is_successful = true;
            break;
        }

        if(record->flags & LoaderDeferredLaunchFlagGui)
            loader_show_gui_error(result, app_name_str, error_message);

        loader_do_next_deferred_launch_if_available(loader);
    } while(false);

    loader_load_watchdog_disarm(loader);
    if(!is_successful) {
        keep_loading_view = false;
    }
    if(!keep_loading_view && (!skip_loading_view || loader_menu_settings_return_pending())) {
        loader_cover_release(loader);
    }
    furi_string_free(error_message);
    return is_successful;
}

static void loader_show_loading_for_launch(Loader* loader, const char* app_name) {
    if(!loader_wheel_should_skip(app_name)) {
        view_holder_set_view(loader->view_holder, loading_get_view(loader->loading));
        view_holder_send_to_front(loader->view_holder);
    }
    loader_load_watchdog_arm(loader, app_name);
}

static void loader_hide_loading_for_launch(Loader* loader) {
    loader_load_watchdog_disarm(loader);
    loader_cover_release(loader);
}

static void loader_do_app_closed(Loader* loader) {
    furi_assert(loader->app.thread);

    char app_path_copy[64];
    strncpy(
        app_path_copy,
        loader->app.launch_path ? furi_string_get_cstr(loader->app.launch_path) : "?",
        sizeof(app_path_copy) - 1);
    app_path_copy[sizeof(app_path_copy) - 1] = '\0';

    furi_thread_join(loader->app.thread);
    FURI_LOG_I(TAG, "App returned: %li", furi_thread_get_return_code(loader->app.thread));

    if(loader->app.insomniac) {
        furi_hal_power_insomnia_exit();
    }

    furi_kernel_lock();
    if(loader->app.args) {
        free(loader->app.args);
        loader->app.args = NULL;
    }

    if(loader->app.fap) {
        flipper_application_free(loader->app.fap);
        loader->app.fap = NULL;
        loader->app.thread = NULL;
    } else {
        furi_thread_free(loader->app.thread);
        loader->app.thread = NULL;
    }

    furi_string_free(loader->app.launch_path);
    loader->app.launch_path = NULL;
    furi_kernel_unlock();

    FURI_LOG_I(TAG, "Application stopped. Free heap: %zu", memmgr_get_free_heap());

    LoaderEvent event;
    event.type = LoaderEventTypeApplicationStopped;
    furi_pubsub_publish(loader->pubsub, &event);

    loader_do_next_deferred_launch_if_available(loader);

    heap_alloc_guard_lock();
    loader_do_menu_show_settings_if_pending(loader);
    heap_alloc_guard_unlock();
}

static bool loader_is_application_running(Loader* loader) {
    FuriThread* app_thread = loader->app.thread;
    return app_thread && (app_thread != (FuriThread*)LOADER_MAGIC_THREAD_VALUE);
}

static bool loader_do_signal(Loader* loader, uint32_t signal, void* arg) {
    if(loader_is_application_running(loader)) {
        return furi_thread_signal(loader->app.thread, signal, arg);
    }

    return false;
}

static bool loader_do_get_application_name(Loader* loader, FuriString* name) {
    if(loader_is_application_running(loader)) {
        furi_string_set(name, furi_thread_get_name(furi_thread_get_id(loader->app.thread)));
        return true;
    }

    return false;
}

static bool loader_do_get_application_id(Loader* loader, FuriString* appid) {
    if(loader_is_application_running(loader)) {
        const char* id = furi_thread_get_appid(furi_thread_get_id(loader->app.thread));
        if(id) {
            furi_string_set(appid, id);
            return true;
        }
    }

    return false;
}

static bool loader_do_get_application_launch_path(Loader* loader, FuriString* path) {
    if(loader_is_application_running(loader)) {
        furi_string_set(path, loader->app.launch_path);
        return true;
    }

    return false;
}

int32_t loader_srv(void* p) {
    UNUSED(p);
    Loader* loader = loader_alloc();
    heap_alloc_guard_init();
    subghz_devices_preinit();
    furi_record_create(RECORD_LOADER, loader);

    FURI_LOG_I(TAG, "Executing system start hooks");
    for(size_t i = 0; i < FLIPPER_ON_SYSTEM_START_COUNT; i++) {
        FLIPPER_ON_SYSTEM_START[i]();
    }

    /* Anti-fragmentation: build the main menu ONCE here, at boot, and keep it
     * for the firmware lifetime (see loader_do_menu_closed /
     * loader_do_release_hidden_menu, which no longer free it). Building it now
     * - before any app has loaded and churned the heap - clusters the menu's
     * thread/view/Menu/Submenu/item allocations LOW, just above the permanent
     * boot region, leaving the entire upper heap as one contiguous tail.
     *
     * The old behaviour built the menu lazily (on qFlipper/CLI connect or on
     * menu-show) and freed it on every menu close and qFlipper disconnect.
     * Because those builds happened high in an already-churned heap and were
     * then freed, each cycle stranded a small survivor block high up, splitting
     * the free tail and shrinking the max contiguous block over a session
     * (eventually OOMing Mass Storage). Building once at boot and keeping it
     * removes that churn entirely. We trade a few KB of permanently-resident
     * RAM for a stable, large contiguous tail. The menu thread simply parks
     * waiting for a show flag; it does not attach to the GUI until shown. */
    loader_wait_for_storage_ready();
    heap_alloc_guard_lock();
    loader_do_ensure_menu_built(loader);
    heap_alloc_guard_unlock();

    if((furi_hal_rtc_get_boot_mode() == FuriHalRtcBootModeNormal) && FLIPPER_AUTORUN_APP_NAME &&
       strlen(FLIPPER_AUTORUN_APP_NAME)) {
        FURI_LOG_I(TAG, "Starting autorun app: %s", FLIPPER_AUTORUN_APP_NAME);
        loader_do_start_by_name(loader, FLIPPER_AUTORUN_APP_NAME, NULL, NULL);
    }

    LoaderMessage message;
    while(true) {
        if(furi_message_queue_get(loader->queue, &message, FuriWaitForever) == FuriStatusOk) {
            switch(message.type) {
            case LoaderMessageTypeStartByName: {
                loader_wait_for_storage_ready();
                loader_show_loading_for_launch(loader, message.start.name);
                LoaderMessageLoaderStatusResult status = loader_do_start_by_name(
                    loader,
                    message.start.name,
                    message.start.args,
                    message.start.error_message);
                loader_hide_loading_for_launch(loader);
                *(message.status_value) = status;
                if(status.value != LoaderStatusOk) loader_do_emit_queue_empty_event(loader);
                api_lock_unlock(message.api_lock);
                break;
            }
            case LoaderMessageTypeStartByNameDetachedWithGuiError: {
                FuriString* error_message = furi_string_alloc();
                loader_wait_for_storage_ready();
                loader_show_loading_for_launch(loader, message.start.name);
                LoaderMessageLoaderStatusResult status = loader_do_start_by_name(
                    loader, message.start.name, message.start.args, error_message);
                loader_hide_loading_for_launch(loader);
                loader_show_gui_error(status, message.start.name, error_message);
                if(status.value != LoaderStatusOk) loader_do_emit_queue_empty_event(loader);
                if(message.start.name) free((void*)message.start.name);
                if(message.start.args) free((void*)message.start.args);
                furi_string_free(error_message);
                break;
            }
            case LoaderMessageTypeShowMenu:
                loader_wait_for_storage_ready();
                heap_alloc_guard_lock();
                loader_do_menu_show(loader);
                heap_alloc_guard_unlock();
                break;
            case LoaderMessageTypeMenuClosed:
                heap_alloc_guard_lock();
                loader_do_menu_closed(loader);
                heap_alloc_guard_unlock();
                break;
            case LoaderMessageTypeEnsureMenuBuilt:
                loader_wait_for_storage_ready();
                heap_alloc_guard_lock();
                loader_do_ensure_menu_built(loader);
                heap_alloc_guard_unlock();
                api_lock_unlock(message.api_lock);
                break;
            case LoaderMessageTypeReleaseHiddenMenu:
                heap_alloc_guard_lock();
                loader_do_release_hidden_menu(loader);
                heap_alloc_guard_unlock();
                break;
            case LoaderMessageTypeIsLocked:
                message.bool_value->value = loader_do_is_locked(loader);
                api_lock_unlock(message.api_lock);
                break;
            case LoaderMessageTypeAppClosed:
                loader_do_app_closed(loader);
                break;
            case LoaderMessageTypeLock:
                message.bool_value->value = loader_do_lock(loader);
                api_lock_unlock(message.api_lock);
                break;
            case LoaderMessageTypeUnlock:
                loader_do_unlock(loader);
                break;
            case LoaderMessageTypeApplicationsClosed:
                loader_do_applications_closed(loader);
                break;
            case LoaderMessageTypeSignal:
                message.bool_value->value =
                    loader_do_signal(loader, message.signal.signal, message.signal.arg);
                api_lock_unlock(message.api_lock);
                break;
            case LoaderMessageTypeGetApplicationName:
                message.bool_value->value =
                    loader_do_get_application_name(loader, message.application_name);
                api_lock_unlock(message.api_lock);
                break;
            case LoaderMessageTypeGetApplicationId:
                message.bool_value->value =
                    loader_do_get_application_id(loader, message.application_name);
                api_lock_unlock(message.api_lock);
                break;
            case LoaderMessageTypeGetApplicationLaunchPath:
                message.bool_value->value =
                    loader_do_get_application_launch_path(loader, message.application_name);
                api_lock_unlock(message.api_lock);
                break;
            case LoaderMessageTypeEnqueueLaunch:
                furi_check(loader_queue_push(&loader->launch_queue, &message.defer_start));
                loader_menu_take_settings_return();

                if(message.defer_start.name_or_path) {
                    const char* p = message.defer_start.name_or_path;
                    bool is_fap = strstr(p, "subghz_frequency") ||
                                  strstr(p, "subghz_modulation") ||
                                  strstr(p, "subghz_raw");

                    bool is_subghz = strcmp(p, "subghz") == 0;
                    bool is_keeloq_tool = strstr(p, "subghz_keeloq_tool") != NULL;
                    if(is_fap) {
                        view_holder_set_view(
                            loader->view_holder,
                            empty_screen_get_view(loader->empty_screen));
                        view_holder_send_to_front(loader->view_holder);
                    } else if(is_subghz || is_keeloq_tool) {
                        view_holder_set_view(
                            loader->view_holder,
                            loading_get_view(loader->loading));
                        view_holder_send_to_front(loader->view_holder);
                    }
                }
                api_lock_unlock(message.api_lock);
                break;
            case LoaderMessageTypeClearLaunchQueue:
                loader_queue_clear(&loader->launch_queue);
                api_lock_unlock(message.api_lock);
                break;
            }
        }
    }

    return 0;
}
