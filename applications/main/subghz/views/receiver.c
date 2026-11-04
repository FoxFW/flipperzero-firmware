#include "receiver.h"
#include "../subghz_i.h"

#include "types.h"
#include <input/input.h>
#include <gui/elements.h>
#include <assets_icons.h>
#include <m-array.h>

#define FRAME_HEIGHT              12
#define MAX_LEN_PX                111
#define MENU_ITEMS                4u
#define UNLOCK_CNT                3
#define SUBGHZ_RX_RSSI_HISTORY_SIZE 128
#define SUBGHZ_RX_RSSI_DISPLAY_MIN  (-110.0f)
#define SUBGHZ_RX_RSSI_DISPLAY_MAX  (-50.0f)
#define SUBGHZ_RX_WAVE_MAX          33
#define SUBGHZ_RX_DELETE_GUARD_MS   300
#define SUBGHZ_READ_FREQ_CY         9
#define SUBGHZ_READ_FREQ_BASELINE   16

#define FLIP_TIMEOUT (500)
#define SUBGHZ_RX_FLASH_MS (400)

static const Icon* ReceiverItemIcons[] = {
    [SubGhzProtocolTypeUnknown] = &I_Quest_7x8,
    [SubGhzProtocolTypeStatic] = &I_Static_9x7,
    [SubGhzProtocolTypeDynamic] = &I_Dynamic_9x7,
    [SubGhzProtocolTypeBinRAW] = &I_Raw_9x7,
};

typedef enum {
    SubGhzViewReceiverBarShowDefault,
    SubGhzViewReceiverBarShowLock,
    SubGhzViewReceiverBarShowToUnlockPress,
    SubGhzViewReceiverBarShowUnlock,
} SubGhzViewReceiverBarShow;

struct SubGhzViewReceiver {
    bool lock;
    uint8_t lock_count;
    FuriTimer* timer;
    View* view;
    SubGhzViewReceiverCallback callback;
    void* context;
    FuriTimer* flip_time_timer;
    FuriTimer* flash_timer;
    bool delete_guard;
    uint32_t delete_guard_until;
};

typedef struct {
    FuriString* frequency_str;
    FuriString* preset_str;
    FuriString* history_stat_str;
    FuriString* progress_str;
    bool hopping_enabled;
    bool bin_raw_enabled;
    SubGhzViewReceiverItemCallback item_callback;
    void* item_context;
    int32_t flash_idx;
    uint32_t flash_until;
    uint16_t idx;
    uint16_t list_offset;
    uint16_t history_item;
    SubGhzViewReceiverBarShow bar_show;
    SubGhzViewReceiverMode mode;
    uint8_t u_rssi;

    SubGhzRadioDeviceType device_type;

    bool show_time;

    bool nodraw;

    uint8_t rssi_history[SUBGHZ_RX_RSSI_HISTORY_SIZE];
    uint8_t rssi_history_index;
    bool wave_line;

    bool dual;
    bool dual_dashboard;
    FuriString* dual_int_str;
    FuriString* dual_ext_str;
    uint8_t rssi_history_ext[SUBGHZ_RX_RSSI_HISTORY_SIZE];
    uint8_t rssi_history_ext_index;
} SubGhzViewReceiverModel;

void subghz_view_receiver_set_mode(
    SubGhzViewReceiver* subghz_receiver,
    SubGhzViewReceiverMode mode) {
    with_view_model(
        subghz_receiver->view, SubGhzViewReceiverModel * model, { model->mode = mode; }, true);
}

static uint8_t subghz_view_receiver_rssi_bar(float rssi) {
    uint8_t bar = 1;
    if(rssi >= SUBGHZ_RX_RSSI_DISPLAY_MAX) {
        bar = SUBGHZ_RX_WAVE_MAX;
    } else if(rssi > SUBGHZ_RX_RSSI_DISPLAY_MIN) {
        bar = (uint8_t)(((rssi - SUBGHZ_RX_RSSI_DISPLAY_MIN) /
                         (SUBGHZ_RX_RSSI_DISPLAY_MAX - SUBGHZ_RX_RSSI_DISPLAY_MIN)) *
                        SUBGHZ_RX_WAVE_MAX);
        if(bar < 1) bar = 1;
    }
    return bar;
}

void subghz_receiver_rssi(SubGhzViewReceiver* instance, float rssi) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzViewReceiverModel * model,
        {
            if(rssi < SUBGHZ_RAW_THRESHOLD_MIN) {
                model->u_rssi = 0;
            } else {
                model->u_rssi = (uint8_t)(rssi - SUBGHZ_RAW_THRESHOLD_MIN);
            }
            model->rssi_history[model->rssi_history_index] = subghz_view_receiver_rssi_bar(rssi);
            model->rssi_history_index =
                (model->rssi_history_index + 1) % SUBGHZ_RX_RSSI_HISTORY_SIZE;
        },
        true);
}

void subghz_receiver_rssi_ext(SubGhzViewReceiver* instance, float rssi) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzViewReceiverModel * model,
        {
            model->rssi_history_ext[model->rssi_history_ext_index] =
                subghz_view_receiver_rssi_bar(rssi);
            model->rssi_history_ext_index =
                (model->rssi_history_ext_index + 1) % SUBGHZ_RX_RSSI_HISTORY_SIZE;
        },
        true);
}

void subghz_view_receiver_set_dual(SubGhzViewReceiver* subghz_receiver, bool dual) {
    furi_assert(subghz_receiver);
    with_view_model(
        subghz_receiver->view,
        SubGhzViewReceiverModel * model,
        {
            model->dual = dual;
            model->dual_dashboard = dual;
        },
        true);
}

void subghz_view_receiver_set_dual_dashboard(SubGhzViewReceiver* subghz_receiver, bool dashboard) {
    furi_assert(subghz_receiver);
    with_view_model(
        subghz_receiver->view,
        SubGhzViewReceiverModel * model,
        { model->dual_dashboard = dashboard; },
        true);
}

void subghz_view_receiver_set_dual_info(
    SubGhzViewReceiver* subghz_receiver,
    const char* int_str,
    const char* ext_str) {
    furi_assert(subghz_receiver);
    with_view_model(
        subghz_receiver->view,
        SubGhzViewReceiverModel * model,
        {
            furi_string_set(model->dual_int_str, int_str);
            furi_string_set(model->dual_ext_str, ext_str);
        },
        true);
}

void subghz_view_receiver_set_wave_line(SubGhzViewReceiver* subghz_receiver, bool line) {
    furi_assert(subghz_receiver);
    with_view_model(
        subghz_receiver->view, SubGhzViewReceiverModel * model, { model->wave_line = line; }, true);
}

void subghz_view_receiver_set_lock(SubGhzViewReceiver* subghz_receiver, bool lock) {
    furi_assert(subghz_receiver);
    subghz_receiver->lock_count = 0;
    if(lock == true) {
        subghz_receiver->lock = lock;
        with_view_model(
            subghz_receiver->view,
            SubGhzViewReceiverModel * model,
            { model->bar_show = SubGhzViewReceiverBarShowLock; },
            true);
        furi_timer_start(subghz_receiver->timer, 1000);
    } else {
        with_view_model(
            subghz_receiver->view,
            SubGhzViewReceiverModel * model,
            { model->bar_show = SubGhzViewReceiverBarShowDefault; },
            true);
    }
}

void subghz_view_receiver_set_callback(
    SubGhzViewReceiver* subghz_receiver,
    SubGhzViewReceiverCallback callback,
    void* context) {
    furi_assert(subghz_receiver);
    furi_assert(callback);
    subghz_receiver->callback = callback;
    subghz_receiver->context = context;
}

static void subghz_view_receiver_update_offset(SubGhzViewReceiver* subghz_receiver) {
    furi_assert(subghz_receiver);

    with_view_model(
        subghz_receiver->view,
        SubGhzViewReceiverModel * model,
        {
            size_t history_item = model->history_item;
            uint16_t bounds = history_item > 3 ? 2 : history_item;

            if(history_item > 3 && model->idx >= (int16_t)(history_item - 1)) {
                model->list_offset = model->idx - 3;
            } else if(model->list_offset < model->idx - bounds) {
                model->list_offset =
                    CLAMP(model->list_offset + 1, (int16_t)(history_item - bounds), 0);
            } else if(model->list_offset > model->idx - bounds) {
                model->list_offset = CLAMP(model->idx - 1, (int16_t)(history_item - bounds), 0);
            }
        },
        true);
}

static void subghz_view_receiver_show_time_moment(void* context) {
    furi_assert(context);
    SubGhzViewReceiver* subghz_receiver = context;
    with_view_model(
        subghz_receiver->view, SubGhzViewReceiverModel * model, { model->show_time = true; }, true);
    furi_timer_start(subghz_receiver->flip_time_timer, FLIP_TIMEOUT);
}

static void subghz_view_receiver_flip_string_callback(void* context) {
    furi_assert(context);
    SubGhzViewReceiver* subghz_receiver = context;
    with_view_model(
        subghz_receiver->view,
        SubGhzViewReceiverModel * model,
        { model->show_time = false; },
        true);
}

static void subghz_view_receiver_flash_timer_callback(void* context) {
    furi_assert(context);
    SubGhzViewReceiver* subghz_receiver = context;
    with_view_model(
        subghz_receiver->view, SubGhzViewReceiverModel * model, { UNUSED(model); }, true);
}

void subghz_view_receiver_flash_item(SubGhzViewReceiver* subghz_receiver, uint16_t idx) {
    furi_assert(subghz_receiver);
    with_view_model(
        subghz_receiver->view,
        SubGhzViewReceiverModel * model,
        {
            if(idx < model->history_item) {
                model->flash_idx = idx;
                model->flash_until = furi_get_tick() + SUBGHZ_RX_FLASH_MS;
            }
        },
        true);
    furi_timer_start(subghz_receiver->flash_timer, SUBGHZ_RX_FLASH_MS + 20);
}

void subghz_view_receiver_set_item_callback(
    SubGhzViewReceiver* subghz_receiver,
    SubGhzViewReceiverItemCallback callback,
    void* context) {
    furi_assert(subghz_receiver);
    with_view_model(
        subghz_receiver->view,
        SubGhzViewReceiverModel * model,
        {
            model->item_callback = callback;
            model->item_context = context;
        },
        false);
}

void subghz_view_receiver_add_item_to_menu(SubGhzViewReceiver* subghz_receiver) {
    furi_assert(subghz_receiver);
    with_view_model(
        subghz_receiver->view,
        SubGhzViewReceiverModel * model,
        {
            if(model->idx == model->history_item - 1) {
                model->history_item++;
                model->idx++;
                subghz_view_receiver_show_time_moment(subghz_receiver);
            } else {
                model->history_item++;
            }
        },
        true);
    subghz_view_receiver_update_offset(subghz_receiver);
}

void subghz_view_receiver_add_data_statusbar(
    SubGhzViewReceiver* subghz_receiver,
    const char* frequency_str,
    const char* preset_str,
    const char* history_stat_str,
    bool hopping_enabled,
    bool bin_raw_enabled) {
    furi_assert(subghz_receiver);
    with_view_model(
        subghz_receiver->view,
        SubGhzViewReceiverModel * model,
        {
            furi_string_set(model->frequency_str, frequency_str);
            furi_string_set(model->preset_str, preset_str);
            furi_string_set(model->history_stat_str, history_stat_str);
            model->hopping_enabled = hopping_enabled;
            model->bin_raw_enabled = bin_raw_enabled;
        },
        true);
}

void subghz_view_receiver_add_data_progress(
    SubGhzViewReceiver* subghz_receiver,
    const char* progress_str) {
    furi_assert(subghz_receiver);
    with_view_model(
        subghz_receiver->view,
        SubGhzViewReceiverModel * model,
        { furi_string_set(model->progress_str, progress_str); },
        true);
}

void subghz_view_receiver_set_radio_device_type(
    SubGhzViewReceiver* subghz_receiver,
    SubGhzRadioDeviceType device_type) {
    furi_assert(subghz_receiver);
    with_view_model(
        subghz_receiver->view,
        SubGhzViewReceiverModel * model,
        { model->device_type = device_type; },
        true);
}

static void subghz_view_receiver_draw_frame(Canvas* canvas, uint16_t idx, bool scrollbar) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0 + idx * FRAME_HEIGHT, scrollbar ? 122 : 127, FRAME_HEIGHT);

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_dot(canvas, 0, 0 + idx * FRAME_HEIGHT);
    canvas_draw_dot(canvas, 1, 0 + idx * FRAME_HEIGHT);
    canvas_draw_dot(canvas, 0, (0 + idx * FRAME_HEIGHT) + 1);

    canvas_draw_dot(canvas, 0, (0 + idx * FRAME_HEIGHT) + 11);
    canvas_draw_dot(canvas, scrollbar ? 121 : 126, 0 + idx * FRAME_HEIGHT);
    canvas_draw_dot(canvas, scrollbar ? 121 : 126, (0 + idx * FRAME_HEIGHT) + 11);
}

static void subghz_view_rssi_draw(Canvas* canvas, SubGhzViewReceiverModel* model) {
    for(uint8_t i = 1; i < model->u_rssi; i++) {
        if(i % 5) {
            canvas_draw_dot(canvas, 46 + i, 50);
            canvas_draw_dot(canvas, 47 + i, 51);
            canvas_draw_dot(canvas, 46 + i, 52);
        }
    }
}

static void subghz_view_receiver_draw_wave(
    Canvas* canvas,
    const uint8_t* history,
    uint8_t history_index,
    bool line,
    uint8_t x0,
    uint8_t width,
    uint8_t baseline,
    uint8_t height) {
    const uint8_t skip = (uint8_t)(SUBGHZ_RX_RSSI_HISTORY_SIZE - width);
    uint8_t prev_y = baseline;
    for(uint8_t col = 0; col < width; col++) {
        uint8_t idx = (uint8_t)((history_index + skip + col) % SUBGHZ_RX_RSSI_HISTORY_SIZE);
        uint16_t bar = (uint16_t)history[idx] * height / SUBGHZ_RX_WAVE_MAX;
        if(bar < 1) bar = 1;
        if(bar > height) bar = height;
        const uint8_t top = (uint8_t)(baseline - bar + 1);
        const uint8_t x = (uint8_t)(x0 + col);
        if(line) {
            if(col == 0) {
                canvas_draw_dot(canvas, x, top);
            } else {
                canvas_draw_line(canvas, x - 1, prev_y, x, top);
            }
            prev_y = top;
        } else {
            canvas_draw_line(canvas, x, baseline, x, top);
        }
    }
}

static void subghz_view_receiver_draw_dual_pane(
    Canvas* canvas,
    SubGhzViewReceiverModel* model,
    const Icon* icon,
    const char* info,
    const uint8_t* history,
    uint8_t history_index,
    uint8_t top) {
    canvas_draw_rframe(canvas, 0, top, 128, 31, 3);
    canvas_draw_icon(canvas, 2, top + 2, icon);
    canvas_draw_str(canvas, 24, top + 11, info);
    subghz_view_receiver_draw_wave(
        canvas, history, history_index, model->wave_line, 2, 124, top + 28, 14);
}

static void subghz_view_receiver_draw_dual_dashboard(
    Canvas* canvas,
    SubGhzViewReceiverModel* model) {
    canvas_set_font(canvas, FontSecondary);
    subghz_view_receiver_draw_dual_pane(
        canvas,
        model,
        &I_Internal_ant_1_9x11,
        furi_string_get_cstr(model->dual_int_str),
        model->rssi_history,
        model->rssi_history_index,
        0);
    subghz_view_receiver_draw_dual_pane(
        canvas,
        model,
        &I_External_ant_1_9x11,
        furi_string_get_cstr(model->dual_ext_str),
        model->rssi_history_ext,
        model->rssi_history_ext_index,
        33);
    const char* stat = furi_string_get_cstr(model->history_stat_str);
    const int16_t room =
        (int16_t)(125 - 24 - canvas_string_width(canvas, furi_string_get_cstr(model->dual_int_str)) - 4);
    if((int16_t)canvas_string_width(canvas, stat) <= room) {
        canvas_draw_str_aligned(canvas, 125, 11, AlignRight, AlignBottom, stat);
    } else {
        char count[8];
        size_t n = strcspn(stat, "/");
        if(n >= sizeof(count)) n = sizeof(count) - 1;
        memcpy(count, stat, n);
        count[n] = '\0';
        if((int16_t)canvas_string_width(canvas, count) <= room) {
            canvas_draw_str_aligned(canvas, 125, 11, AlignRight, AlignBottom, count);
        }
    }
}

static void subghz_view_receiver_draw_arrow(
    Canvas* canvas,
    uint8_t apex_x,
    uint8_t cy,
    uint8_t width,
    uint8_t half_height,
    bool point_left) {
    for(uint8_t i = 0; i <= width; i++) {
        const uint8_t half = (uint8_t)((half_height * i + width / 2) / width);
        const uint8_t col = point_left ? (uint8_t)(apex_x + i) : (uint8_t)(apex_x - i);
        canvas_draw_line(canvas, col, (uint8_t)(cy - half), col, (uint8_t)(cy + half));
    }
}

static void subghz_view_receiver_draw_read_header(Canvas* canvas, SubGhzViewReceiverModel* model) {
    const char* freq_str = furi_string_get_cstr(model->frequency_str);
    canvas_set_font(canvas, FontBigNumbers);
    const uint8_t freq_w = canvas_string_width(canvas, freq_str);
    const uint8_t group_w = model->hopping_enabled ? freq_w : (uint8_t)(freq_w + 18);
    uint8_t x = group_w < 108 ? (uint8_t)((108 - group_w) / 2) : 0;
    if(!model->hopping_enabled) {
        subghz_view_receiver_draw_arrow(canvas, x, SUBGHZ_READ_FREQ_CY, 5, 3, true);
        x += 9;
    }
    canvas_draw_str(canvas, x, SUBGHZ_READ_FREQ_BASELINE, freq_str);
    x = (uint8_t)(x + canvas_string_width(canvas, freq_str) + 3);
    if(!model->hopping_enabled) {
        subghz_view_receiver_draw_arrow(canvas, (uint8_t)(x + 5), SUBGHZ_READ_FREQ_CY, 5, 3, false);
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_icon(
        canvas,
        109,
        0,
        (model->device_type == SubGhzRadioDeviceTypeInternal) ? &I_Internal_ant_1_9x11 :
                                                                &I_External_ant_1_9x11);
    if(model->bin_raw_enabled) {
        canvas_draw_icon(canvas, 116, 11, &I_Cos_9x7);
    }
    canvas_draw_line(canvas, 0, 19, 127, 19);
}

static void subghz_view_receiver_draw_bottom_info(Canvas* canvas, SubGhzViewReceiverModel* model) {
    canvas_set_font(canvas, FontSecondary);
    const char* stat = furi_string_get_cstr(model->history_stat_str);
    if(model->dual) {
        canvas_draw_str_aligned(canvas, 127, 62, AlignRight, AlignBottom, stat);
        return;
    }
    if(model->history_item == 0 && model->mode == SubGhzViewReceiverModeLive) {
        canvas_draw_str_aligned(canvas, 127, 62, AlignRight, AlignBottom, stat);
        canvas_draw_str(canvas, 3, 62, furi_string_get_cstr(model->preset_str));
        return;
    }
    const char* freq = furi_string_get_cstr(model->frequency_str);
    const char* preset = furi_string_get_cstr(model->preset_str);
    canvas_draw_str(canvas, 44, 62, freq);
    const int16_t preset_x = (int16_t)(44 + canvas_string_width(canvas, freq) + 4);
    const int16_t preset_w = (int16_t)canvas_string_width(canvas, preset);
    char count[8];
    size_t n = strcspn(stat, "/");
    if(n >= sizeof(count)) n = sizeof(count) - 1;
    memcpy(count, stat, n);
    count[n] = '\0';
    const char* shown = stat;
    bool show_preset = preset_w > 0 &&
                       preset_x + preset_w <= 127 - (int16_t)canvas_string_width(canvas, stat) - 1;
    if(!show_preset && preset_w > 0 &&
       preset_x + preset_w <= 127 - (int16_t)canvas_string_width(canvas, count) - 1) {
        shown = count;
        show_preset = true;
    }
    if(show_preset) canvas_draw_str(canvas, preset_x, 62, preset);
    canvas_draw_str_aligned(canvas, 127, 62, AlignRight, AlignBottom, shown);
}

void subghz_view_receiver_draw(Canvas* canvas, SubGhzViewReceiverModel* model) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    if(model->dual && (model->dual_dashboard || model->history_item == 0)) {
        subghz_view_receiver_draw_dual_dashboard(canvas, model);
        return;
    }

    if(model->mode == SubGhzViewReceiverModeLive) {
        if(model->history_item == 0) {
            elements_button_center(canvas, "Config");
        } else {
            elements_button_left(canvas, "Config");
        }
    } else {
        canvas_draw_line(canvas, 2, 52, 125, 52);
        canvas_draw_str(canvas, 3, 62, furi_string_get_cstr(model->progress_str));
    }

    bool scrollbar = model->history_item > 4;
    FuriString* str_buff = furi_string_alloc();
    FuriString* time_buff = furi_string_alloc();

    if(!model->nodraw && model->item_callback) {
        for(size_t i = 0; i < MIN(model->history_item, MENU_ITEMS); ++i) {
            size_t idx = CLAMP((uint16_t)(i + model->list_offset), model->history_item, 0);
            uint8_t type = 0;
            model->item_callback(model->item_context, idx, str_buff, time_buff, &type);
            if(type == 0) {
                break;
            }
            if(model->idx == idx) {
                subghz_view_receiver_draw_frame(canvas, i, scrollbar);
                if(model->show_time) {
                    furi_string_set(str_buff, time_buff);
                }
            } else {
                canvas_set_color(canvas, ColorBlack);
            }
            elements_string_fit_width(canvas, str_buff, scrollbar ? MAX_LEN_PX - 7 : MAX_LEN_PX);
            if(type < COUNT_OF(ReceiverItemIcons) && ReceiverItemIcons[type]) {
                canvas_draw_icon(canvas, 4, 2 + i * FRAME_HEIGHT, ReceiverItemIcons[type]);
            }
            bool flashing = model->flash_idx == (int32_t)idx && model->flash_until != 0 &&
                            furi_get_tick() < model->flash_until;
            if(flashing) {
                canvas_set_font(canvas, FontPrimary);
            }
            canvas_draw_str(canvas, 15, 9 + i * FRAME_HEIGHT, furi_string_get_cstr(str_buff));
            if(flashing) {
                canvas_set_font(canvas, FontSecondary);
            }
            furi_string_reset(str_buff);
        }
        if(scrollbar) {
            elements_scrollbar_pos(canvas, 128, 0, 49, model->idx, model->history_item);
        }
    }
    furi_string_free(str_buff);
    furi_string_free(time_buff);

    canvas_set_color(canvas, ColorBlack);

    if(model->history_item == 0) {
        if(model->mode == SubGhzViewReceiverModeLive) {
            subghz_view_receiver_draw_wave(
                canvas,
                model->rssi_history,
                model->rssi_history_index,
                model->wave_line,
                0,
                SUBGHZ_RX_RSSI_HISTORY_SIZE,
                49,
                27);
            subghz_view_receiver_draw_read_header(canvas, model);
        } else {

            canvas_set_font(canvas, FontPrimary);
            canvas_draw_str(canvas, 63, 46, "Decoding...");
            canvas_set_font(canvas, FontSecondary);
        }
    }

    if(model->mode == SubGhzViewReceiverModeLive && model->history_item > 0 && !model->dual) {
        subghz_view_rssi_draw(canvas, model);
    }
    switch(model->bar_show) {
    case SubGhzViewReceiverBarShowLock:
        canvas_draw_icon(canvas, 64, 55, &I_Lock_7x8);
        canvas_draw_str(canvas, 74, 62, "Locked");
        break;
    case SubGhzViewReceiverBarShowToUnlockPress:
        subghz_view_receiver_draw_bottom_info(canvas, model);
        canvas_set_font(canvas, FontSecondary);
        elements_bold_rounded_frame(canvas, 14, 8, 99, 48);
        elements_multiline_text(canvas, 65, 26, "To unlock\npress:");
        canvas_draw_icon(canvas, 65, 42, &I_Pin_back_arrow_10x8);
        canvas_draw_icon(canvas, 80, 42, &I_Pin_back_arrow_10x8);
        canvas_draw_icon(canvas, 95, 42, &I_Pin_back_arrow_10x8);

        canvas_draw_dot(canvas, 17, 61);
        break;
    case SubGhzViewReceiverBarShowUnlock:
        canvas_draw_icon(canvas, 64, 55, &I_Unlock_7x8);
        canvas_draw_str(canvas, 74, 62, "Unlocked");
        break;
    default:
        subghz_view_receiver_draw_bottom_info(canvas, model);
        break;
    }
}

static void subghz_view_receiver_timer_callback(void* context) {
    furi_assert(context);
    SubGhzViewReceiver* subghz_receiver = context;
    with_view_model(
        subghz_receiver->view,
        SubGhzViewReceiverModel * model,
        { model->bar_show = SubGhzViewReceiverBarShowDefault; },
        true);
    if(subghz_receiver->lock_count < UNLOCK_CNT) {
        subghz_receiver->callback(
            SubGhzCustomEventViewReceiverOffDisplay, subghz_receiver->context);
    }
    subghz_receiver->lock_count = 0;
}

bool subghz_view_receiver_input(InputEvent* event, void* context) {
    furi_assert(context);
    SubGhzViewReceiver* subghz_receiver = context;

    if(subghz_receiver->lock == true) {
        with_view_model(
            subghz_receiver->view,
            SubGhzViewReceiverModel * model,
            { model->bar_show = SubGhzViewReceiverBarShowToUnlockPress; },
            true);
        if(subghz_receiver->lock_count == 0) {
            furi_timer_start(subghz_receiver->timer, 1000);
        }
        if(event->key == InputKeyBack && event->type == InputTypeShort) {
            subghz_receiver->lock_count++;
        }
        if(subghz_receiver->lock_count >= UNLOCK_CNT) {
            subghz_receiver->callback(
                SubGhzCustomEventViewReceiverUnlock, subghz_receiver->context);
            with_view_model(
                subghz_receiver->view,
                SubGhzViewReceiverModel * model,
                { model->bar_show = SubGhzViewReceiverBarShowUnlock; },
                true);
            subghz_receiver->lock = false;
            furi_timer_start(subghz_receiver->timer, 650);
        }

        return true;
    }

    const uint32_t now = furi_get_tick();
    bool dual = false;
    bool dual_dashboard = false;
    bool dual_has_items = false;
    with_view_model(
        subghz_receiver->view,
        SubGhzViewReceiverModel * model,
        {
            dual = model->dual;
            dual_has_items = model->history_item != 0;
            dual_dashboard = model->dual && (model->dual_dashboard || !dual_has_items);
        },
        false);
    if(dual_dashboard) {
        if(event->type == InputTypeShort) {
            if(event->key == InputKeyBack) {
                subghz_receiver->callback(
                    SubGhzCustomEventViewReceiverBack, subghz_receiver->context);
            } else if(event->key == InputKeyOk && dual_has_items) {
                subghz_view_receiver_set_dual_dashboard(subghz_receiver, false);
            } else if(event->key == InputKeyOk || event->key == InputKeyLeft) {
                subghz_receiver->callback(
                    SubGhzCustomEventViewReceiverConfig, subghz_receiver->context);
            }
        }
        return true;
    }
    if(dual && event->key == InputKeyBack && event->type == InputTypeShort) {
        subghz_view_receiver_set_dual_dashboard(subghz_receiver, true);
        return true;
    }

    bool consumed = false;
    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        subghz_receiver->callback(SubGhzCustomEventViewReceiverBack, subghz_receiver->context);
        consumed = true;
    } else if(
        event->key == InputKeyUp &&
        (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
        with_view_model(
            subghz_receiver->view,
            SubGhzViewReceiverModel * model,
            {
                if(model->history_item == 0) {
                    if(model->mode == SubGhzViewReceiverModeLive &&
                       event->type == InputTypeShort) {
                        subghz_receiver->callback(
                            SubGhzCustomEventViewReceiverNextPreset, subghz_receiver->context);
                    }
                } else {
                    if(model->idx != 0) model->idx--;
                    subghz_view_receiver_show_time_moment(context);
                }
            },
            true);
        consumed = true;
    } else if(
        event->key == InputKeyDown &&
        (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
        with_view_model(
            subghz_receiver->view,
            SubGhzViewReceiverModel * model,
            {
                if(model->history_item == 0) {
                    if(model->mode == SubGhzViewReceiverModeLive &&
                       event->type == InputTypeShort) {
                        subghz_receiver->callback(
                            SubGhzCustomEventViewReceiverPrevPreset, subghz_receiver->context);
                    }
                } else if(model->idx != model->history_item - 1) {
                    model->idx++;
                    subghz_view_receiver_show_time_moment(context);
                }
            },
            true);
        consumed = true;
    } else if(
        event->key == InputKeyLeft &&
        (event->type == InputTypeShort || event->type == InputTypeRepeat ||
         event->type == InputTypeLong)) {
        bool empty_live = false;
        bool has_items = false;
        with_view_model(
            subghz_receiver->view,
            SubGhzViewReceiverModel * model,
            {
                empty_live = model->mode == SubGhzViewReceiverModeLive && model->history_item == 0;
                has_items = model->history_item != 0;
            },
            false);
        if(empty_live) {
            subghz_receiver->callback(
                SubGhzCustomEventViewReceiverPrevFrequency, subghz_receiver->context);
        } else if(has_items && event->type == InputTypeShort) {
            subghz_receiver->callback(SubGhzCustomEventViewReceiverConfig, subghz_receiver->context);
        }
        consumed = true;
    } else if(event->key == InputKeyRight && event->type == InputTypeRelease) {
        subghz_receiver->delete_guard = false;
        consumed = true;
    } else if(
        event->key == InputKeyRight &&
        (event->type == InputTypeShort || event->type == InputTypeRepeat ||
         event->type == InputTypeLong)) {
        bool empty_live = false;
        bool has_items = false;
        with_view_model(
            subghz_receiver->view,
            SubGhzViewReceiverModel * model,
            {
                empty_live = model->mode == SubGhzViewReceiverModeLive && model->history_item == 0;
                has_items = model->history_item != 0;
            },
            false);
        if(empty_live) {
            bool blocked = (event->type != InputTypeShort) &&
                           (subghz_receiver->delete_guard ||
                            now < subghz_receiver->delete_guard_until);
            if(!blocked) {
                subghz_receiver->callback(
                    SubGhzCustomEventViewReceiverNextFrequency, subghz_receiver->context);
            }
        } else if(has_items && event->type == InputTypeLong) {
            subghz_receiver->delete_guard = true;
            subghz_receiver->delete_guard_until =
                now + furi_ms_to_ticks(SUBGHZ_RX_DELETE_GUARD_MS);
            subghz_receiver->callback(
                SubGhzCustomEventViewReceiverDeleteItem, subghz_receiver->context);
        }
        consumed = true;
    } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
        bool empty_live = false;
        bool has_items = false;
        with_view_model(
            subghz_receiver->view,
            SubGhzViewReceiverModel * model,
            {
                empty_live = model->mode == SubGhzViewReceiverModeLive && model->history_item == 0;
                has_items = model->history_item != 0;
            },
            false);
        if(empty_live) {
            subghz_receiver->callback(SubGhzCustomEventViewReceiverConfig, subghz_receiver->context);
        } else if(has_items) {
            subghz_receiver->callback(SubGhzCustomEventViewReceiverOK, subghz_receiver->context);
        }
        consumed = true;
    }

    if(consumed) {
        subghz_view_receiver_update_offset(subghz_receiver);
    }
    return consumed;
}

void subghz_view_receiver_enter(void* context) {
    furi_assert(context);
    SubGhzViewReceiver* subghz_receiver = context;
    subghz_receiver->delete_guard = false;
    subghz_receiver->delete_guard_until = 0;
    subghz_view_receiver_show_time_moment(context);
}

void subghz_view_receiver_exit(void* context) {
    furi_assert(context);
    SubGhzViewReceiver* subghz_receiver = context;
    with_view_model(
        subghz_receiver->view,
        SubGhzViewReceiverModel * model,
        {
            furi_string_reset(model->frequency_str);
            furi_string_reset(model->preset_str);
            furi_string_reset(model->history_stat_str);
                model->flash_idx = -1;
                model->flash_until = 0;
                model->idx = 0;
                model->list_offset = 0;
                model->history_item = 0;
                model->nodraw = false;
                model->hopping_enabled = false;
                model->bin_raw_enabled = false;
                model->dual = false;
                model->dual_dashboard = false;
                furi_string_reset(model->dual_int_str);
                furi_string_reset(model->dual_ext_str);
        },
        false);
    furi_timer_stop(subghz_receiver->timer);
    furi_timer_stop(subghz_receiver->flip_time_timer);
    furi_timer_stop(subghz_receiver->flash_timer);
}

SubGhzViewReceiver* subghz_view_receiver_alloc(void) {
    SubGhzViewReceiver* subghz_receiver = malloc(sizeof(SubGhzViewReceiver));

    subghz_receiver->view = view_alloc();

    subghz_receiver->lock = false;
    subghz_receiver->lock_count = 0;
    view_allocate_model(
        subghz_receiver->view, ViewModelTypeLocking, sizeof(SubGhzViewReceiverModel));
    view_set_context(subghz_receiver->view, subghz_receiver);
    view_set_draw_callback(subghz_receiver->view, (ViewDrawCallback)subghz_view_receiver_draw);
    view_set_input_callback(subghz_receiver->view, subghz_view_receiver_input);
    view_set_enter_callback(subghz_receiver->view, subghz_view_receiver_enter);
    view_set_exit_callback(subghz_receiver->view, subghz_view_receiver_exit);

    subghz_receiver->flip_time_timer = furi_timer_alloc(
        subghz_view_receiver_flip_string_callback, FuriTimerTypeOnce, subghz_receiver);

    subghz_receiver->flash_timer = furi_timer_alloc(
        subghz_view_receiver_flash_timer_callback, FuriTimerTypeOnce, subghz_receiver);

    with_view_model(
        subghz_receiver->view,
        SubGhzViewReceiverModel * model,
        {
            model->frequency_str = furi_string_alloc();
            model->preset_str = furi_string_alloc();
            model->history_stat_str = furi_string_alloc();
            model->progress_str = furi_string_alloc();
            model->bar_show = SubGhzViewReceiverBarShowDefault;
            model->nodraw = false;
            model->item_callback = NULL;
            model->item_context = NULL;
            model->flash_idx = -1;
            model->flash_until = 0;
            model->hopping_enabled = false;
            model->bin_raw_enabled = false;
            memset(model->rssi_history, 0, sizeof(model->rssi_history));
            model->rssi_history_index = 0;
            model->dual = false;
            model->dual_dashboard = false;
            model->dual_int_str = furi_string_alloc();
            model->dual_ext_str = furi_string_alloc();
            memset(model->rssi_history_ext, 0, sizeof(model->rssi_history_ext));
            model->rssi_history_ext_index = 0;
        },
        true);
    subghz_receiver->timer =
        furi_timer_alloc(subghz_view_receiver_timer_callback, FuriTimerTypeOnce, subghz_receiver);
    return subghz_receiver;
}

void subghz_view_receiver_free(SubGhzViewReceiver* subghz_receiver) {
    furi_assert(subghz_receiver);

    furi_timer_free(subghz_receiver->flip_time_timer);
    furi_timer_free(subghz_receiver->flash_timer);

    with_view_model(
        subghz_receiver->view,
        SubGhzViewReceiverModel * model,
        {
            furi_string_free(model->frequency_str);
            furi_string_free(model->preset_str);
            furi_string_free(model->history_stat_str);
            furi_string_free(model->progress_str);
            furi_string_free(model->dual_int_str);
            furi_string_free(model->dual_ext_str);
        },
        false);
    furi_timer_free(subghz_receiver->timer);
    view_free(subghz_receiver->view);
    free(subghz_receiver);
}

View* subghz_view_receiver_get_view(SubGhzViewReceiver* subghz_receiver) {
    furi_assert(subghz_receiver);
    return subghz_receiver->view;
}

uint16_t subghz_view_receiver_get_idx_menu(SubGhzViewReceiver* subghz_receiver) {
    furi_assert(subghz_receiver);
    uint16_t idx;
    with_view_model(
        subghz_receiver->view, SubGhzViewReceiverModel * model, { idx = model->idx; }, false);
    return idx;
}

void subghz_view_receiver_delete_item(SubGhzViewReceiver* subghz_receiver, uint16_t idx) {
    furi_assert(subghz_receiver);

    with_view_model(
        subghz_receiver->view,
        SubGhzViewReceiverModel * model,
        {
            if(idx < model->history_item) {
                if(model->flash_idx == (int32_t)idx) {
                    model->flash_idx = -1;
                } else if(model->flash_idx > (int32_t)idx) {
                    model->flash_idx--;
                }

                if(model->history_item == 5) {
                    if(model->idx >= 2) {
                        model->idx = model->history_item - 1;
                    }
                }
                model->history_item--;

                if(model->idx && (model->idx > idx || model->idx == model->history_item)) {
                    model->idx--;
                }
            }
        },
        true);
}

void subghz_view_receiver_enable_draw_callback(SubGhzViewReceiver* subghz_receiver) {
    furi_assert(subghz_receiver);

    with_view_model(
        subghz_receiver->view, SubGhzViewReceiverModel * model, { model->nodraw = false; }, true);
}

void subghz_view_receiver_disable_draw_callback(SubGhzViewReceiver* subghz_receiver) {
    furi_assert(subghz_receiver);

    with_view_model(
        subghz_receiver->view, SubGhzViewReceiverModel * model, { model->nodraw = true; }, true);
}

void subghz_view_receiver_set_idx_menu(SubGhzViewReceiver* subghz_receiver, uint16_t idx) {
    furi_assert(subghz_receiver);
    with_view_model(
        subghz_receiver->view,
        SubGhzViewReceiverModel * model,
        {
            model->idx = idx;
            if(model->idx > 2) model->list_offset = idx - 2;
        },
        true);
    subghz_view_receiver_update_offset(subghz_receiver);
}
