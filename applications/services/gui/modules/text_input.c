#include "text_input.h"
#include <gui/elements.h>
#include <furi.h>
#include <string.h>

struct TextInput {
    View* view;
    FuriTimer* timer;
    FuriTimer* blink_timer;
};

typedef enum {
    TextInputLayerLetters,
    TextInputLayerSymbols,
} TextInputLayer;

typedef struct {
    const char* header;
    char* text_buffer;
    size_t text_buffer_size;
    size_t minimum_length;
    bool clear_default_text;

    TextInputCallback callback;
    void* callback_context;

    TextInputLayer layer;
    bool in_buttons;
    bool in_text;
    size_t cursor_pos;
    uint8_t selected_row;
    uint8_t selected_column;

    TextInputValidatorCallback validator_callback;
    void* validator_callback_context;
    FuriString* validator_text;
    bool validator_message_visible;

    bool space_blink;
} TextInputModel;

static const char letters_row_0[] = "qwertyuiop";
static const char letters_row_1[] = "asdfghjkl:";
static const char letters_row_2[] = "zxcvbnm ./";

static const char symbols_row_0[] = "1234567890";
static const char symbols_row_1[] = ":/.-_=@#&";
static const char symbols_row_2[] = "!?%+*,;()";

#define ROW_COUNT 3
static const uint8_t ROW_TOP[ROW_COUNT] = {20, 34, 48};
static const uint8_t ROW_BOX_H[ROW_COUNT] = {13, 13, 14};

static const uint8_t LETTERS_ROW_COLS[ROW_COUNT] = {10, 10, 10};
static const uint8_t LETTERS_ROW_START_X[ROW_COUNT] = {1, 1, 1};
static const uint8_t SYMBOLS_ROW_COLS[ROW_COUNT] = {10, 9, 9};
static const uint8_t SYMBOLS_ROW_START_X[ROW_COUNT] = {1, 6, 6};
#define GRID_BOX_W 9

static uint8_t get_row_cols(TextInputLayer layer, uint8_t row) {
    return layer == TextInputLayerLetters ? LETTERS_ROW_COLS[row] : SYMBOLS_ROW_COLS[row];
}

static uint8_t get_row_start_x(TextInputLayer layer, uint8_t row) {
    return layer == TextInputLayerLetters ? LETTERS_ROW_START_X[row] : SYMBOLS_ROW_START_X[row];
}

#define BTN_LEFT 103
#define BTN_W    24

static const char* get_row_chars(TextInputLayer layer, uint8_t row) {
    if(layer == TextInputLayerLetters) {
        switch(row) {
        case 0:
            return letters_row_0;
        case 1:
            return letters_row_1;
        default:
            return letters_row_2;
        }
    } else {
        switch(row) {
        case 0:
            return symbols_row_0;
        case 1:
            return symbols_row_1;
        default:
            return symbols_row_2;
        }
    }
}

static char get_char(TextInputLayer layer, uint8_t row, uint8_t column) {
    return get_row_chars(layer, row)[column];
}

static char char_shift(char c) {
    if(c >= 'a' && c <= 'z') return (char)(c - 0x20);
    if(c >= 'A' && c <= 'Z') return (char)(c + 0x20);
    return c;
}

static void text_input_delete(TextInputModel* model) {
    if(model->clear_default_text) {
        model->clear_default_text = false;
        return;
    }

    size_t len = strlen(model->text_buffer);
    if(model->cursor_pos == 0 || model->cursor_pos > len) return;
    memmove(
        model->text_buffer + model->cursor_pos - 1,
        model->text_buffer + model->cursor_pos,
        len - model->cursor_pos + 1);
    model->cursor_pos--;
}

static void text_input_clamp_column(TextInputModel* model) {
    uint8_t size = get_row_cols(model->layer, model->selected_row);
    if(model->selected_column >= size) {
        model->selected_column = size - 1;
    }
}

static void draw_grid(Canvas* canvas, TextInputModel* model) {

    canvas_set_font(canvas, model->layer == TextInputLayerLetters ? FontKeyboard : FontSecondary);

    for(uint8_t row = 0; row < ROW_COUNT; row++) {
        const char* chars = get_row_chars(model->layer, row);
        uint8_t cols = get_row_cols(model->layer, row);
        uint8_t top = ROW_TOP[row];
        uint8_t h = ROW_BOX_H[row];
        uint8_t start_x = get_row_start_x(model->layer, row);

        for(uint8_t column = 0; column < cols; column++) {
            uint8_t x = start_x + column * 10;
            bool selected = !model->in_text && !model->in_buttons &&
                             model->selected_row == row && model->selected_column == column;

            canvas_set_color(canvas, ColorBlack);
            if(selected) {
                canvas_draw_rbox(canvas, x, top, GRID_BOX_W, h, 1);
                canvas_set_color(canvas, ColorWhite);
            } else {
                canvas_draw_rframe(canvas, x, top, GRID_BOX_W, h, 1);
            }
            if(chars[column] == ' ') {
                if(model->space_blink) {
                    canvas_draw_glyph(canvas, x + 2, (uint8_t)(top + h - 4), '_');
                }
            } else {

                uint8_t glyph_y =
                    (chars[column] == '_') ? (uint8_t)(top + h - 4) : (uint8_t)(top + h - 3);
                canvas_draw_glyph(canvas, x + 2, glyph_y, chars[column]);
            }
            canvas_set_color(canvas, ColorBlack);
        }
    }
}

static void draw_buttons(Canvas* canvas, TextInputModel* model) {
    canvas_set_font(canvas, FontSecondary);
    const char* labels[ROW_COUNT] = {
        "DEL",
        model->layer == TextInputLayerLetters ? "SYM" : "ABC",
        "OK",
    };
    for(uint8_t row = 0; row < ROW_COUNT; row++) {
        uint8_t top = ROW_TOP[row];
        uint8_t h = ROW_BOX_H[row];
        bool selected = !model->in_text && model->in_buttons && model->selected_row == row;

        canvas_set_color(canvas, ColorBlack);
        if(selected) {
            canvas_draw_rbox(canvas, BTN_LEFT, top, BTN_W, h, 2);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, BTN_LEFT, top, BTN_W, h, 2);
        }
        uint16_t text_w = canvas_string_width(canvas, labels[row]);
        uint8_t text_x = BTN_LEFT + (uint8_t)((BTN_W - text_w) / 2);
        canvas_draw_str(canvas, text_x, top + h - 3, labels[row]);
        canvas_set_color(canvas, ColorBlack);
    }
}

static const char*
    text_field_scroll_start(Canvas* canvas, const char* full_text, size_t cursor, uint8_t max_width) {
    const char* text = full_text;
    while((size_t)(text - full_text) < cursor) {
        size_t seg_len = cursor - (size_t)(text - full_text);
        char segment[80];
        if(seg_len >= sizeof(segment)) seg_len = sizeof(segment) - 1;
        memcpy(segment, text, seg_len);
        segment[seg_len] = '\0';
        if(canvas_string_width(canvas, segment) <= max_width) break;
        text++;
    }
    return text;
}

static void draw_text_field(Canvas* canvas, TextInputModel* model) {

    canvas_set_font(canvas, FontSecondary);

    uint8_t needed_string_width = canvas_width(canvas) - 8;
    uint8_t start_pos = 4;

    const char* full_text = model->text_buffer ? model->text_buffer : "";
    size_t full_len = strlen(full_text);

    size_t cursor = model->cursor_pos > full_len ? full_len : model->cursor_pos;

    canvas_draw_str(canvas, 2, 7, model->header);
    if(model->in_text) {
        elements_slightly_rounded_frame(canvas, 0, 7, 128, 14);
    }
    elements_slightly_rounded_frame(canvas, 1, 8, 126, 12);

    const char* text = text_field_scroll_start(canvas, full_text, cursor, needed_string_width);

    if(text > full_text) {
        needed_string_width -= 8;
        text = text_field_scroll_start(canvas, full_text, cursor, needed_string_width);
        canvas_draw_str(canvas, start_pos, 17, "...");
        start_pos += 6;
    }

    char before[80];
    size_t n = cursor - (size_t)(text - full_text);
    if(n >= sizeof(before)) n = sizeof(before) - 1;
    memcpy(before, text, n);
    before[n] = '\0';
    uint16_t cursor_x = start_pos + canvas_string_width(canvas, before);

    if(model->clear_default_text) {

        elements_slightly_rounded_box(
            canvas, start_pos - 1, 9, canvas_string_width(canvas, text) + 2, 10);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, start_pos, 17, text);
    } else if(model->in_text) {
        canvas_draw_str(canvas, start_pos, 17, text);
        canvas_draw_box(canvas, cursor_x, 10, 1, 9);
    } else {

        canvas_draw_str(canvas, cursor_x + 1, 18, "|");
        canvas_draw_str(canvas, start_pos, 17, text);
    }
    canvas_set_color(canvas, ColorBlack);
}

static void text_input_view_draw_callback(Canvas* canvas, void* _model) {
    TextInputModel* model = _model;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    draw_text_field(canvas, model);
    draw_grid(canvas, model);
    draw_buttons(canvas, model);

    if(model->validator_message_visible) {
        canvas_set_font(canvas, FontSecondary);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 8, 10, 110, 48);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rframe(canvas, 8, 8, 112, 50, 3);
        canvas_draw_rframe(canvas, 9, 9, 110, 48, 2);
        elements_multiline_text(canvas, 14, 20, furi_string_get_cstr(model->validator_text));
    }
}

static void text_input_handle_up(TextInputModel* model) {

    model->clear_default_text = false;
    if(model->in_text) return;
    if(model->selected_row > 0) {
        model->selected_row--;
        if(!model->in_buttons) text_input_clamp_column(model);
        return;
    }

    model->in_text = true;
}

static void text_input_handle_down(TextInputModel* model) {
    model->clear_default_text = false;
    if(model->in_text) {
        model->in_text = false;
        return;
    }
    if(model->selected_row < ROW_COUNT - 1) {
        model->selected_row++;
        if(!model->in_buttons) text_input_clamp_column(model);
    }
}

static void text_input_handle_left(TextInputModel* model) {
    model->clear_default_text = false;
    if(model->in_text) {
        if(model->cursor_pos > 0) model->cursor_pos--;
        return;
    }
    if(model->in_buttons) {

        model->in_buttons = false;
        model->selected_column = get_row_cols(model->layer, model->selected_row) - 1;
    } else if(model->selected_column > 0) {
        model->selected_column--;
    } else {
        model->in_buttons = true;
    }
}

static void text_input_handle_right(TextInputModel* model) {
    model->clear_default_text = false;
    if(model->in_text) {
        size_t len = strlen(model->text_buffer);
        if(model->cursor_pos < len) model->cursor_pos++;
        return;
    }
    if(model->in_buttons) {
        model->in_buttons = false;
        model->selected_column = 0;
    } else if(model->selected_column < get_row_cols(model->layer, model->selected_row) - 1) {
        model->selected_column++;
    } else {
        model->in_buttons = true;
    }
}

static void
    text_input_handle_ok(TextInput* text_input, TextInputModel* model, bool shift) {
    if(model->in_text) {
        text_input_delete(model);
        return;
    }
    if(model->in_buttons) {
        switch(model->selected_row) {
        case 0:
            text_input_delete(model);
            break;
        case 1:
            model->layer = (model->layer == TextInputLayerLetters) ? TextInputLayerSymbols :
                                                                       TextInputLayerLetters;
            break;
        case 2: {
            size_t text_length = strlen(model->text_buffer);
            if(model->validator_callback &&
               (!model->validator_callback(
                   model->text_buffer, model->validator_text, model->validator_callback_context))) {
                model->validator_message_visible = true;
                furi_timer_start(text_input->timer, furi_kernel_get_tick_frequency() * 4);
            } else if(model->callback != 0 && text_length >= model->minimum_length) {
                model->callback(model->callback_context);
            }
            break;
        }
        }
        return;
    }

    char selected = get_char(model->layer, model->selected_row, model->selected_column);
    if(selected == ' ' && shift) {
        selected = '_';
    } else if(shift) {
        selected = char_shift(selected);
    }

    size_t text_length = strlen(model->text_buffer);
    size_t insert_at = model->cursor_pos;
    if(insert_at > text_length) insert_at = text_length;

    if(text_length < (model->text_buffer_size - 1)) {
        memmove(
            model->text_buffer + insert_at + 1,
            model->text_buffer + insert_at,
            text_length - insert_at + 1);
        model->text_buffer[insert_at] = selected;
        model->cursor_pos = insert_at + 1;

        if(text_length + 1 >= model->text_buffer_size - 1) {
            model->in_buttons = true;
            model->selected_row = 2;
        }
    }
    model->clear_default_text = false;
}

static bool text_input_view_input_callback(InputEvent* event, void* context) {
    TextInput* text_input = context;
    furi_assert(text_input);

    bool consumed = false;

    TextInputModel* model = view_get_model(text_input->view);

    if((!(event->type == InputTypePress) && !(event->type == InputTypeRelease)) &&
       model->validator_message_visible) {
        model->validator_message_visible = false;
        consumed = true;
    } else if(event->type == InputTypeShort) {
        consumed = true;
        switch(event->key) {
        case InputKeyUp:
            text_input_handle_up(model);
            break;
        case InputKeyDown:
            text_input_handle_down(model);
            break;
        case InputKeyLeft:
            text_input_handle_left(model);
            break;
        case InputKeyRight:
            text_input_handle_right(model);
            break;
        case InputKeyOk:
            text_input_handle_ok(text_input, model, false);
            break;
        default:
            consumed = false;
            break;
        }
    } else if(event->type == InputTypeLong) {
        consumed = true;
        switch(event->key) {
        case InputKeyUp:
            text_input_handle_up(model);
            break;
        case InputKeyDown:
            text_input_handle_down(model);
            break;
        case InputKeyLeft:
            text_input_handle_left(model);
            break;
        case InputKeyRight:
            text_input_handle_right(model);
            break;
        case InputKeyOk:
            text_input_handle_ok(text_input, model, true);
            break;
        case InputKeyBack:
            text_input_delete(model);
            break;
        default:
            consumed = false;
            break;
        }
    } else if(event->type == InputTypeRepeat) {
        consumed = true;
        switch(event->key) {
        case InputKeyUp:
            text_input_handle_up(model);
            break;
        case InputKeyDown:
            text_input_handle_down(model);
            break;
        case InputKeyLeft:
            text_input_handle_left(model);
            break;
        case InputKeyRight:
            text_input_handle_right(model);
            break;
        case InputKeyBack:
            text_input_delete(model);
            break;
        default:
            consumed = false;
            break;
        }
    }

    view_commit_model(text_input->view, consumed);

    return consumed;
}

void text_input_timer_callback(void* context) {
    furi_assert(context);
    TextInput* text_input = context;

    with_view_model(
        text_input->view,
        TextInputModel * model,
        { model->validator_message_visible = false; },
        true);
}

static void text_input_blink_timer_callback(void* context) {
    furi_assert(context);
    TextInput* text_input = context;

    with_view_model(
        text_input->view,
        TextInputModel * model,
        { model->space_blink = !model->space_blink; },
        true);
}

static void text_input_view_enter_callback(void* context) {
    furi_assert(context);
    TextInput* text_input = context;
    furi_timer_start(text_input->blink_timer, furi_ms_to_ticks(500));

    with_view_model(
        text_input->view,
        TextInputModel * model,
        {
            model->in_text = false;
            model->cursor_pos = model->text_buffer ? strlen(model->text_buffer) : 0;

            model->in_buttons = true;
            model->selected_row = 2;
            model->selected_column = 0;
        },
        true);
}

static void text_input_view_exit_callback(void* context) {
    furi_assert(context);
    TextInput* text_input = context;
    furi_timer_stop(text_input->blink_timer);
}

TextInput* text_input_alloc(void) {
    TextInput* text_input = malloc(sizeof(TextInput));
    text_input->view = view_alloc();
    view_set_context(text_input->view, text_input);
    view_allocate_model(text_input->view, ViewModelTypeLocking, sizeof(TextInputModel));
    view_set_draw_callback(text_input->view, text_input_view_draw_callback);
    view_set_input_callback(text_input->view, text_input_view_input_callback);
    view_set_enter_callback(text_input->view, text_input_view_enter_callback);
    view_set_exit_callback(text_input->view, text_input_view_exit_callback);

    text_input->timer = furi_timer_alloc(text_input_timer_callback, FuriTimerTypeOnce, text_input);
    text_input->blink_timer =
        furi_timer_alloc(text_input_blink_timer_callback, FuriTimerTypePeriodic, text_input);

    with_view_model(
        text_input->view,
        TextInputModel * model,
        { model->validator_text = furi_string_alloc(); },
        false);

    text_input_reset(text_input);

    return text_input;
}

void text_input_free(TextInput* text_input) {
    furi_check(text_input);
    with_view_model(
        text_input->view,
        TextInputModel * model,
        { furi_string_free(model->validator_text); },
        false);

    furi_timer_stop(text_input->timer);
    furi_timer_free(text_input->timer);
    furi_timer_stop(text_input->blink_timer);
    furi_timer_free(text_input->blink_timer);

    view_free(text_input->view);

    free(text_input);
}

void text_input_reset(TextInput* text_input) {
    furi_check(text_input);
    with_view_model(
        text_input->view,
        TextInputModel * model,
        {
            model->header = "";
            model->layer = TextInputLayerLetters;
            model->in_buttons = false;
            model->in_text = false;
            model->cursor_pos = 0;
            model->selected_row = 0;
            model->selected_column = 0;
            model->minimum_length = 1;
            model->clear_default_text = false;
            model->text_buffer = NULL;
            model->text_buffer_size = 0;
            model->callback = NULL;
            model->callback_context = NULL;
            model->validator_callback = NULL;
            model->validator_callback_context = NULL;
            furi_string_reset(model->validator_text);
            model->validator_message_visible = false;
        },
        true);
}

View* text_input_get_view(TextInput* text_input) {
    furi_check(text_input);
    return text_input->view;
}

void text_input_set_result_callback(
    TextInput* text_input,
    TextInputCallback callback,
    void* callback_context,
    char* text_buffer,
    size_t text_buffer_size,
    bool clear_default_text) {
    furi_check(text_input);
    with_view_model(
        text_input->view,
        TextInputModel * model,
        {
            model->callback = callback;
            model->callback_context = callback_context;
            model->text_buffer = text_buffer;
            model->text_buffer_size = text_buffer_size;
            // Fox: never pre-select/highlight the default text. The cursor starts at the end (set below)
            // with the OK button focused (see enter_callback), so you can type to append, Backspace to
            // delete from the end, or just press OK to keep the name as-is. (Ignoring the caller's flag
            // also removes the "first Backspace only de-selects" quirk the companion had to work around.)
            (void)clear_default_text;
            model->clear_default_text = false;
            model->layer = TextInputLayerLetters;
            model->in_text = false;

            model->cursor_pos = text_buffer ? strlen(text_buffer) : 0;
            model->in_buttons = false;
            model->selected_row = 0;
            model->selected_column = 0;
        },
        true);
}

void text_input_set_minimum_length(TextInput* text_input, size_t minimum_length) {
    with_view_model(
        text_input->view,
        TextInputModel * model,
        { model->minimum_length = minimum_length; },
        true);
}

void text_input_set_validator(
    TextInput* text_input,
    TextInputValidatorCallback callback,
    void* callback_context) {
    furi_check(text_input);
    with_view_model(
        text_input->view,
        TextInputModel * model,
        {
            model->validator_callback = callback;
            model->validator_callback_context = callback_context;
        },
        true);
}

TextInputValidatorCallback text_input_get_validator_callback(TextInput* text_input) {
    furi_check(text_input);
    TextInputValidatorCallback validator_callback = NULL;
    with_view_model(
        text_input->view,
        TextInputModel * model,
        { validator_callback = model->validator_callback; },
        false);
    return validator_callback;
}

void* text_input_get_validator_callback_context(TextInput* text_input) {
    furi_check(text_input);
    void* validator_callback_context = NULL;
    with_view_model(
        text_input->view,
        TextInputModel * model,
        { validator_callback_context = model->validator_callback_context; },
        false);
    return validator_callback_context;
}

void text_input_set_header_text(TextInput* text_input, const char* text) {
    furi_check(text_input);
    with_view_model(text_input->view, TextInputModel * model, { model->header = text; }, true);
}
