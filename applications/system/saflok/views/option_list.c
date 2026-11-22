#include "option_list.h"
#include "icons/assets_icons.h"
#include <gui/elements.h>
#include <gui/canvas.h>
#include <furi.h>
#include "assets_icons.h"
#include "saflok_icons.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TAG "OptionList"

#define ITEM_HEIGHT            14
#define VALUE_BUTTON_HEIGHT    12
#define BUTTON_PADDING         8
#define MIN_BUTTON_WIDTH       23
#define RIGHT_MARGIN           7
#define HEADER_HEIGHT          12
#define SUBHEADER_HEIGHT       10
#define SCREEN_WIDTH           128
#define SCREEN_HEIGHT          64
#define MAX_ENTRIES            32

typedef struct {
    OptionEntry** entries;
    uint8_t entries_count;
    uint8_t entries_capacity;

    uint8_t position;
    uint8_t window_position;

    FuriString* header;
    FuriString* subheader;

    size_t scroll_counter;
    bool locked_message_visible;
    bool in_callback;
} OptionListModel;

static void option_list_draw_callback(Canvas* canvas, void* _model);
static bool option_list_input_callback(InputEvent* event, void* context);
static void option_list_scroll_timer_callback(void* context);
static void option_list_locked_timer_callback(void* context);

static void draw_scrollbar(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    uint8_t height,
    uint8_t position,
    uint8_t total) {

    uint8_t scroll_height = MAX(height / total, 6);
    uint8_t scroll_position = (height - scroll_height) * position / MAX(1, total - 1);

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, x, y + scroll_position, 2, scroll_height);
}

static void draw_value_button(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    uint8_t width,
    uint8_t height,
    const char* text,
    bool selected,
    bool locked,
    size_t scroll_counter) {

    if(locked) {
        elements_slightly_rounded_frame(canvas, x, y, width, height);
        canvas_draw_icon(canvas, x + width - 10, y + (height/2) - 4, &I_Lock_7x8);
    } else {
        FuriString* temp_str = furi_string_alloc_set(text);

        if(selected) {
            elements_slightly_rounded_box(canvas, x, y, width, height);
            canvas_set_color(canvas, ColorWhite);
        } else {
            elements_slightly_rounded_frame(canvas, x, y, width, height);
            canvas_set_color(canvas, ColorBlack);
        }

        elements_scrollable_text_line_str(
            canvas,
            x + (width / 2),
            y + height - 3,
            width - BUTTON_PADDING,
            furi_string_get_cstr(temp_str),
            selected ? scroll_counter : 0,
            false,
            true);

        furi_string_free(temp_str);
    }
}

static void draw_spacer_entry(Canvas* canvas, uint8_t y, const char* label) {
    if(!label || strlen(label) == 0) {
        return;
    }

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_str(canvas, 6, y + 4, label);
}

static void draw_option_entry(
    Canvas* canvas,
    OptionEntry* entry,
    uint8_t y,
    bool selected,
    size_t scroll_counter) {

    if(entry->spacer) {
        draw_spacer_entry(canvas, y, furi_string_get_cstr(entry->label));
        return;
    }

    uint8_t row_center = y + (ITEM_HEIGHT / 2);

    if(selected) {
        if(scroll_counter < 1) {
            scroll_counter = 0;
        } else {
            scroll_counter -= 1;
        }
    }

    uint8_t max_value_width = 0;
    for(uint8_t i = 0; i < entry->values_count; i++) {
        const char* value_text = entry->values[i].display_text;
        uint8_t text_width = canvas_string_width(canvas, value_text);
        if(text_width > max_value_width) {
            max_value_width = text_width;
        }
    }

    uint8_t button_width = MAX(MIN_BUTTON_WIDTH, max_value_width + BUTTON_PADDING);
    uint8_t button_x = SCREEN_WIDTH - button_width - RIGHT_MARGIN;
    uint8_t button_y = row_center - (VALUE_BUTTON_HEIGHT / 2);

    uint8_t label_width = button_x - 10;

    canvas_set_color(canvas, ColorBlack);
    elements_scrollable_text_line(
        canvas,
        6,
        row_center + 4,
        label_width,
        entry->label,
        scroll_counter,
        !selected);

    if(selected && entry->values_count > 1) {
        if(entry->current_value < (entry->values_count - 1)) {
            canvas_draw_icon(canvas, button_x + button_width + 3, row_center - 3, &I_SmallArrowRight_3x5);
        }

        if(entry->current_value > 0) {
            canvas_draw_icon(canvas, button_x - 6, row_center - 3, &I_SmallArrowLeft_3x5);
        }
    }

    const char* current_value_text = "";
    if(entry->values_count > 0 && entry->current_value < entry->values_count) {
        current_value_text = entry->values[entry->current_value].display_text;
    }

    draw_value_button(
        canvas,
        button_x,
        button_y,
        button_width,
        VALUE_BUTTON_HEIGHT,
        current_value_text,
        selected,
        entry->locked,
        scroll_counter);
}

static void draw_locked_message(Canvas* canvas, const char* message) {
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 8, 10, 110, 48);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_icon(canvas, 10, 14, &I_WarningDolphin_45x42);
    canvas_draw_rframe(canvas, 8, 8, 112, 50, 3);
    canvas_draw_rframe(canvas, 9, 9, 110, 48, 2);

    if(message) {
        elements_multiline_text_aligned(
            canvas,
            84,
            32,
            AlignCenter,
            AlignCenter,
            message);
    }
}

static void option_list_draw_callback(Canvas* canvas, void* _model) {
    OptionListModel* model = _model;

    canvas_clear(canvas);

    uint8_t y_offset = 0;

    if(!furi_string_empty(model->header)) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(
            canvas,
            SCREEN_WIDTH/2,
            0,
            AlignCenter,
            AlignTop,
            furi_string_get_cstr(model->header));
        y_offset += HEADER_HEIGHT;
    }

    if(!furi_string_empty(model->subheader)) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas,
            SCREEN_WIDTH/2,
            y_offset,
            AlignCenter,
            AlignTop,
            furi_string_get_cstr(model->subheader));
        y_offset += SUBHEADER_HEIGHT;
    }

    uint8_t fully_visible_items = (SCREEN_HEIGHT - y_offset) / ITEM_HEIGHT;
    uint8_t items_on_screen = fully_visible_items + 1;

    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = model->window_position;
        i < model->entries_count && i < model->window_position + items_on_screen;
        i++) {

        OptionEntry* entry = model->entries[i];
        uint8_t item_y = y_offset + ((i - model->window_position) * ITEM_HEIGHT);

        draw_option_entry(
            canvas,
            entry,
            item_y,
            (i == model->position),
            model->scroll_counter);
    }

    if(model->entries_count > fully_visible_items) {
        draw_scrollbar(
            canvas,
            SCREEN_WIDTH,
            y_offset,
            canvas_height(canvas) - y_offset,
            model->window_position,
            model->entries_count - fully_visible_items + 1);
    }

    if(model->locked_message_visible && model->position < model->entries_count) {
        OptionEntry* entry = model->entries[model->position];
        if(entry && entry->locked_message) {
            draw_locked_message(canvas, furi_string_get_cstr(entry->locked_message));
        }
    }
}

static void option_list_process_up(OptionList* option_list) {
    with_view_model(option_list->view, OptionListModel* model, {
        uint8_t fully_visible_items = (SCREEN_HEIGHT -
            (furi_string_empty(model->header) ? 0 : HEADER_HEIGHT) -
            (furi_string_empty(model->subheader) ? 0 : SUBHEADER_HEIGHT)) / ITEM_HEIGHT;

        if(model->position > 0) {
            model->position--;

            while(model->position > 0 && model->entries[model->position]->spacer) {
                model->position--;
            }

            if(model->position < model->window_position) {
                model->window_position = model->position;
            }
        } else {
            model->position = model->entries_count - 1;

            while(model->position > 0 && model->entries[model->position]->spacer) {
                model->position--;
            }

            if(model->entries_count > fully_visible_items) {
                model->window_position = model->position - (fully_visible_items - 1);

                if(model->window_position > model->position) {
                    model->window_position = 0;
                }
            } else {
                model->window_position = 0;
            }
        }

        model->scroll_counter = 0;
    }, true);
}

static void option_list_process_down(OptionList* option_list) {
    with_view_model(option_list->view, OptionListModel* model, {
        uint8_t fully_visible_items = (SCREEN_HEIGHT -
            (furi_string_empty(model->header) ? 0 : HEADER_HEIGHT) -
            (furi_string_empty(model->subheader) ? 0 : SUBHEADER_HEIGHT)) / ITEM_HEIGHT;

        if(model->position < (model->entries_count - 1)) {
            model->position++;

            while(model->position < (model->entries_count - 1) &&
                  model->entries[model->position]->spacer) {
                model->position++;
            }

            if((model->position - model->window_position) >= fully_visible_items) {
                model->window_position = model->position - fully_visible_items + 1;
            }
        } else {
            model->position = 0;

            while(model->position < (model->entries_count - 1) &&
                  model->entries[model->position]->spacer) {
                model->position++;
            }

            model->window_position = 0;
        }

        model->scroll_counter = 0;
    }, true);
}

static void option_list_process_left(OptionList* option_list) {
    with_view_model(option_list->view, OptionListModel* model, {
        if(model->position >= model->entries_count) return;

        OptionEntry* entry = model->entries[model->position];

        if(entry->locked) {
            model->locked_message_visible = true;
            furi_timer_start(option_list->locked_timer, furi_kernel_get_tick_frequency() * 3);
        } else if(entry->values_count > 0 && !model->in_callback) {
            uint8_t original_value = entry->current_value;
            uint8_t new_value = original_value;

            do {
                new_value = (new_value == 0) ? (entry->values_count - 1) : (new_value - 1);

                if(new_value == original_value) break;

            } while(!entry->values[new_value].enabled);

            if(entry->values[new_value].enabled && new_value != original_value) {
                entry->current_value = new_value;

                model->in_callback = true;

                view_commit_model(option_list->view, true);

                if(entry->change_callback) {
                    entry->change_callback(entry);
                }

                with_view_model(option_list->view, OptionListModel* model, {
                    model->in_callback = false;
                    model->scroll_counter = 0;
                }, true);

                return;
            }
        }

        model->scroll_counter = 0;
    }, true);
}

static void option_list_process_right(OptionList* option_list) {
    with_view_model(option_list->view, OptionListModel* model, {
        if(model->position >= model->entries_count) return;

        OptionEntry* entry = model->entries[model->position];

        if(entry->locked) {
            model->locked_message_visible = true;
            furi_timer_start(option_list->locked_timer, furi_kernel_get_tick_frequency() * 3);
        } else if(entry->values_count > 0 && !model->in_callback) {
            uint8_t original_value = entry->current_value;
            uint8_t new_value = original_value;

            do {
                new_value = (new_value + 1) % entry->values_count;

                if(new_value == original_value) break;

            } while(!entry->values[new_value].enabled);

            if(entry->values[new_value].enabled && new_value != original_value) {
                entry->current_value = new_value;

                model->in_callback = true;

                view_commit_model(option_list->view, true);

                if(entry->change_callback) {
                    entry->change_callback(entry);
                }

                with_view_model(option_list->view, OptionListModel* model, {
                    model->in_callback = false;
                    model->scroll_counter = 0;
                }, true);

                return;
            }
        }

        model->scroll_counter = 0;
    }, true);
}

static void option_list_process_ok(OptionList* option_list) {
    with_view_model(option_list->view, OptionListModel* model, {
        if(model->position >= model->entries_count) return;

        OptionEntry* entry = model->entries[model->position];

        if(entry->locked) {
            model->locked_message_visible = true;
            furi_timer_start(option_list->locked_timer, furi_kernel_get_tick_frequency() * 3);
        } else if(option_list->callback && !model->in_callback) {
            model->in_callback = true;

            view_commit_model(option_list->view, true);

            option_list->callback(option_list->context, model->position);

            with_view_model(option_list->view, OptionListModel* model, {
                model->in_callback = false;
            }, true);

            return;
        }
    }, true);
}

static bool option_list_input_callback(InputEvent* event, void* context) {
    OptionList* option_list = context;
    furi_assert(option_list);
    bool consumed = false;

    bool locked_message_visible = false;
    with_view_model(option_list->view, OptionListModel* model, {
        locked_message_visible = model->locked_message_visible;
    }, false);

    if((event->type != InputTypePress && event->type != InputTypeRelease) && locked_message_visible) {
        with_view_model(option_list->view, OptionListModel* model, {
            model->locked_message_visible = false;
        }, true);
        consumed = true;
    } else if(event->type == InputTypeShort) {
        switch(event->key) {
            case InputKeyUp:
                consumed = true;
                option_list_process_up(option_list);
                break;
            case InputKeyDown:
                consumed = true;
                option_list_process_down(option_list);
                break;
            case InputKeyLeft:
                consumed = true;
                option_list_process_left(option_list);
                break;
            case InputKeyRight:
                consumed = true;
                option_list_process_right(option_list);
                break;
            case InputKeyOk:
                consumed = true;
                option_list_process_ok(option_list);
                break;
            default:
                break;
        }
    } else if(event->type == InputTypeRepeat) {
        switch(event->key) {
            case InputKeyUp:
                consumed = true;
                option_list_process_up(option_list);
                break;
            case InputKeyDown:
                consumed = true;
                option_list_process_down(option_list);
                break;
            case InputKeyLeft:
                consumed = true;
                option_list_process_left(option_list);
                break;
            case InputKeyRight:
                consumed = true;
                option_list_process_right(option_list);
                break;
            default:
                break;
        }
    }

    return consumed;
}

static void option_list_scroll_timer_callback(void* context) {
    furi_assert(context);
    OptionList* option_list = context;

    with_view_model(option_list->view, OptionListModel* model, {
        model->scroll_counter++;
    }, true);
}

static void option_list_locked_timer_callback(void* context) {
    furi_assert(context);
    OptionList* option_list = context;

    with_view_model(option_list->view, OptionListModel* model, {
        model->locked_message_visible = false;
    }, true);
}

OptionEntry* option_entry_alloc(
    const char* label,
    uint8_t index,
    OptionEntryChangeCallback change_callback,
    void* context) {

    OptionEntry* entry = malloc(sizeof(OptionEntry));

    entry->label = furi_string_alloc();
    furi_string_set_str(entry->label, label ? label : "");

    entry->index = index;
    entry->values = NULL;
    entry->values_count = 0;
    entry->current_value = 0;
    entry->change_callback = change_callback;
    entry->context = context;
    entry->locked = false;
    entry->spacer = false;
    entry->locked_message = furi_string_alloc();

    return entry;
}

void option_entry_free(OptionEntry* entry) {
    if(!entry) return;

    if(entry->label) {
        furi_string_free(entry->label);
        entry->label = NULL;
    }

    if(entry->locked_message) {
        furi_string_free(entry->locked_message);
        entry->locked_message = NULL;
    }

    if(entry->values) {
        for(uint8_t i = 0; i < entry->values_count; i++) {
            if(entry->values[i].display_text) {
                free(entry->values[i].display_text);
            }
        }
        free(entry->values);
        entry->values = NULL;
    }

    free(entry);
}

void option_entry_add_int_value(OptionEntry* entry, const char* display_text, int32_t int_value, bool enabled) {
    if(entry == NULL) return;

    OptionValue* new_values = realloc(entry->values, (entry->values_count + 1) * sizeof(OptionValue));
    if(new_values == NULL) return;
    entry->values = new_values;

    OptionValue* value = &entry->values[entry->values_count];
    value->display_text = display_text ? strdup(display_text) : strdup("");
    value->int_value = int_value;
    value->enabled = enabled;

    entry->values_count++;
}

bool option_entry_set_current_index(OptionEntry* entry, uint8_t index) {
    if(entry == NULL || index >= entry->values_count) return false;

    if(!entry->values[index].enabled) return false;

    entry->current_value = index;

    return true;
}

uint8_t option_entry_get_current_index(const OptionEntry* entry) {
    if(entry == NULL) return 0;
    return entry->current_value;
}

int32_t option_entry_get_current_int_value(const OptionEntry* entry) {
    if(entry == NULL || entry->values_count == 0 || entry->current_value >= entry->values_count) {
        return 0;
    }

    return entry->values[entry->current_value].int_value;
}

void option_entry_set_locked(OptionEntry* entry, bool locked, const char* locked_message) {
    if(entry == NULL) return;

    entry->locked = locked;

    if(locked_message) {
        furi_string_set_str(entry->locked_message, locked_message);
    } else if(locked && furi_string_empty(entry->locked_message)) {
        furi_string_set_str(entry->locked_message, "Locked!");
    }
}

void* option_entry_get_context(const OptionEntry* entry) {
    if(entry == NULL) return NULL;
    return entry->context;
}

OptionList* option_list_alloc(void) {
    OptionList* option_list = malloc(sizeof(OptionList));

    option_list->view = view_alloc();
    view_set_context(option_list->view, option_list);
    view_allocate_model(option_list->view, ViewModelTypeLocking, sizeof(OptionListModel));
    view_set_draw_callback(option_list->view, option_list_draw_callback);
    view_set_input_callback(option_list->view, option_list_input_callback);

    option_list->locked_timer = furi_timer_alloc(
        option_list_locked_timer_callback, FuriTimerTypeOnce, option_list);

    option_list->scroll_timer = furi_timer_alloc(
        option_list_scroll_timer_callback, FuriTimerTypePeriodic, option_list);

    with_view_model(option_list->view, OptionListModel* model, {
        model->entries = malloc(MAX_ENTRIES * sizeof(OptionEntry*));
        model->entries_count = 0;
        model->entries_capacity = MAX_ENTRIES;
        model->position = 0;
        model->window_position = 0;
        model->header = furi_string_alloc();
        model->subheader = furi_string_alloc();
        model->scroll_counter = 0;
        model->locked_message_visible = false;
        model->in_callback = false;
    }, true);

    furi_timer_start(option_list->scroll_timer, 333);

    return option_list;
}

void option_list_free(OptionList* option_list) {
    furi_assert(option_list);

    furi_timer_stop(option_list->scroll_timer);
    furi_timer_stop(option_list->locked_timer);

    with_view_model(option_list->view, OptionListModel* model, {
        for(uint8_t i = 0; i < model->entries_count; i++) {
            option_entry_free(model->entries[i]);
        }

        free(model->entries);
        model->entries = NULL;
        model->entries_count = 0;

        furi_string_free(model->header);
        model->header = NULL;
        furi_string_free(model->subheader);
        model->subheader = NULL;
    }, false);

    furi_timer_free(option_list->scroll_timer);
    furi_timer_free(option_list->locked_timer);

    view_free(option_list->view);

    free(option_list);
}

void option_list_reset(OptionList* option_list) {
    furi_assert(option_list);

    furi_timer_stop(option_list->scroll_timer);
    furi_timer_stop(option_list->locked_timer);

    furi_delay_ms(5);

    with_view_model(option_list->view, OptionListModel* model, {
        model->locked_message_visible = false;

        for(uint8_t i = 0; i < model->entries_count; i++) {
            option_entry_free(model->entries[i]);
            model->entries[i] = NULL;
        }

        model->entries_count = 0;

        furi_string_reset(model->header);
        furi_string_reset(model->subheader);

        model->position = 0;
        model->window_position = 0;
        model->scroll_counter = 0;
        model->in_callback = false;
    }, true);

    furi_timer_start(option_list->scroll_timer, 333);
}

View* option_list_get_view(OptionList* option_list) {
    furi_assert(option_list);
    return option_list->view;
}

void option_list_add_entry(OptionList* option_list, OptionEntry* entry) {
    furi_assert(entry);
    furi_assert(option_list);

    with_view_model(option_list->view, OptionListModel* model, {
        if(model->entries_count < model->entries_capacity) {
            model->entries[model->entries_count++] = entry;
        }
    }, true);
}

OptionEntry* option_list_get_entry_by_index(OptionList* option_list, uint8_t index) {
    furi_assert(option_list);
    OptionEntry* found_entry = NULL;

    with_view_model(option_list->view, OptionListModel* model, {
        for(uint8_t i = 0; i < model->entries_count; i++) {
            if(model->entries[i]->index == index) {
                found_entry = model->entries[i];
                break;
            }
        }
    }, false);

    return found_entry;
}

void option_list_set_selected_item(OptionList* option_list, uint8_t index) {
    furi_assert(option_list);

    with_view_model(option_list->view, OptionListModel* model, {
        uint8_t position = 0;
        for(uint8_t i = 0; i < model->entries_count; i++) {
            if(model->entries[i]->index == index) {
                position = i;
                break;
            }
        }

        uint8_t fully_visible_items = (SCREEN_HEIGHT -
            (furi_string_empty(model->header) ? 0 : HEADER_HEIGHT) -
            (furi_string_empty(model->subheader) ? 0 : SUBHEADER_HEIGHT)) / ITEM_HEIGHT;

        model->position = position;

        if(position < model->window_position) {
            model->window_position = position;
        } else if((position - model->window_position) >= fully_visible_items) {
            model->window_position = position - fully_visible_items + 1;
        }
    }, true);
}

void option_list_set_header(OptionList* option_list, const char* header) {
    furi_assert(option_list);

    with_view_model(option_list->view, OptionListModel* model, {
        if(header == NULL) {
            furi_string_reset(model->header);
        } else {
            furi_string_set_str(model->header, header);
        }
    }, true);
}

void option_list_set_subheader(OptionList* option_list, const char* subheader) {
    furi_assert(option_list);

    with_view_model(option_list->view, OptionListModel* model, {
        if(subheader == NULL) {
            furi_string_reset(model->subheader);
        } else {
            furi_string_set_str(model->subheader, subheader);
        }
    }, true);
}

void option_list_set_enter_callback(
    OptionList* option_list,
    OptionListEnterCallback callback,
    void* context) {

    furi_assert(option_list);
    option_list->callback = callback;
    option_list->context = context;
}

OptionEntry* option_list_add_spacer(OptionList* option_list, bool divider, void* context) {
    const char* label = divider ? "--------------------------------" : "";

    furi_assert(option_list);

    OptionEntry* entry = option_entry_alloc(label, 0, NULL, context);
    entry->spacer = true;

    option_list_add_entry(option_list, entry);

    return entry;
}

