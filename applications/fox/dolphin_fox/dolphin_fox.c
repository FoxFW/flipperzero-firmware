#include "dolphin_fox.h"

#include <input/input.h>
#include <flipper_format/flipper_format.h>

static void df_settings_load(DfApp* app) {
    app->mood = 0;
    app->level = DF_LEVEL_MIN;

    FlipperFormat* ff = flipper_format_file_alloc(app->storage);
    FuriString* type = furi_string_alloc();
    uint32_t ver = 0;
    uint32_t value = 0;
    do {
        if(!flipper_format_file_open_existing(ff, DF_CONF_PATH)) break;
        if(!flipper_format_read_header(ff, type, &ver)) break;
        if(flipper_format_read_uint32(ff, "Mood", &value, 1) && value < DF_MOOD_COUNT) {
            app->mood = (uint8_t)value;
        }
        if(flipper_format_read_uint32(ff, "Level", &value, 1)) {
            if(value < DF_LEVEL_MIN) value = DF_LEVEL_MIN;
            if(value > DF_LEVEL_MAX) value = DF_LEVEL_MAX;
            app->level = (uint8_t)value;
        }
    } while(0);
    furi_string_free(type);
    flipper_format_free(ff);
}

static void df_settings_save(DfApp* app) {
    storage_simply_mkdir(app->storage, DF_DATA_DIR);

    FlipperFormat* ff = flipper_format_file_alloc(app->storage);
    do {
        if(!flipper_format_file_open_always(ff, DF_CONF_PATH)) break;
        if(!flipper_format_write_header_cstr(ff, "Dolphin Fox Settings", 1)) break;
        uint32_t value = app->mood;
        flipper_format_write_uint32(ff, "Mood", &value, 1);
        value = app->level;
        flipper_format_write_uint32(ff, "Level", &value, 1);
    } while(0);
    flipper_format_free(ff);
}

static void df_show_anim(DfApp* app, int index) {
    furi_timer_stop(app->frame_timer);
    df_player_unload(app);
    app->screen = DfScreenAnim;

    if(app->anim_count == 0) return;

    int count = (int)app->anim_count;
    bool ok = false;
    for(int tries = 0; tries < count; tries++) {
        int i = ((index % count) + count) % count;
        if(df_player_load(app, i)) {
            app->current = i;
            ok = true;
            break;
        }
        index = i + 1;
    }

    if(ok && app->frame_period_ms > 0) {
        furi_timer_start(app->frame_timer, furi_ms_to_ticks(app->frame_period_ms));
    }
}

static void df_goto_card(DfApp* app) {
    furi_timer_stop(app->frame_timer);
    app->screen = DfScreenCard;
    app->sel = DfSelNone;
}

static void df_return_to_anim(DfApp* app) {
    int idx = df_library_pick(app);
    if(idx < 0) idx = (app->current >= 0) ? app->current : 0;
    df_show_anim(app, idx);
}

static void df_card_input(DfApp* app, InputKey key, bool is_short) {
    switch(key) {
    case InputKeyBack:
        if(is_short) app->running = false;
        break;
    case InputKeyDown:
        if(is_short) {
            app->sel = (app->sel == DfSelNone) ? DfSelMood :
                       (app->sel == DfSelMood) ? DfSelLevel :
                                                 DfSelNone;
        }
        break;
    case InputKeyUp:
        if(is_short) {
            app->sel = (app->sel == DfSelNone)  ? DfSelLevel :
                       (app->sel == DfSelLevel) ? DfSelMood :
                                                  DfSelNone;
        }
        break;
    case InputKeyLeft:
        if(app->sel == DfSelNone) {
            if(is_short) df_return_to_anim(app);
        } else if(app->sel == DfSelMood) {
            app->mood = (uint8_t)((app->mood + DF_MOOD_COUNT - 1) % DF_MOOD_COUNT);
        } else {
            if(app->level > DF_LEVEL_MIN) app->level--;
        }
        break;
    case InputKeyRight:
        if(app->sel == DfSelNone) {
            if(is_short) df_return_to_anim(app);
        } else if(app->sel == DfSelMood) {
            app->mood = (uint8_t)((app->mood + 1) % DF_MOOD_COUNT);
        } else {
            if(app->level < DF_LEVEL_MAX) app->level++;
        }
        break;
    case InputKeyOk:
        if(is_short) {
            if(app->sel == DfSelNone) {
                df_return_to_anim(app);
            } else {
                app->sel = DfSelNone;
            }
        }
        break;
    default:
        break;
    }
}

static void df_handle_input(DfApp* app, InputEvent* event) {
    bool is_short = (event->type == InputTypeShort);
    bool is_repeat = (event->type == InputTypeRepeat);

    if(app->screen == DfScreenAnim) {
        if(!is_short) return;
        switch(event->key) {
        case InputKeyRight:
            df_show_anim(app, app->current + 1);
            break;
        case InputKeyLeft:
            df_show_anim(app, app->current - 1);
            break;
        case InputKeyUp:
        case InputKeyDown:
        case InputKeyOk:
        case InputKeyBack:
            df_goto_card(app);
            break;
        default:
            break;
        }
    } else {
        if(!is_short && !is_repeat) return;
        df_card_input(app, event->key, is_short);
    }
}

static void df_render(Canvas* canvas, void* ctx) {
    DfApp* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    if(app->screen == DfScreenAnim) {
        df_player_draw(app, canvas);
    } else {
        df_card_draw(app, canvas);
    }
    furi_mutex_release(app->mutex);
}

static void df_input_cb(InputEvent* input, void* ctx) {
    DfApp* app = ctx;
    DfEvent event = {.type = DfEventInput, .input = *input};
    furi_message_queue_put(app->queue, &event, FuriWaitForever);
}

static void df_frame_timer_cb(void* ctx) {
    DfApp* app = ctx;
    DfEvent event = {.type = DfEventTick};
    furi_message_queue_put(app->queue, &event, 0);
}

static DfApp* df_app_alloc(void) {
    DfApp* app = malloc(sizeof(DfApp));
    memset(app, 0, sizeof(DfApp));
    app->running = true;
    app->current = -1;
    app->screen = DfScreenAnim;
    app->sel = DfSelNone;

    app->storage = furi_record_open(RECORD_STORAGE);
    df_settings_load(app);
    df_library_build(app);

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->queue = furi_message_queue_alloc(16, sizeof(DfEvent));
    app->frame_timer = furi_timer_alloc(df_frame_timer_cb, FuriTimerTypePeriodic, app);

    app->gui = furi_record_open(RECORD_GUI);
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, df_render, app);
    view_port_input_callback_set(app->view_port, df_input_cb, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    return app;
}

static void df_app_free(DfApp* app) {
    df_settings_save(app);

    furi_timer_stop(app->frame_timer);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);

    furi_timer_free(app->frame_timer);
    furi_message_queue_free(app->queue);

    df_player_unload(app);
    df_library_free(app);

    furi_mutex_free(app->mutex);
    furi_record_close(RECORD_STORAGE);

    free(app);
}

int32_t dolphin_fox_main(void* p) {
    UNUSED(p);
    DfApp* app = df_app_alloc();

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    int idx = df_library_pick(app);
    if(idx < 0) idx = 0;
    df_show_anim(app, idx);
    furi_mutex_release(app->mutex);
    view_port_update(app->view_port);

    DfEvent event;
    while(app->running) {
        if(furi_message_queue_get(app->queue, &event, FuriWaitForever) != FuriStatusOk) continue;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        if(event.type == DfEventTick) {
            if(app->screen == DfScreenAnim && app->seq_len > 0) {
                app->pos = (app->pos + 1) % app->seq_len;
            }
        } else {
            df_handle_input(app, &event.input);
        }
        furi_mutex_release(app->mutex);
        view_port_update(app->view_port);
    }

    df_app_free(app);
    return 0;
}
