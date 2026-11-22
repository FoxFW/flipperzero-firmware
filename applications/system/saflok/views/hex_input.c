#include "hex_input.h"
#include "../icons/assets_icons.h"
#include <gui/elements.h>
#include <furi.h>

static const size_t keyboard_origin_x = 7;
static const size_t keyboard_origin_y = 31;
static const size_t keyboard_row_count = 2;
static const char enter_symbol = '\r';
static const char backspace_symbol = '\b';
static const char sign_symbol = '-';

static const HexInputKey keyboard_keys_row_1[] = {
    {'0', 0, 12},
    {'1', 11, 12},
    {'2', 22, 12},
    {'3', 33, 12},
    {'4', 44, 12},
    {'A', 66, 12},
    {'B', 77, 12},
    {'C', 88, 12},
    {backspace_symbol, 100, 3},
};

static const HexInputKey keyboard_keys_row_2[] = {
    {'5', 0, 26},
    {'6', 11, 26},
    {'7', 22, 26},
    {'8', 33, 26},
    {'9', 44, 26},
    {'D', 66, 26},
    {'E', 77, 26},
    {'F', 88, 26},
    {sign_symbol, 55, 17},
    {enter_symbol, 95, 17},
};

static size_t hex_input_get_row_size(size_t row_index) {
    size_t row_size = 0;

    switch(row_index + 1) {
    case 1:
        row_size = COUNT_OF(keyboard_keys_row_1);
        break;
    case 2:
        row_size = COUNT_OF(keyboard_keys_row_2);
        break;
    default:
        furi_crash();
    }

    return row_size;
}

static const HexInputKey* hex_input_get_row(size_t row_index) {
    const HexInputKey* row = NULL;

    switch(row_index + 1) {
    case 1:
        row = keyboard_keys_row_1;
        break;
    case 2:
        row = keyboard_keys_row_2;
        break;
    default:
        furi_crash();
    }

    return row;
}

static void hex_input_draw_input(Canvas* canvas, HexInputModel* model) {
    const size_t text_x = 8;
    const size_t text_y = 25;

    elements_slightly_rounded_frame(canvas, 4, 14, 120, 15);

    char hex_str[5];
    snprintf(hex_str, sizeof(hex_str), "%04lX", model->current_number);
    canvas_draw_str(canvas, text_x, text_y, hex_str);
}

static bool hex_input_use_sign(HexInputModel* model) {
    if(model->min_value < 0 && model->max_value >= 0) {
        return true;
    }
    return false;
}

static void hex_input_backspace_cb(HexInputModel* model) {
    size_t text_length = furi_string_utf8_length(model->text_buffer);
    if(text_length < 1 || (text_length < 2 && model->current_number <= 0)) {
        return;
    }
    furi_string_set_strn(
        model->text_buffer, furi_string_get_cstr(model->text_buffer), text_length - 1);
    model->current_number = strtol(furi_string_get_cstr(model->text_buffer), NULL, 16);
}

static void hex_input_handle_up(HexInputModel* model) {
    if(model->selected_row > 0) {
        model->selected_row--;
        if(model->selected_column > hex_input_get_row_size(model->selected_row) - 1) {
            model->selected_column = hex_input_get_row_size(model->selected_row) - 1;
        }
    }
}

static void hex_input_handle_down(HexInputModel* model) {
    if(model->selected_row < keyboard_row_count - 1) {
        if(model->selected_column >= hex_input_get_row_size(model->selected_row) - 1) {
            model->selected_column = hex_input_get_row_size(model->selected_row + 1) - 1;
        }
        model->selected_row += 1;
    }
    const HexInputKey* keys = hex_input_get_row(model->selected_row);
    if(keys[model->selected_column].text == sign_symbol && !hex_input_use_sign(model)) {
        model->selected_column--;
    }
}

static void hex_input_handle_left(HexInputModel* model) {
    if(model->selected_column > 0) {
        model->selected_column--;
    } else {
        model->selected_column = hex_input_get_row_size(model->selected_row) - 1;
    }
    const HexInputKey* keys = hex_input_get_row(model->selected_row);
    if(keys[model->selected_column].text == sign_symbol && !hex_input_use_sign(model)) {
        model->selected_column--;
    }
}

static void hex_input_handle_right(HexInputModel* model) {
    if(model->selected_column < hex_input_get_row_size(model->selected_row) - 1) {
        model->selected_column++;
    } else {
        model->selected_column = 0;
    }
    const HexInputKey* keys = hex_input_get_row(model->selected_row);
    if(keys[model->selected_column].text == sign_symbol && !hex_input_use_sign(model)) {
        model->selected_column++;
    }
}

typedef enum {
    StrintParseNoError,
    StrintParseSignError,
    StrintParseAbsentError,
    StrintParseOverflowError,
} StrintParseError;

static StrintParseError strint_to_uint64_internal(
    const char* str,
    char** end,
    uint64_t* abs_out,
    bool* negative_out,
    uint8_t base,
    uint64_t max_abs_negative,
    uint64_t max_positive) {
    while(((*str >= '\t') && (*str <= '\r')) || *str == ' ') {
        str++;
    }

    bool negative = false;
    if(*str == '+' || *str == '-') {
        if(*str == '-') negative = true;
        str++;
    }
    if(*str == '+' || *str == '-') return StrintParseSignError;
    if(max_abs_negative == 0 && negative) return StrintParseSignError;

    uint8_t inferred_base = 0;
    if(strncasecmp(str, "0x", 2) == 0) {
        inferred_base = 16;
        str += 2;
    } else if(strncasecmp(str, "0b", 2) == 0) {
        inferred_base = 2;
        str += 2;
    } else if(*str == '0') {
        inferred_base = 8;
        str++;
    } else {
        inferred_base = 10;
    }
    if(base == 0) base = inferred_base;

    uint64_t limit = negative ? max_abs_negative : max_positive;
    uint64_t mul_limit = limit / base;
    uint64_t result = 0;
    int read_total = 0;
    while(*str != 0) {
        int digit_value;
        if(*str >= '0' && *str <= '9') {
            digit_value = *str - '0';
        } else if(*str >= 'A' && *str <= 'Z') {
            digit_value = *str - 'A' + 10;
        } else if(*str >= 'a' && *str <= 'z') {
            digit_value = *str - 'a' + 10;
        } else {
            break;
        }

        if(digit_value >= base) {
            break;
        }

        if(result > mul_limit) return StrintParseOverflowError;
        result *= base;
        if(result > limit - digit_value) return StrintParseOverflowError;
        result += digit_value;

        read_total++;
        str++;
    }

    if(read_total == 0) {
        if(inferred_base == 8) {
            result = 0;
        } else {
            return StrintParseAbsentError;
        }
    }

    if(abs_out) *abs_out = result;
    if(negative_out) *negative_out = negative;
    if(end) *end = (char*)str;
    return StrintParseNoError;
}

static StrintParseError strint_to_int64(const char* str, char** end, int64_t* out, uint8_t base) {
    uint64_t absolute;
    bool negative;
    StrintParseError err = strint_to_uint64_internal(
        str, end, &absolute, &negative, base, ((uint64_t)INT64_MAX + 1), (INT64_MAX));
    if(err) return err;
    if(out) *out = (negative ? (-(int64_t)absolute) : ((int64_t)absolute));
    return StrintParseNoError;
}

static bool is_number_too_large(HexInputModel* model) {
    int64_t value;
    if(strint_to_int64(furi_string_get_cstr(model->text_buffer), NULL, &value, 16) !=
       StrintParseNoError) {
        return true;
    }
    if(value > (int64_t)model->max_value) {
        return true;
    }
    return false;
}

static bool is_number_too_small(HexInputModel* model) {
    int64_t value;
    if(strint_to_int64(furi_string_get_cstr(model->text_buffer), NULL, &value, 16) !=
       StrintParseNoError) {
        return true;
    }
    if(value < (int64_t)model->min_value) {
        return true;
    }
    return false;
}

static void hex_input_sign(HexInputModel* model) {
    int32_t number = strtol(furi_string_get_cstr(model->text_buffer), NULL, 16);
    if(number == 0 && furi_string_cmp_str(model->text_buffer, "-") != 0) {
        furi_string_set_str(model->text_buffer, "-");
        return;
    }
    number = number * -1;
    furi_string_printf(model->text_buffer, "%ld", number);
    if(is_number_too_large(model) || is_number_too_small(model)) {
        furi_string_printf(model->text_buffer, "%ld", model->current_number);
        return;
    }
    model->current_number = strtol(furi_string_get_cstr(model->text_buffer), NULL, 16);
    if(model->current_number == 0) {
        furi_string_set_str(model->text_buffer, "");
    }
}

static void hex_input_add_digit(HexInputModel* model, char* newChar) {
    furi_string_cat_str(model->text_buffer, newChar);
    if((model->max_value >= 0 && is_number_too_large(model)) ||
       (model->min_value < 0 && is_number_too_small(model))) {
        furi_string_printf(model->text_buffer, "%ld", model->current_number);
        return;
    }
    model->current_number = strtol(furi_string_get_cstr(model->text_buffer), NULL, 16);
    if(model->current_number == 0) {
        furi_string_set(model->text_buffer, "0");
    }
}

static void hex_input_handle_ok(HexInputModel* model) {
    char selected = hex_input_get_row(model->selected_row)[model->selected_column].text;
    char temp_str[2] = {selected, '\0'};
    if(selected == enter_symbol) {
        if(is_number_too_large(model) || is_number_too_small(model)) {
            return;
        }
        model->current_number = strtol(furi_string_get_cstr(model->text_buffer), NULL, 16);
        model->callback(model->callback_context, model->current_number);
    } else if(selected == backspace_symbol) {
        hex_input_backspace_cb(model);
    } else if(selected == sign_symbol) {
        hex_input_sign(model);
    } else {
        hex_input_add_digit(model, temp_str);
    }
}

static void hex_input_view_draw_callback(Canvas* canvas, void* _model) {
    HexInputModel* model = _model;

    hex_input_draw_input(canvas, model);

    if(!furi_string_empty(model->header)) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 0, AlignCenter, AlignTop, furi_string_get_cstr(model->header));
    }
    canvas_set_font(canvas, FontKeyboard);
    for(size_t row = 0; row < keyboard_row_count; row++) {
        const size_t column_count = hex_input_get_row_size(row);
        const HexInputKey* keys = hex_input_get_row(row);

        for(size_t column = 0; column < column_count; column++) {
            if(keys[column].text == sign_symbol && !hex_input_use_sign(model)) {
                continue;
            }

            if(keys[column].text == enter_symbol) {
                if(is_number_too_small(model) || is_number_too_large(model)) {
                    if(model->selected_row == row && model->selected_column == column) {
                        canvas_draw_icon(
                            canvas,
                            keyboard_origin_x + keys[column].x,
                            keyboard_origin_y + keys[column].y,
                            &I_KeySaveBlockedSelected);
                    } else {
                        canvas_draw_icon(
                            canvas,
                            keyboard_origin_x + keys[column].x,
                            keyboard_origin_y + keys[column].y,
                            &I_KeySaveBlocked);
                    }
                } else {
                    if(model->selected_row == row && model->selected_column == column) {
                        canvas_draw_icon(
                            canvas,
                            keyboard_origin_x + keys[column].x,
                            keyboard_origin_y + keys[column].y,
                            &I_KeySaveSelected);
                    } else {
                        canvas_draw_icon(
                            canvas,
                            keyboard_origin_x + keys[column].x,
                            keyboard_origin_y + keys[column].y,
                            &I_KeySave);
                    }
                }
            } else if(keys[column].text == backspace_symbol) {
                if(model->selected_row == row && model->selected_column == column) {
                    canvas_draw_icon(
                        canvas,
                        keyboard_origin_x + keys[column].x,
                        keyboard_origin_y + keys[column].y,
                        &I_KeyBackspaceSelected_16x9);
                } else {
                    canvas_draw_icon(
                        canvas,
                        keyboard_origin_x + keys[column].x,
                        keyboard_origin_y + keys[column].y,
                        &I_KeyBackspace_16x9);
                }
            } else if(keys[column].text == sign_symbol) {
                if(model->selected_row == row && model->selected_column == column) {
                    canvas_draw_icon(
                        canvas,
                        keyboard_origin_x + keys[column].x,
                        keyboard_origin_y + keys[column].y,
                        &I_KeySignSelected);
                } else {
                    canvas_draw_icon(
                        canvas,
                        keyboard_origin_x + keys[column].x,
                        keyboard_origin_y + keys[column].y,
                        &I_KeySign);
                }
            } else {
                if(model->selected_row == row && model->selected_column == column) {
                    elements_slightly_rounded_box(
                        canvas,
                        keyboard_origin_x + keys[column].x - 3,
                        keyboard_origin_y + keys[column].y - 10,
                        11,
                        13);
                    canvas_set_color(canvas, ColorWhite);
                }

                canvas_draw_glyph(
                    canvas,
                    keyboard_origin_x + keys[column].x,
                    keyboard_origin_y + keys[column].y,
                    keys[column].text);
                canvas_set_color(canvas, ColorBlack);
            }
        }
    }
}

static bool hex_input_view_input_callback(InputEvent* event, void* context) {
    furi_assert(context);
    HexInput* hex_input = context;

    bool consumed = false;

    HexInputModel* model = view_get_model(hex_input->view);

    if(event->type == InputTypeShort || event->type == InputTypeLong ||
       event->type == InputTypeRepeat) {
        consumed = true;
        switch(event->key) {
        case InputKeyLeft:
            hex_input_handle_left(model);
            break;
        case InputKeyRight:
            hex_input_handle_right(model);
            break;
        case InputKeyUp:
            hex_input_handle_up(model);
            break;
        case InputKeyDown:
            hex_input_handle_down(model);
            break;
        case InputKeyOk:
            hex_input_handle_ok(model);
            break;
        default:
            consumed = false;
            break;
        }
    }

    view_commit_model(hex_input->view, consumed);

    return consumed;
}

HexInput* hex_input_alloc(void) {
    HexInput* hex_input = malloc(sizeof(HexInput));
    hex_input->view = view_alloc();
    view_set_context(hex_input->view, hex_input);
    view_allocate_model(hex_input->view, ViewModelTypeLocking, sizeof(HexInputModel));
    view_set_draw_callback(hex_input->view, hex_input_view_draw_callback);
    view_set_input_callback(hex_input->view, hex_input_view_input_callback);

    with_view_model(
        hex_input->view,
        HexInputModel * model,
        {
            model->header = furi_string_alloc();
            model->text_buffer = furi_string_alloc();
        },
        true);

    return hex_input;
}

void hex_input_reset(HexInput* hex_input) {
    furi_assert(hex_input);

    with_view_model(
        hex_input->view,
        HexInputModel * model,
        {
            memset(&model->header, 0, sizeof(model->header));
            memset(&model->text_buffer, 0, sizeof(model->text_buffer));
            model->current_number = 0;
            model->max_value = 0;
            model->min_value = 0;
            model->value = 0;
            model->digit_count = 0;
            model->cursor = 0;
            model->callback = NULL;
            model->callback_context = NULL;
            model->selected_row = 0;
            model->selected_column = 0;
        }, false);
}

void hex_input_free(HexInput* hex_input) {
    furi_assert(hex_input);
    with_view_model(
        hex_input->view,
        HexInputModel * model,
        {
            furi_string_free(model->header);
            furi_string_free(model->text_buffer);
        },
        true);
    view_free(hex_input->view);
    free(hex_input);
}

View* hex_input_get_view(HexInput* hex_input) {
    furi_assert(hex_input);
    return hex_input->view;
}

void hex_input_set_result_callback(
    HexInput* hex_input,
    HexInputCallback callback,
    void* callback_context,
    int32_t current_number,
    int32_t min_value,
    int32_t max_value) {
    furi_assert(hex_input);

    if(current_number != 0) {
        current_number = CLAMP(current_number, max_value, min_value);
    }

    with_view_model(
        hex_input->view,
        HexInputModel * model,
        {
            model->callback = callback;
            model->callback_context = callback_context;
            model->current_number = current_number;
            if(current_number != 0) {
                furi_string_printf(model->text_buffer, "%04lX", current_number);
            } else {
                furi_string_set(model->text_buffer, "0000");
            }
            model->min_value = min_value;
            model->digit_count = 4;
            model->max_value = max_value;
        },
        true);
}

void hex_input_set_header_text(HexInput* hex_input, const char* text) {
    furi_assert(hex_input);
    with_view_model(
        hex_input->view, HexInputModel * model, { furi_string_set(model->header, text); }, true);
}
