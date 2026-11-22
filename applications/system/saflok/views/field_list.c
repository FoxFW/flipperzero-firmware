#include <furi.h>
#include <core/string.h>
#include <core/log.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gui/canvas.h>
#include <gui/elements.h>
#include <gui/modules/submenu.h>
#include "../saflok_app_i.h"
#include "../helpers/saflok_field_helpers.h"
#include "../helpers/saflok_app_helpers.h"
#include "views/aligned_text_box.h"

#undef TAG
#define TAG          "FieldList"
#define FIELD_COUNT  14
#define ITEM_HEIGHT  12
#define SCREEN_WIDTH 128
#define VIS_ITEMS    5

typedef struct {
    uint16_t selected_item;
    uint16_t top_item;
    FuriString* header;
    SaflokData* saflok_data;
    FieldListEnterCallback callback;
    void* callback_context;
    bool needs_extra_line[FIELD_COUNT];
} FieldListModel;

static void abbreviate_day(FuriString* output, const char* day) {
    const char* abbreviation = "None";
    for(int i = 0; i < DAY_COUNT; i++) {
        if(strcmp(day, weekdays[i].full_name) == 0) {
            abbreviation = weekdays[i].abbreviation;
            break;
        }
    }

    if(furi_string_size(output) > 0) {
        furi_string_cat(output, ",");
    }
    furi_string_cat(output, abbreviation);
}

void abbreviate_days(const char* input, FuriString* output) {
    FuriString* buffer = furi_string_alloc();
    furi_string_set(buffer, input);

    const char* token = furi_string_get_cstr(buffer);
    const char* delimiter = ", ";

    while(token && *token) {
        size_t delimiter_pos = strcspn(token, delimiter);
        char temp[32];
        snprintf(temp, sizeof(temp), "%.*s", (int)delimiter_pos, token);

        abbreviate_day(output, temp);

        token += delimiter_pos;
        while(*token && strchr(delimiter, *token))
            token++;
    }

    furi_string_free(buffer);
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

static void format_restrictions(SaflokData* data, char* buffer, size_t buffer_len, bool include_spaces) {
    if(!buffer || buffer_len == 0) return;

    SaflokDay* days = data->time_control.restrictions.day_array;
    bool none = days[MONDAY].enabled == false &&
                days[TUESDAY].enabled == false &&
                days[WEDNESDAY].enabled == false &&
                days[THURSDAY].enabled == false &&
                days[FRIDAY].enabled == false &&
                days[SATURDAY].enabled == false &&
                days[SUNDAY].enabled == false &&
                days[NONE].enabled == true;

    bool all = days[MONDAY].enabled == true &&
               days[TUESDAY].enabled == true &&
               days[WEDNESDAY].enabled == true &&
               days[THURSDAY].enabled == true &&
               days[FRIDAY].enabled == true &&
               days[SATURDAY].enabled == true &&
               days[SUNDAY].enabled == true &&
               days[ALL].enabled == true;

    bool wkdays = days[MONDAY].enabled == true &&
                    days[TUESDAY].enabled == true &&
                    days[WEDNESDAY].enabled == true &&
                    days[THURSDAY].enabled == true &&
                    days[FRIDAY].enabled == true &&
                    days[WEEKDAYS].enabled == true;

    bool weekends = days[SATURDAY].enabled == true &&
                    days[SUNDAY].enabled == true &&
                    days[WEEKENDS].enabled == true;

    int written = 0;
    int total_written = 0;
    bool first_item = true;
    const char* separator = include_spaces ? ", " : ",";

    written = snprintf(buffer, buffer_len, "[");
    if(written < 0 || written >= (int)buffer_len) return;
    total_written += written;

    if(none) {
        written = snprintf(buffer + total_written, buffer_len - total_written, "%s", weekdays[NONE].abbreviation);
    } else if(all) {
        written = snprintf(buffer + total_written, buffer_len - total_written, "%s", weekdays[ALL].abbreviation);
    } else {
        if(wkdays) {
            written = snprintf(buffer + total_written, buffer_len - total_written, "%s", weekdays[WEEKDAYS].abbreviation);
            if(written < 0 || written >= (int)(buffer_len - total_written)) goto fail;
            total_written += written;
            first_item = false;
        } else {
            for(int i = 0; i < 5; i++) {
                if(days[i].enabled) {
                    written = snprintf(
                        buffer + total_written,
                        buffer_len - total_written,
                        "%s%s",
                        (first_item ? "" : separator),
                        days[i].abbreviation);
                    if(written < 0 || written >= (int)(buffer_len - total_written)) goto fail;
                    total_written += written;
                    first_item = false;
                }
            }
        }

        if(weekends) {
             written = snprintf(
                buffer + total_written,
                buffer_len - total_written,
                "%s%s",
                (first_item ? "" : separator),
                weekdays[WEEKENDS].abbreviation);
            if(written < 0 || written >= (int)(buffer_len - total_written)) goto fail;
            total_written += written;
        } else {
             for(int i = 5; i < 7; i++) {
                if(days[i].enabled) {
                    written = snprintf(
                        buffer + total_written,
                        buffer_len - total_written,
                        "%s%s",
                        (first_item ? "" : separator),
                        days[i].abbreviation);
                    if(written < 0 || written >= (int)(buffer_len - total_written)) goto fail;
                    total_written += written;
                    first_item = false;
                }
            }
        }
        written = 0;

    }

    if(written < 0 || written >= (int)(buffer_len - total_written)) goto fail;
    total_written += written;

    if(total_written + 1 < (int)buffer_len) {
        buffer[total_written++] = ']';
        buffer[total_written] = '\0';
    }
    return;

fail:
    if(buffer_len > 0) {
         buffer[buffer_len - 1] = '\0';
    }
    return;
}

static uint8_t get_string_width(Canvas* canvas, const char* str) {
    return canvas_string_width(canvas, str);
}

static char* get_field_value(SaflokData* data, uint8_t index, char* buffer, size_t buffer_size) {
    SaflokApp* app = data->app_context;
    format_field_value(app, data, index, buffer, buffer_size);
    return buffer;
}

static void _draw_formatted_value(
    Canvas* canvas,
    int y_pos,
    const char* label,
    const char* value_with_spaces,
    const char* value_without_spaces) {

    const uint16_t RIGHT_EDGE = SCREEN_WIDTH - (FIELD_COUNT > 5 ? 2 : 0);
    const uint16_t LEFT_EDGE = 5;

    uint8_t label_width = canvas_string_width(canvas, label);
    uint8_t value_width_with_spaces = canvas_string_width(canvas, value_with_spaces);
    uint8_t value_width_without_spaces = canvas_string_width(canvas, value_without_spaces);

    uint8_t available_width = RIGHT_EDGE - LEFT_EDGE - 6;

    if(label_width + value_width_with_spaces < available_width) {
        canvas_draw_str(canvas, LEFT_EDGE, y_pos, label);
        canvas_draw_str_aligned(canvas, RIGHT_EDGE - 5, y_pos, AlignRight, AlignBottom, value_with_spaces);
    } else if(label_width + value_width_without_spaces < available_width) {
        canvas_draw_str(canvas, LEFT_EDGE, y_pos, label);
        canvas_draw_str_aligned(canvas, RIGHT_EDGE - 5, y_pos, AlignRight, AlignBottom, value_without_spaces);
    } else if(value_width_with_spaces < available_width) {
        canvas_draw_str(canvas, LEFT_EDGE, y_pos, label);
        canvas_draw_str_aligned(canvas, RIGHT_EDGE - 5, y_pos + (ITEM_HEIGHT - 2), AlignRight, AlignBottom, value_with_spaces);
    } else {
        canvas_draw_str(canvas, LEFT_EDGE, y_pos, label);
        canvas_draw_str_aligned(canvas, RIGHT_EDGE - 5, y_pos + (ITEM_HEIGHT - 2), AlignRight, AlignBottom, value_without_spaces);
    }
}

static void draw_aligned_field(Canvas* canvas, int y_pos, const char* label, const char* value, bool is_restrictions, bool is_pass_levels, SaflokData* data) {
    const uint16_t RIGHT_EDGE = SCREEN_WIDTH - (FIELD_COUNT > 5 ? 2 : 0);
    const uint16_t LEFT_EDGE = 5;

    canvas_set_font(canvas, FontSecondary);

    if((is_pass_levels || is_restrictions) && data) {
        char buffer_with_spaces[64];
        char buffer_without_spaces[64];

        if(is_pass_levels) {
            format_pass_levels(data, buffer_with_spaces, sizeof(buffer_with_spaces), true);
            format_pass_levels(data, buffer_without_spaces, sizeof(buffer_without_spaces), false);
        } else {
            format_restrictions(data, buffer_with_spaces, sizeof(buffer_with_spaces), true);
            format_restrictions(data, buffer_without_spaces, sizeof(buffer_without_spaces), false);
        }

        _draw_formatted_value(canvas, y_pos, label, buffer_with_spaces, buffer_without_spaces);
    } else {
        canvas_draw_str(canvas, LEFT_EDGE, y_pos, label);
        canvas_draw_str_aligned(canvas, RIGHT_EDGE - 5, y_pos, AlignRight, AlignBottom, value);
    }
}

void field_list_draw_callback(Canvas* canvas, void* _model) {
    FieldListModel* model = _model;
    const uint8_t item_height = 12;
    uint8_t item_width = canvas_width(canvas) - 5;
    const uint8_t VISIBLE_ITEMS = 5;

    char value_buffer[32];

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    for (uint8_t i = 0; i < FIELD_COUNT; i++) {
        bool is_pass_levels = (i == 5);
        bool is_restrictions = (i == 9);
        const uint16_t RIGHT_EDGE = SCREEN_WIDTH - (FIELD_COUNT > 5 ? 2 : 0);
        const uint16_t LEFT_EDGE = 5;

        char buffer_with_spaces[64];
        char buffer_without_spaces[64];

        if((is_pass_levels || is_restrictions) && model->saflok_data) {
            if(is_pass_levels) {
                format_pass_levels(model->saflok_data, buffer_with_spaces, sizeof(buffer_with_spaces), true);
                format_pass_levels(model->saflok_data, buffer_without_spaces, sizeof(buffer_without_spaces), false);
            } else {
                format_restrictions(model->saflok_data, buffer_with_spaces, sizeof(buffer_with_spaces), true);
                format_restrictions(model->saflok_data, buffer_without_spaces, sizeof(buffer_without_spaces), false);
            }

            uint8_t label_width = get_string_width(canvas, field_labels[i]);
            uint8_t value_width_without_spaces = get_string_width(canvas, buffer_without_spaces);

            uint8_t available_width = RIGHT_EDGE - LEFT_EDGE - 6;

            model->needs_extra_line[i] = (label_width + value_width_without_spaces >= available_width);
        } else {
            model->needs_extra_line[i] = false;
        }
    }

    uint8_t y_pos = item_height;
    uint8_t item_position = 0;
    uint8_t extra_height = 0;

    for(uint8_t i = model->top_item; i < FIELD_COUNT && item_position < VISIBLE_ITEMS; i++) {
        uint8_t current_index = i;
        bool is_selected = current_index == model->selected_item;
        bool is_restrictions = (current_index == 9);
        bool is_pass_levels = (current_index == 5);
        bool needs_extra_line = model->needs_extra_line[current_index];

        uint8_t current_item_height = needs_extra_line ? item_height * 2 - 2 : item_height;

        if(is_selected) {
            elements_slightly_rounded_box(canvas, 0, y_pos - 9, item_width, current_item_height);
            canvas_set_color(canvas, ColorWhite);
        }

        get_field_value(model->saflok_data, current_index, value_buffer, sizeof(value_buffer));

        draw_aligned_field(
            canvas,
            y_pos,
            field_labels[current_index],
            value_buffer,
            is_restrictions,
            is_pass_levels,
            model->saflok_data);

        if(is_selected) {
            canvas_set_color(canvas, ColorBlack);
        }

        y_pos += current_item_height;
        item_position++;

        if (needs_extra_line) {
            extra_height++;
        }
    }

    if(FIELD_COUNT > VISIBLE_ITEMS) {
        elements_scrollbar_pos(
            canvas,
            SCREEN_WIDTH,
            0,
            item_height * VISIBLE_ITEMS,
            model->selected_item,
            FIELD_COUNT + extra_height);
    }
}

static bool field_list_input_callback(InputEvent* event, void* context) {
    FieldList* field_list = context;
    bool consumed = false;

    with_view_model(
        field_list->view,
        FieldListModel * model,
        {
            if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
                switch(event->key) {
                case InputKeyUp:
                    if(model->selected_item > 0) {
                        model->selected_item--;

                        if(model->selected_item < model->top_item) {
                            model->top_item = model->selected_item;
                        }

                        consumed = true;
                    }
                    break;

                case InputKeyDown:
                    if(model->selected_item < (FIELD_COUNT - 1)) {
                        model->selected_item++;

                        uint8_t visible_items = 0;
                        uint8_t visible_position = 0;

                        for(uint8_t i = model->top_item; i < FIELD_COUNT && visible_items < VIS_ITEMS; i++) {
                            visible_items++;
                            visible_position++;

                            if(model->needs_extra_line[i]) {
                                visible_position++;
                            }

                            if(i == model->selected_item) {
                                break;
                            }
                        }

                        if(visible_position > 4 || (visible_position == 4 && model->needs_extra_line[model->selected_item])) {
                            model->top_item++;
                        }

                        consumed = true;
                    }
                    break;

                case InputKeyOk:
                    if(model->callback) {
                        model->callback(model->callback_context, model->selected_item);
                        consumed = true;
                    }
                    break;

                default:
                    break;
                }
            }
        },
        consumed);

    return consumed;
}

FieldList* field_list_alloc() {
    FieldList* field_list = malloc(sizeof(FieldList));
    field_list->view = view_alloc();
    view_allocate_model(field_list->view, ViewModelTypeLocking, sizeof(FieldListModel));
    view_set_context(field_list->view, field_list);
    view_set_draw_callback(field_list->view, field_list_draw_callback);
    view_set_input_callback(field_list->view, field_list_input_callback);

    with_view_model(
        field_list->view,
        FieldListModel * model,
        {
            model->selected_item = 0;
            model->top_item = 0;
            model->saflok_data = NULL;
            model->callback = NULL;
            model->callback_context = NULL;

            for(int i = 0; i < FIELD_COUNT; i++) {
                model->needs_extra_line[i] = false;
            }
        },
        true);

    return field_list;
}

void field_list_free(FieldList* field_list) {
    furi_assert(field_list);
    view_free(field_list->view);
    free(field_list);
}

View* field_list_get_view(FieldList* field_list) {
    furi_assert(field_list);
    return field_list->view;
}

void field_list_reset(FieldList* field_list) {
    furi_assert(field_list);
    furi_assert(field_list->view);

    with_view_model(
        field_list->view,
        FieldListModel * model,
        {
            model->selected_item = 0;
            model->top_item = 0;
            model->saflok_data = NULL;
            model->callback = NULL;
            model->callback_context = NULL;
        },
        true);
}

void field_list_set_callback(FieldList* field_list, FieldListEnterCallback callback, void* context) {
    furi_assert(field_list);
    with_view_model(
        field_list->view,
        FieldListModel * model,
        {
            model->callback = callback;
            model->callback_context = context;
        },
        true);
}

void field_list_set_saflok_data(FieldList* field_list, SaflokData* data) {
    furi_assert(field_list);
    with_view_model(field_list->view, FieldListModel * model, {
        model->saflok_data = data;
    }, true);
}

void field_list_set_selected_item(FieldList* field_list, uint8_t index) {
    furi_assert(field_list);
    with_view_model(
        field_list->view,
        FieldListModel * model,
        {
            if(index < FIELD_COUNT) {
                model->selected_item = index;
                if(model->selected_item < model->top_item) {
                    model->top_item = model->selected_item;
                } else if(model->selected_item >= model->top_item + 4) {
                    model->top_item = model->selected_item - 3;
                }
            }
        },
        true);
}

uint8_t field_list_get_selected_item(FieldList* field_list) {
    furi_assert(field_list);
    uint8_t selected;
    with_view_model(
        field_list->view, FieldListModel * model, { selected = model->selected_item; }, false);
    return selected;
}
