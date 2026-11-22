#include "aligned_text_box.h"
#include "helpers/saflok_field_helpers.h"
#include "helpers/saflok_app_helpers.h"
#include <gui/canvas.h>
#include <gui/elements.h>
#include <furi.h>
#include <furi/core/log.h>
#include <input/input.h>

#define FIELD_COUNT 14
#define ITEM_HEIGHT 12
#define VISIBLE_ITEMS 5

struct AlignedTextBox {
    View* view;
};

typedef struct {
    SaflokData* saflok_data;
    uint16_t scroll_pos;
    uint16_t scroll_num;
} AlignedTextBoxModel;

extern void abbreviate_days(const char* input, FuriString* output);

static uint8_t get_string_width(Canvas* canvas, const char* str) {
    return canvas_string_width(canvas, str);
}

static void format_pass_levels(SaflokData* data, char* buffer, size_t buffer_size, bool include_spaces) {
    size_t pos = 0;
    bool first = true;

    buffer[pos++] = '[';

    for(int i = 0; i < 12 && data->access_control.pass_levels[i] != 0 && pos + 4 < buffer_size; i++) {
        if(!first) {
            buffer[pos++] = ',';
            if(include_spaces && pos + 1 < buffer_size) {
                buffer[pos++] = ' ';
            }
        }
        first = false;
        pos += snprintf(buffer + pos, buffer_size - pos - 1, "%d", data->access_control.pass_levels[i]);
    }

    if(pos + 1 < buffer_size) {
        buffer[pos++] = ']';
        buffer[pos] = '\0';
    }
}

static void draw_aligned_field(Canvas* canvas, int y_pos, const char* label, const char* value, bool is_weekday, bool is_pass_levels, SaflokData* data) {
    const uint16_t RIGHT_EDGE = 128 - (FIELD_COUNT > 5 ? 2 : 0);
    const uint16_t LEFT_EDGE = 5;

        canvas_set_font(canvas, FontSecondary);

    if(is_pass_levels && data) {
        char buffer_with_spaces[64];
        char buffer_without_spaces[64];

        format_pass_levels(data, buffer_with_spaces, sizeof(buffer_with_spaces), true);
        format_pass_levels(data, buffer_without_spaces, sizeof(buffer_without_spaces), false);

        uint8_t label_width = get_string_width(canvas, label);
        uint8_t value_width_with_spaces = get_string_width(canvas, buffer_with_spaces);
        uint8_t value_width_without_spaces = get_string_width(canvas, buffer_without_spaces);

        uint8_t available_width = RIGHT_EDGE - LEFT_EDGE - 6;

        if(label_width + value_width_with_spaces < available_width) {
            canvas_draw_str(canvas, LEFT_EDGE, y_pos, label);
            canvas_draw_str_aligned(canvas, RIGHT_EDGE - 5, y_pos, AlignRight, AlignBottom, buffer_with_spaces);
        } else if(label_width + value_width_without_spaces < available_width) {
            canvas_draw_str(canvas, LEFT_EDGE, y_pos, label);
            canvas_draw_str_aligned(canvas, RIGHT_EDGE - 5, y_pos, AlignRight, AlignBottom, buffer_without_spaces);
        } else if(value_width_with_spaces < available_width) {
            canvas_draw_str(canvas, LEFT_EDGE, y_pos, label);
            canvas_draw_str_aligned(canvas, RIGHT_EDGE - 5, y_pos + (ITEM_HEIGHT - 2), AlignRight, AlignBottom, buffer_with_spaces);
        } else {
            canvas_draw_str(canvas, LEFT_EDGE, y_pos, label);
            canvas_draw_str_aligned(canvas, RIGHT_EDGE - 5, y_pos + (ITEM_HEIGHT - 2), AlignRight, AlignBottom, buffer_without_spaces);
        }
    } else {
        canvas_draw_str(canvas, LEFT_EDGE, y_pos, label);
        canvas_draw_str_aligned(canvas, RIGHT_EDGE - 5, y_pos, AlignRight, AlignBottom, value);
    }

    if(is_weekday) {
        canvas_set_font(canvas, FontSecondary);
    }
}

static char* get_field_value(SaflokData* data, uint8_t index, char* buffer, size_t buffer_size) {
    switch(index) {
    case 0:
        snprintf(buffer, buffer_size, "%s", data->access_control.key_level.name);
        break;
    case 1:
        snprintf(buffer, buffer_size, "%s", data->access_control.sub_function.name);
        break;
    case 2:
        snprintf(buffer, buffer_size, data->app_context->display_settings->key_id_hex_format ? "%02X" : "%02d", data->identification.key_id);
        break;
    case 3:
        snprintf(buffer, buffer_size, "%s", data->access_control.led_warning ? "Yes" : "No");
        break;
    case 4:
        snprintf(buffer, buffer_size, data->app_context->display_settings->key_record_hex_format ? "%04X" : "%04d", data->identification.key_record);
        break;
    case 5:
        snprintf(buffer, buffer_size, "PASS_LEVELS_PLACEHOLDER");
        break;
    case 6:
        snprintf(buffer, buffer_size, "%s", data->access_control.opening_key ? "Yes" : "No");
        break;
    case 7:
        snprintf(buffer, buffer_size, data->app_context->display_settings->sequence_combo_hex_format ? "%04X" : "%04d", data->security.sequence_combination);
        break;
    case 8:
        snprintf(buffer, buffer_size, "%s", data->access_control.override_deadbolt ? "Yes" : "No");
        break;
    case 9: {
        FuriString* abbrev = furi_string_alloc();
        abbreviate_days(
            furi_string_get_cstr(data->time_control.restrictions.restricted_weekday_string),
            abbrev);
        snprintf(buffer, buffer_size, "%s", furi_string_get_cstr(abbrev));
        furi_string_free(abbrev);
        break;
    }
    case 10:
        snprintf(
            buffer,
            buffer_size,
            "%04d-%02d-%02d",
            data->time_control.creation_date.year,
            data->time_control.creation_date.month,
            data->time_control.creation_date.day);
        break;
    case 11:
        snprintf(
            buffer,
            buffer_size,
            "%04d-%02d-%02d",
            data->time_control.expiration_date.year,
            data->time_control.expiration_date.month,
            data->time_control.expiration_date.day);
        break;
    case 12:
        snprintf(buffer, buffer_size, data->app_context->display_settings->property_number_hex_format ? "%04X" : "%04d", data->identification.property_number);
        break;
    case 13:
        FURI_LOG_D("Saflok", "In aligned_text_box");
        snprintf(buffer, buffer_size, "%s", saflok_validate_checksum(data) ? "Yes" : "No");
        break;
    default:
        snprintf(buffer, buffer_size, " ");
        break;
    }
    return buffer;
}

static void aligned_text_box_draw_callback(Canvas* canvas, void* _model) {
    AlignedTextBoxModel* model = _model;
    char value_buffer[64];
    const uint16_t RIGHT_EDGE = 128 - (FIELD_COUNT > 5 ? 2 : 0);
    const uint16_t LEFT_EDGE = 5;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 0, AlignCenter, AlignTop, "Saflok Card");
    canvas_set_font(canvas, FontSecondary);

    uint8_t y_pos = ITEM_HEIGHT + 8;
    bool needs_extra_line = false;

    for(uint8_t i = 0; i < VISIBLE_ITEMS && (i + model->scroll_pos) < FIELD_COUNT; i++) {
        uint8_t current_index = i + model->scroll_pos;
        if(current_index >= FIELD_COUNT) break;

        get_field_value(model->saflok_data, current_index, value_buffer, sizeof(value_buffer));

        if(field_labels[current_index] != NULL) {
            bool is_pass_levels = (current_index == 5);

            draw_aligned_field(
                canvas,
                y_pos,
                field_labels[current_index],
                value_buffer,
                current_index == 11,
                is_pass_levels,
                model->saflok_data);

            if(is_pass_levels) {
                char buffer_with_spaces[64];
                char buffer_without_spaces[64];

                format_pass_levels(model->saflok_data, buffer_with_spaces, sizeof(buffer_with_spaces), true);
                format_pass_levels(model->saflok_data, buffer_without_spaces, sizeof(buffer_without_spaces), false);

                uint8_t label_width = get_string_width(canvas, field_labels[current_index]);
                uint8_t value_width_without_spaces = get_string_width(canvas, buffer_without_spaces);

                uint8_t available_width = RIGHT_EDGE - LEFT_EDGE - 6;

                if(label_width + value_width_without_spaces > available_width) {
                    needs_extra_line = true;
                    y_pos += ITEM_HEIGHT - 2;
                }
            }
        }

        y_pos += ITEM_HEIGHT - 2;
    }

    if(FIELD_COUNT > VISIBLE_ITEMS) {
        elements_scrollbar_pos(
            canvas,
            128,
            0,
            canvas_height(canvas),
            model->scroll_pos,
            FIELD_COUNT - VISIBLE_ITEMS + (needs_extra_line ? 0 : 1));
    }
}

static bool aligned_text_box_input_callback(InputEvent* event, void* context) {
    furi_assert(context);
    AlignedTextBox* aligned_text_box = context;
    bool consumed = false;

    if(!aligned_text_box || !aligned_text_box->view) {
        return false;
    }

    with_view_model(aligned_text_box->view, AlignedTextBoxModel * model, {
        if(!model->saflok_data) {
            consumed = false;
        } else if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
            switch(event->key) {
                case InputKeyUp:
                    if(model->scroll_pos > 0) {
                        model->scroll_pos--;
                        consumed = true;
                    }
                    break;
                case InputKeyDown:
                    if(model->scroll_pos < (FIELD_COUNT - VISIBLE_ITEMS)) {
                        model->scroll_pos++;
                        consumed = true;
                    }
                    break;
                default:
                    break;
            }
        }
    }, consumed);

    return consumed;
}

AlignedTextBox* aligned_text_box_alloc() {
    AlignedTextBox* aligned_text_box = malloc(sizeof(AlignedTextBox));
    aligned_text_box->view = view_alloc();
    view_allocate_model(aligned_text_box->view, ViewModelTypeLocking, sizeof(AlignedTextBoxModel));
    view_set_context(aligned_text_box->view, aligned_text_box);
    view_set_draw_callback(aligned_text_box->view, aligned_text_box_draw_callback);
    view_set_input_callback(aligned_text_box->view, aligned_text_box_input_callback);

    with_view_model(aligned_text_box->view, AlignedTextBoxModel * model, {
        model->scroll_pos = 0;
        model->saflok_data = NULL;
    }, true);

    return aligned_text_box;
}

void aligned_text_box_free(AlignedTextBox* aligned_text_box) {
    furi_assert(aligned_text_box);
    view_free(aligned_text_box->view);
    free(aligned_text_box);
}

View* aligned_text_box_get_view(AlignedTextBox* aligned_text_box) {
    furi_assert(aligned_text_box);
    return aligned_text_box->view;
}

void aligned_text_box_reset(AlignedTextBox* aligned_text_box) {
    furi_assert(aligned_text_box);
    furi_assert(aligned_text_box->view);

    with_view_model(aligned_text_box->view, AlignedTextBoxModel * model, {
        model->saflok_data = NULL;
        model->scroll_pos = 0;
        model->scroll_num = 0;
    }, true);
}

void aligned_text_box_set_saflok_data(AlignedTextBox* aligned_text_box, SaflokData* data) {
    FURI_LOG_D("aligned_text_box", "In aligned_text_box_set_saflok_data");
    furi_assert(data);

    FURI_LOG_D("aligned_text_box", "Setting saflok_data in model");
    with_view_model(aligned_text_box->view, AlignedTextBoxModel * model, {
        model->saflok_data = data;
    }, true);
}
