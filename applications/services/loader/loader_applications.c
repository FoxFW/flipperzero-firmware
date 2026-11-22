#include "loader.h"
#include <core/kernel.h>
#include "loader_applications.h"
#include <dialogs/dialogs.h>
#include <flipper_application/flipper_application.h>
#include <assets_icons.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_holder.h>
#include <gui/modules/loading.h>
#include <lib/toolbox/path.h>
#include <toolbox/heap_alloc_guard.h>
#include <gui/icon.h>
#include <furi_hal.h>

#define TAG "LoaderApplications"

#define LOADER_APPLICATIONS_WATCHDOG_TIMEOUT_MS 7000
#define LOADER_APPLICATIONS_STILL_LOADING_WAIT_MS 5000
#define LOADER_APPLICATIONS_STILL_LOADING_SPIN_INTERVAL_MS 50
#define LOADER_APPLICATIONS_STILL_LOADING_BAR_H 16

struct LoaderApplications {
    FuriThread* thread;
    void (*closed_cb)(void*);
    void* context;
};

static int32_t loader_applications_thread(void* p);

LoaderApplications* loader_applications_alloc(void (*closed_cb)(void*), void* context) {
    LoaderApplications* loader_applications = malloc(sizeof(LoaderApplications));
    loader_applications->thread =
        furi_thread_alloc_ex(TAG, 768, loader_applications_thread, (void*)loader_applications);
    loader_applications->closed_cb = closed_cb;
    loader_applications->context = context;
    furi_thread_start(loader_applications->thread);
    return loader_applications;
}

void loader_applications_free(LoaderApplications* loader_applications) {
    furi_assert(loader_applications);
    furi_thread_join(loader_applications->thread);
    furi_kernel_lock();
    furi_thread_free(loader_applications->thread);
    free(loader_applications);
    furi_kernel_unlock();
}

typedef struct {
    FuriString* file_path;
    DialogsApp* dialogs;
    Storage* storage;
    Loader* loader;

    Gui* gui;
    ViewHolder* view_holder;
    Loading* loading;

    FuriTimer* load_watchdog;
    uint32_t load_watchdog_started_tick;
    FuriString* load_watchdog_app_name;
    View* still_loading_view;
    FuriTimer* still_loading_wait_timer;
    FuriTimer* still_loading_spin_timer;
} LoaderApplicationsApp;

typedef struct {
    uint8_t spin_frame;
    bool buttons_visible;
    bool show_wait_button;
    bool focus_left;
} LoaderApplicationsStillLoadingModel;

static const int8_t loader_applications_still_loading_spin_dx[8] =
    {0, 8, 11, 8, 0, -8, -11, -8};
static const int8_t loader_applications_still_loading_spin_dy[8] =
    {-11, -8, 0, 8, 11, 8, 0, -8};

static void loader_applications_still_loading_draw_spinner(Canvas* canvas, uint8_t frame) {
    canvas_set_color(canvas, ColorBlack);
    for(uint8_t i = 0; i < 8; i++) {
        int32_t x = 64 + loader_applications_still_loading_spin_dx[i];
        int32_t y = 32 + loader_applications_still_loading_spin_dy[i];
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

static void loader_applications_still_loading_draw_two_buttons(
    Canvas* canvas, bool focus_left, const char* left_label, const char* right_label) {
    int32_t bar_y = 64 - LOADER_APPLICATIONS_STILL_LOADING_BAR_H;
    int32_t btn_gap = 4;
    int32_t btn_w = (128 - btn_gap * 3) / 2;
    int32_t left_x = btn_gap;
    int32_t right_x = btn_gap * 2 + btn_w;

    canvas_set_color(canvas, ColorBlack);
    if(focus_left) {
        canvas_draw_rbox(
            canvas, left_x, bar_y, btn_w, LOADER_APPLICATIONS_STILL_LOADING_BAR_H, 3);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str_aligned(
            canvas,
            left_x + btn_w / 2,
            bar_y + LOADER_APPLICATIONS_STILL_LOADING_BAR_H / 2,
            AlignCenter,
            AlignCenter,
            left_label);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rframe(
            canvas, right_x, bar_y, btn_w, LOADER_APPLICATIONS_STILL_LOADING_BAR_H, 3);
        canvas_draw_str_aligned(
            canvas,
            right_x + btn_w / 2,
            bar_y + LOADER_APPLICATIONS_STILL_LOADING_BAR_H / 2,
            AlignCenter,
            AlignCenter,
            right_label);
    } else {
        canvas_draw_rframe(
            canvas, left_x, bar_y, btn_w, LOADER_APPLICATIONS_STILL_LOADING_BAR_H, 3);
        canvas_draw_str_aligned(
            canvas,
            left_x + btn_w / 2,
            bar_y + LOADER_APPLICATIONS_STILL_LOADING_BAR_H / 2,
            AlignCenter,
            AlignCenter,
            left_label);
        canvas_draw_rbox(
            canvas, right_x, bar_y, btn_w, LOADER_APPLICATIONS_STILL_LOADING_BAR_H, 3);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str_aligned(
            canvas,
            right_x + btn_w / 2,
            bar_y + LOADER_APPLICATIONS_STILL_LOADING_BAR_H / 2,
            AlignCenter,
            AlignCenter,
            right_label);
        canvas_set_color(canvas, ColorBlack);
    }
}

static void
    loader_applications_still_loading_draw_one_button(Canvas* canvas, const char* label) {
    int32_t bar_y = 64 - LOADER_APPLICATIONS_STILL_LOADING_BAR_H;
    const Icon* icon = &I_ButtonCenter_7x7;
    int32_t icon_w = icon_get_width(icon);
    int32_t icon_h = icon_get_height(icon);
    int32_t icon_gap = 3;
    int32_t pad_x = 10;
    int32_t content_w = icon_w + icon_gap + (int32_t)canvas_string_width(canvas, label);
    int32_t btn_w = content_w + pad_x * 2;
    int32_t x = (128 - btn_w) / 2;

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rbox(canvas, x, bar_y, btn_w, LOADER_APPLICATIONS_STILL_LOADING_BAR_H, 3);
    canvas_set_color(canvas, ColorWhite);

    int32_t gx = x + (btn_w - content_w) / 2;
    int32_t gy_icon = bar_y + (LOADER_APPLICATIONS_STILL_LOADING_BAR_H - icon_h) / 2;
    canvas_draw_icon(canvas, gx, gy_icon, icon);
    canvas_draw_str_aligned(
        canvas,
        gx + icon_w + icon_gap,
        bar_y + LOADER_APPLICATIONS_STILL_LOADING_BAR_H / 2,
        AlignLeft,
        AlignCenter,
        label);

    canvas_set_color(canvas, ColorBlack);
}

static void loader_applications_still_loading_draw_callback(Canvas* canvas, void* model) {
    LoaderApplicationsStillLoadingModel* m = model;
    canvas_clear(canvas);
    loader_applications_still_loading_draw_spinner(canvas, m->spin_frame);

    if(!m->buttons_visible) {
        return;
    }

    if(m->show_wait_button) {
        loader_applications_still_loading_draw_two_buttons(
            canvas, m->focus_left, "Restart", "Wait");
    } else {
        loader_applications_still_loading_draw_one_button(canvas, "Restart");
    }
}

static void loader_applications_still_loading_spin_timer_callback(void* context) {
    LoaderApplicationsApp* app = context;
    with_view_model(
        app->still_loading_view,
        LoaderApplicationsStillLoadingModel* model,
        { model->spin_frame = (uint8_t)((model->spin_frame + 1u) % 8u); },
        true);
}

static void loader_applications_still_loading_wait_timer_callback(void* context) {
    LoaderApplicationsApp* app = context;
    with_view_model(
        app->still_loading_view,
        LoaderApplicationsStillLoadingModel* model,
        {
            model->buttons_visible = true;
            model->show_wait_button = false;
        },
        true);
}

static void loader_applications_still_loading_enter_callback(void* context) {
    LoaderApplicationsApp* app = context;
    with_view_model(
        app->still_loading_view,
        LoaderApplicationsStillLoadingModel* model,
        {
            model->spin_frame = 0;
            model->buttons_visible = true;
            model->show_wait_button = true;
            model->focus_left = false;
        },
        false);
    furi_timer_start(
        app->still_loading_spin_timer,
        furi_ms_to_ticks(LOADER_APPLICATIONS_STILL_LOADING_SPIN_INTERVAL_MS));
}

static void loader_applications_still_loading_exit_callback(void* context) {
    LoaderApplicationsApp* app = context;
    furi_timer_stop(app->still_loading_spin_timer);
    furi_timer_stop(app->still_loading_wait_timer);
}

static bool
    loader_applications_still_loading_input_callback(InputEvent* event, void* context) {
    LoaderApplicationsApp* app = context;
    if(event->type != InputTypeShort) {
        return true;
    }

    bool do_reset = false;
    bool start_wait_timer = false;

    with_view_model(
        app->still_loading_view,
        LoaderApplicationsStillLoadingModel* model,
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
            app->still_loading_wait_timer,
            furi_ms_to_ticks(LOADER_APPLICATIONS_STILL_LOADING_WAIT_MS));
    }

    if(do_reset) {
        furi_hal_power_reset();
    }

    return true;
}

static void loader_applications_watchdog_callback(void* context) {
    LoaderApplicationsApp* app = context;
    uint32_t elapsed = furi_get_tick() - app->load_watchdog_started_tick;
    FURI_LOG_E(
        TAG,
        "Loading spinner still visible after %lums for \"%s\" - switching to still-loading "
        "screen",
        (unsigned long)elapsed,
        furi_string_get_cstr(app->load_watchdog_app_name));
    view_holder_set_view(app->view_holder, app->still_loading_view);
}

static void loader_applications_watchdog_arm(LoaderApplicationsApp* app, const char* name) {
    furi_string_set(app->load_watchdog_app_name, name ? name : "?");
    app->load_watchdog_started_tick = furi_get_tick();
    furi_timer_start(
        app->load_watchdog, furi_ms_to_ticks(LOADER_APPLICATIONS_WATCHDOG_TIMEOUT_MS));
}

static void loader_applications_watchdog_disarm(LoaderApplicationsApp* app) {
    furi_timer_stop(app->load_watchdog);
}

static LoaderApplicationsApp* loader_applications_app_alloc(void) {
    LoaderApplicationsApp* app = malloc(sizeof(LoaderApplicationsApp));
    app->file_path = furi_string_alloc_set(EXT_PATH("apps"));
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->loader = furi_record_open(RECORD_LOADER);

    app->gui = furi_record_open(RECORD_GUI);
    app->view_holder = view_holder_alloc();
    app->loading = loading_alloc();

    view_holder_attach_to_gui(app->view_holder, app->gui);

    app->load_watchdog_app_name = furi_string_alloc();
    app->load_watchdog = furi_timer_alloc(
        loader_applications_watchdog_callback, FuriTimerTypeOnce, app);
    app->still_loading_wait_timer = furi_timer_alloc(
        loader_applications_still_loading_wait_timer_callback, FuriTimerTypeOnce, app);
    app->still_loading_spin_timer = furi_timer_alloc(
        loader_applications_still_loading_spin_timer_callback, FuriTimerTypePeriodic, app);
    app->still_loading_view = view_alloc();
    view_allocate_model(
        app->still_loading_view,
        ViewModelTypeLocking,
        sizeof(LoaderApplicationsStillLoadingModel));
    view_set_draw_callback(
        app->still_loading_view, loader_applications_still_loading_draw_callback);
    view_set_input_callback(
        app->still_loading_view, loader_applications_still_loading_input_callback);
    view_set_enter_callback(
        app->still_loading_view, loader_applications_still_loading_enter_callback);
    view_set_exit_callback(
        app->still_loading_view, loader_applications_still_loading_exit_callback);
    view_set_context(app->still_loading_view, app);

    return app;
}

static void loader_applications_app_free(LoaderApplicationsApp* app) {
    furi_assert(app);

    furi_timer_free(app->load_watchdog);
    furi_timer_free(app->still_loading_wait_timer);
    furi_timer_free(app->still_loading_spin_timer);
    furi_string_free(app->load_watchdog_app_name);

    view_holder_set_view(app->view_holder, NULL);
    view_free(app->still_loading_view);
    view_holder_free(app->view_holder);
    loading_free(app->loading);
    furi_record_close(RECORD_GUI);

    furi_record_close(RECORD_LOADER);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    furi_string_free(app->file_path);
    free(app);
}

static bool loader_applications_item_callback(
    FuriString* path,
    void* context,
    uint8_t** icon_ptr,
    FuriString* item_name) {
    LoaderApplicationsApp* loader_applications_app = context;
    furi_assert(loader_applications_app);
    return flipper_application_load_name_and_icon(
        path, loader_applications_app->storage, icon_ptr, item_name);
}

static bool loader_applications_select_app(LoaderApplicationsApp* loader_applications_app) {
    const DialogsFileBrowserOptions browser_options = {
        .extension = ".fap",
        .skip_assets = true,
        .icon = &I_unknown_10px,
        .hide_ext = true,
        .item_loader_callback = loader_applications_item_callback,
        .item_loader_context = loader_applications_app,
        .base_path = EXT_PATH("apps"),
        .sort_by_name = true,
    };

    return dialog_file_browser_show(
        loader_applications_app->dialogs,
        loader_applications_app->file_path,
        loader_applications_app->file_path,
        &browser_options);
}

#define APPLICATION_STOP_EVENT 1

static void loader_pubsub_callback(const void* message, void* context) {
    const LoaderEvent* event = message;
    const FuriThreadId thread_id = (FuriThreadId)context;

    if(event->type == LoaderEventTypeNoMoreAppsInQueue) {
        furi_thread_flags_set(thread_id, APPLICATION_STOP_EVENT);
    }
}

static void
    loader_applications_start_app(LoaderApplicationsApp* app, const char* name, const char* args) {

    FuriThreadId thread_id = furi_thread_get_current_id();
    FuriPubSubSubscription* subscription =
        furi_pubsub_subscribe(loader_get_pubsub(app->loader), loader_pubsub_callback, thread_id);

    loader_applications_watchdog_arm(app, name);
    LoaderStatus status = loader_start_with_gui_error(app->loader, name, args);
    loader_applications_watchdog_disarm(app);

    if(status == LoaderStatusOk) {
        furi_thread_flags_wait(APPLICATION_STOP_EVENT, FuriFlagWaitAny, FuriWaitForever);
    }

    furi_pubsub_unsubscribe(loader_get_pubsub(app->loader), subscription);
    furi_thread_flags_clear(APPLICATION_STOP_EVENT);
}

static int32_t loader_applications_thread(void* p) {
    LoaderApplications* loader_applications = p;
    heap_alloc_guard_lock();
    LoaderApplicationsApp* app = loader_applications_app_alloc();
    heap_alloc_guard_unlock();

    view_holder_set_view(app->view_holder, loading_get_view(app->loading));

    while(loader_applications_select_app(app)) {
        loader_applications_start_app(app, furi_string_get_cstr(app->file_path), NULL);
    }

    view_holder_set_view(app->view_holder, NULL);

    heap_alloc_guard_lock();
    loader_applications_app_free(app);
    heap_alloc_guard_unlock();

    if(loader_applications->closed_cb) {
        loader_applications->closed_cb(loader_applications->context);
    }

    return 0;
}
