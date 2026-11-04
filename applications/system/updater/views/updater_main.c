#include <gui/gui_i.h>
#include <gui/view.h>
#include <gui/elements.h>
#include <gui/canvas.h>
#include <assets_icons.h>
#include <furi.h>
#include <input/input.h>
#include <string.h>
#include <toolbox/version.h>

#include "../updater_i.h"
#include "updater_main.h"

struct UpdaterMainView {
    View* view;
    ViewDispatcher* view_dispatcher;
    FuriPubSubSubscription* subscription;
    void* context;
    FuriTimer* spin_timer;
};

static const uint8_t PROGRESS_RENDER_STEP = 1; /* percent, to limit rendering rate */

// Same comet-tail spinner as gui/modules/loading.c (the "please wait" wheel
// used everywhere else in the firmware while an app loads), re-centered and
// reused here instead of a bespoke rotating icon - see that file for the
// full explanation of why a timer-driven with_view_model(..., true) is the
// correct way to animate under both ViewHolder and ViewDispatcher. Radius
// scaled down from loading.c's 11 to 9 (with the diagonal points scaled to
// match) so the largest disc (radius 3) still clears the page icon's inner
// edge instead of touching it. Interval doubled from loading.c's 50ms to
// 100ms - an update takes far longer than the quick app-load spinner was
// designed for, and spinning at the normal speed looked frantic over that
// stretch.
#define UPDATER_SPIN_INTERVAL_MS 100u
static const int8_t updater_spin_dx[8] = {0, 5, 7, 5, 0, -5, -7, -5};
static const int8_t updater_spin_dy[8] = {-7, -5, 0, 5, 7, 5, 0, -5};

typedef struct {
    FuriString* status;
    uint8_t progress, rendered_progress;
    bool failed;
    uint8_t spin_frame;
} UpdaterProgressModel;

void updater_main_model_set_state(
    UpdaterMainView* main_view,
    const char* message,
    uint8_t progress,
    bool failed) {
    bool update = false;
    with_view_model(
        main_view->view,
        UpdaterProgressModel * model,
        {
            model->failed = failed;
            model->progress = progress;
            if(furi_string_cmp_str(model->status, message)) {
                furi_string_set(model->status, message);
                model->rendered_progress = progress;
                update = true;
            } else if(
                (model->rendered_progress > progress) ||
                ((progress - model->rendered_progress) > PROGRESS_RENDER_STEP)) {
                model->rendered_progress = progress;
                update = true;
            }
        },
        update);
}

View* updater_main_get_view(UpdaterMainView* main_view) {
    furi_assert(main_view);
    return main_view->view;
}

bool updater_main_input(InputEvent* event, void* context) {
    furi_assert(event);
    furi_assert(context);

    UpdaterMainView* main_view = context;
    if(!main_view->view_dispatcher) {
        return true;
    }

    if((event->type == InputTypeShort) && (event->key == InputKeyOk)) {
        view_dispatcher_send_custom_event(
            main_view->view_dispatcher, UpdaterCustomEventRetryUpdate);
    } else if((event->type == InputTypeLong) && (event->key == InputKeyBack)) {
        view_dispatcher_send_custom_event(
            main_view->view_dispatcher, UpdaterCustomEventCancelUpdate);
    }

    return true;
}

// Splits the version line into "FoxFW " (bold) and "(v2.0.4)" (not bold) so
// the caller can render each in a different font - the full line doesn't
// fit on screen at all-bold width. Built from firmware.ver via
// toolbox/version.h, so this never needs manual edits on release -
// version_get_firmware_origin() is already correctly-cased ("FoxFW"),
// version_get_version() returns the dist-suffixed tag (e.g. "foxfw-v2.0.4");
// strip the known "foxfw-" prefix to isolate the bare version.
static void updater_build_version_line(
    char* origin_out,
    size_t origin_out_size,
    char* tag_out,
    size_t tag_out_size) {
    const Version* ver = version_get();
    const char* origin = version_get_firmware_origin(ver);
    const char* tag = version_get_version(ver);

    const char* prefix = "foxfw-";
    size_t prefix_len = strlen(prefix);
    if(strncmp(tag, prefix, prefix_len) == 0) {
        tag += prefix_len;
    }

    // Drop a leading "v" so the version shows as "2.1", not "v2.1".
    if(tag[0] == 'v' || tag[0] == 'V') {
        tag += 1;
    }

    // The RAM-resident updater stage (FURI_RAM_EXEC, active while flash is
    // actually being written) appends " (RAM)" to the version string - strip
    // it here so this screen doesn't briefly overflow off-screen with
    // "(v2.0.4 (RAM))" before settling back to the normal "(v2.0.4)".
    char tag_buf[24];
    strlcpy(tag_buf, tag, sizeof(tag_buf));
    const char* ram_suffix = " (RAM)";
    size_t tag_len = strlen(tag_buf);
    size_t ram_suffix_len = strlen(ram_suffix);
    if(tag_len >= ram_suffix_len &&
       strcmp(tag_buf + tag_len - ram_suffix_len, ram_suffix) == 0) {
        tag_buf[tag_len - ram_suffix_len] = '\0';
    }

    snprintf(origin_out, origin_out_size, "%s ", origin);

    strlcpy(tag_out, tag_buf, tag_out_size);
}

static void updater_main_spin_timer_callback(void* context) {
    UpdaterMainView* main_view = context;
    with_view_model(
        main_view->view,
        UpdaterProgressModel * model,
        { model->spin_frame = (uint8_t)((model->spin_frame + 1u) % 8u); },
        true);
}

static void updater_main_draw_spinner(Canvas* canvas, uint8_t frame) {
    // Centered over the blank circle left in I_Updating_Page_32x40 (drawn at
    // x=4, y=5) where that icon's old baked-in gear graphic used to be.
    const uint8_t cx = 20;
    const uint8_t cy = 29;
    canvas_set_color(canvas, ColorBlack);
    for(uint8_t i = 0; i < 8; i++) {
        uint8_t x = (uint8_t)(cx + updater_spin_dx[i]);
        uint8_t y = (uint8_t)(cy + updater_spin_dy[i]);
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

static void updater_main_draw_callback(Canvas* canvas, void* _model) {
    UpdaterProgressModel* model = _model;

    canvas_set_font(canvas, FontPrimary);

    if(model->failed) {
        canvas_draw_icon(canvas, 2, 22, &I_Warning_30x23);
        canvas_draw_str_aligned(canvas, 40, 9, AlignLeft, AlignTop, "Update Failed!");
        canvas_set_font(canvas, FontSecondary);

        elements_multiline_text_aligned(
            canvas, 75, 26, AlignCenter, AlignTop, furi_string_get_cstr(model->status));

        canvas_draw_str_aligned(
            canvas, 18, 55, AlignLeft, AlignTop, "to retry, hold       to abort");
        canvas_draw_icon(canvas, 7, 54, &I_Ok_btn_9x9);
        canvas_draw_icon(canvas, 75, 55, &I_Pin_back_arrow_10x8);
    } else {
        canvas_draw_str_aligned(canvas, 55, 6, AlignLeft, AlignTop, "Installing");
        // "FoxFW" and "(v2.0.4)" both centered on the same x as "Installing"
        // above - derived from its actual drawn position/width rather than
        // a second hardcoded x, so the three lines always share one center
        // even if "Installing" itself ever moves.
        uint16_t installing_width = canvas_string_width(canvas, "Installing");
        int32_t text_center_x = 55 + installing_width / 2;

        char origin_part[16] = {0};
        char tag_part[24] = {0};
        updater_build_version_line(origin_part, sizeof(origin_part), tag_part, sizeof(tag_part));
        // updater_build_version_line() appends a trailing space to
        // origin_part for the old same-line "FoxFW (v2.0.4)" layout - strip
        // it here, or it'd skew this centered, standalone line left.
        size_t origin_len = strlen(origin_part);
        if(origin_len > 0 && origin_part[origin_len - 1] == ' ') {
            origin_part[origin_len - 1] = '\0';
        }

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, text_center_x, 17, AlignCenter, AlignTop, origin_part);
        canvas_set_font(canvas, FontSecondary);
        // Prefix the bare tag with "Version " so the line reads e.g. "Version 2.1".
        char version_line[32];
        snprintf(version_line, sizeof(version_line), "Version %s", tag_part);
        canvas_draw_str_aligned(canvas, text_center_x, 29, AlignCenter, AlignTop, version_line);

        // Same progress number the bar below already shows, e.g.
        // "Extracting resources 34%" - matches Momentum's updater style.
        // This is always the current stage's own percentage (0-100 within
        // whatever step is running now), not overall install progress -
        // see update_task_set_progress()'s own comment for why.
        char status_line[48];
        snprintf(
            status_line,
            sizeof(status_line),
            "%s %u%%",
            furi_string_get_cstr(model->status),
            (unsigned)model->progress);
        canvas_draw_str_aligned(canvas, 64, 51, AlignCenter, AlignTop, status_line);
        canvas_draw_icon(canvas, 4, 5, &I_Updating_Page_32x40);
        updater_main_draw_spinner(canvas, model->spin_frame);
        // Icon bottom edge (y=5+40=45) lines up with the middle of the bar
        // (9px tall, so its middle row is bar_y+4) instead of the old
        // bar_y=36, where the bar's bottom - not its middle - lined up with
        // the icon's bottom.
        elements_progress_bar(canvas, 42, 40, 80, (float)model->progress / 100);
    }
}

UpdaterMainView* updater_main_alloc(void) {
    UpdaterMainView* main_view = malloc(sizeof(UpdaterMainView));

    main_view->view = view_alloc();
    view_allocate_model(main_view->view, ViewModelTypeLocking, sizeof(UpdaterProgressModel));

    with_view_model(
        main_view->view,
        UpdaterProgressModel * model,
        {
            model->status = furi_string_alloc_set("Waiting for SD card");
            model->progress = 0;
            model->rendered_progress = 0;
            model->failed = false;
            model->spin_frame = 0;
        },
        true);

    view_set_context(main_view->view, main_view);
    view_set_input_callback(main_view->view, updater_main_input);
    view_set_draw_callback(main_view->view, updater_main_draw_callback);

    // Same comet-tail spinner as the rest of the firmware's loading screens
    // - own timer, independent of how often progress events redraw the rest
    // of the screen.
    main_view->spin_timer =
        furi_timer_alloc(updater_main_spin_timer_callback, FuriTimerTypePeriodic, main_view);
    furi_timer_start(main_view->spin_timer, furi_ms_to_ticks(UPDATER_SPIN_INTERVAL_MS));

    return main_view;
}

void updater_main_free(UpdaterMainView* main_view) {
    furi_assert(main_view);
    furi_timer_stop(main_view->spin_timer);
    furi_timer_free(main_view->spin_timer);
    with_view_model(
        main_view->view,
        UpdaterProgressModel * model,
        { furi_string_free(model->status); },
        false);
    view_free(main_view->view);
    free(main_view);
}

void updater_main_set_storage_pubsub(UpdaterMainView* main_view, FuriPubSubSubscription* sub) {
    main_view->subscription = sub;
}

FuriPubSubSubscription* updater_main_get_storage_pubsub(UpdaterMainView* main_view) {
    return main_view->subscription;
}

void updater_main_set_view_dispatcher(UpdaterMainView* main_view, ViewDispatcher* view_dispatcher) {
    main_view->view_dispatcher = view_dispatcher;
}
