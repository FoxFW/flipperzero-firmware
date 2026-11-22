#include "saflok_date_input.h"
#include "icons/assets_icons.h"

#include <furi.h>
#include <core/log.h>
#include <core/string.h>
#include <gui/elements.h>
#include "assets_icons.h"
#include <locale/locale.h>

#define TAG "DateInput"

typedef enum {
    EditStateNone,
    EditStateActive,
    EditStateActiveEditing,
} EditState;

#define get_state(m, r, c)                                           \
    ((m)->row == (r) && (m)->column == (c) ?                         \
         ((m)->editing ? EditStateActiveEditing : EditStateActive) : \
         EditStateNone)

#define ROW_0_Y (10)
#define ROW_0_H (20)

#define ROW_1_Y (40)
#define ROW_1_H (20)

#define ROW_COUNT    2
#define COLUMN_COUNT 3

static inline void saflok_date_input_cleanup_date(DateTime* dt) {
    uint8_t day_per_month = datetime_get_days_per_month(datetime_is_leap_year(dt->year), dt->month);
    if(dt->day > day_per_month) {
        dt->day = day_per_month;
    }
}

bool date_input_init_from_date(DateInputModel* model, SaflokDateTime* date) {
    if (!model || !date) return false;

    model->date.year = date->year;
    model->date.month = date->month;
    model->date.day = date->day;
    model->date.hour = date->hour;
    model->date.minute = date->minute;
    model->date.second = 0;
    model->row = 0;
    model->column = 0;
    model->editing = false;

    return true;
}

bool date_input_extract_to_date(DateInputModel* model, SaflokDateTime* date) {
    if (!model || !date) return false;

    date->year = model->date.year;
    date->month = model->date.month;
    date->day = model->date.day;
    date->hour = model->date.hour;
    date->minute = model->date.minute;

    return true;
}

bool date_has_changed(SaflokDateTime* original, SaflokDateTime* new_date) {
    if (!original || !new_date) return false;

    return (original->year != new_date->year) ||
           (original->month != new_date->month) ||
           (original->day != new_date->day) ||
           (original->hour != new_date->hour) ||
           (original->minute != new_date->minute);
}
static inline void saflok_date_input_draw_block(Canvas* canvas, int32_t x, int32_t y, size_t w, size_t h, Font font, EditState state, const char* text) {
    furi_assert(canvas);
    furi_assert(text);

    canvas_set_color(canvas, ColorBlack);
    if(state != EditStateNone) {
        if(state == EditStateActiveEditing) {
            canvas_draw_icon(canvas, x + w / 2 - 2, y - 1 - 3, &I_SmallArrowUp_5x3);
            canvas_draw_icon(canvas, x + w / 2 - 2, y + h + 1, &I_SmallArrowDown_5x3);
        }
        canvas_draw_rbox(canvas, x, y, w, h, 1);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_rframe(canvas, x, y, w, h, 1);
    }

    canvas_set_font(canvas, font);
    canvas_draw_str_aligned(canvas, x + w / 2, y + h / 2, AlignCenter, AlignCenter, text);

    if(state != EditStateNone) {
        canvas_set_color(canvas, ColorBlack);
    }
}

static void saflok_date_input_draw_date_callback(Canvas* canvas, DateInputModel* model) {
    char buffer[64];

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, ROW_0_Y - 2, "     Y   Y   Y   Y        M   M      D    D");
    canvas_set_font(canvas, FontPrimary);
    snprintf(buffer, sizeof(buffer), "%04u", model->date.year);
    saflok_date_input_draw_block(canvas, 2, ROW_0_Y, 56, ROW_0_H, FontBigNumbers, get_state(model, 0, 0), buffer);
    snprintf(buffer, sizeof(buffer), "%02u", model->date.month);
    saflok_date_input_draw_block(canvas, 64, ROW_0_Y, 28, ROW_0_H, FontBigNumbers, get_state(model, 0, 1), buffer);
    canvas_draw_box(canvas, 64 - 5, ROW_0_Y + (ROW_0_H / 2), 4, 2);
    snprintf(buffer, sizeof(buffer), "%02u", model->date.day);
    saflok_date_input_draw_block(canvas, 98, ROW_0_Y, 28, ROW_0_H, FontBigNumbers, get_state(model, 0, 2), buffer);
    canvas_draw_box(canvas, 98 - 5, ROW_0_Y + (ROW_0_H / 2), 4, 2);
}

static void saflok_date_input_draw_time_callback(Canvas* canvas, DateInputModel* model) {
    char buffer[64];

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, ROW_1_Y - 2, "                 H    H       M   M      S    S");
    canvas_set_font(canvas, FontPrimary);

    snprintf(buffer, sizeof(buffer), "%02u", model->date.hour);
    saflok_date_input_draw_block(canvas, 30, ROW_1_Y, 28, ROW_1_H, FontBigNumbers, get_state(model, 1, 0), buffer);
    canvas_draw_box(canvas, 60, ROW_1_Y + ROW_1_H - 7, 2, 2);
    canvas_draw_box(canvas, 60, ROW_1_Y + ROW_1_H - 7 - 6, 2, 2);

    snprintf(buffer, sizeof(buffer), "%02u", model->date.minute);
    saflok_date_input_draw_block(canvas, 64, ROW_1_Y, 28, ROW_1_H, FontBigNumbers, get_state(model, 1, 1), buffer);
    canvas_draw_box(canvas, 94, ROW_1_Y + ROW_1_H - 7, 2, 2);
    canvas_draw_box(canvas, 94, ROW_1_Y + ROW_1_H - 7 - 6, 2, 2);

    snprintf(buffer, sizeof(buffer), "%02u", model->date.second);
    saflok_date_input_draw_block(canvas, 98, ROW_1_Y, 28, ROW_1_H, FontBigNumbers, get_state(model, 1, 2), buffer);
}

static void saflok_date_input_draw_callback(Canvas* canvas, void* ctx) {
    furi_assert(canvas != NULL);
    furi_assert(ctx != NULL);

    DateInputModel* model = ctx;
    saflok_date_input_draw_time_callback(canvas, model);
    saflok_date_input_draw_date_callback(canvas, model);

}

static bool saflok_date_input_navigation_callback(InputEvent* event, DateInputModel* model) {
    if(event->key == InputKeyOk) {
        model->editing = !model->editing;
        return true;
    }

    if(model->editing) {
        return false;
    }

    if(event->key == InputKeyLeft) {
        if(model->column > 0) {
            model->column--;
            return true;
        } else if (model->row > 0) {
            model->column = 2;
            model->row--;
            return true;
        }
    } else if(event->key == InputKeyRight) {
        if(model->column < 2) {
            model->column++;
            return true;
        } else if (model->row < 1){
            model->column = 0;
            model->row++;
            return true;
        }
    } else if(event->key == InputKeyUp) {
        if(model->row > 0) {
            model->row--;
            return true;
        }
    } else if(event->key == InputKeyDown) {
        if(model->row < 1) {
            model->row++;
            return true;
        }
    }

    return false;
}

static bool saflok_date_input_time_input_callback(InputEvent* event, DateInputModel* model) {
    bool value_changed = false;

    if(event->key == InputKeyOk) {
        model->editing = !model->editing;
        return true;
    }

    if(!model->editing) {
        return saflok_date_input_navigation_callback(event, model);
    }

    if(event->key == InputKeyUp) {
        value_changed = true;
        if(model->column == 0) {
            model->date.hour = (model->date.hour + 1) % 24;
        } else if(model->column == 1) {
            model->date.minute = (model->date.minute + 1) % 60;
        } else if(model->column == 2) {
            model->date.second = (model->date.second + 1) % 60;
        }
    } else if(event->key == InputKeyDown) {
        value_changed = true;
        if(model->column == 0) {
            model->date.hour = (model->date.hour > 0) ? model->date.hour - 1 : 23;
        } else if(model->column == 1) {
            model->date.minute = (model->date.minute > 0) ? model->date.minute - 1 : 59;
        } else if(model->column == 2) {
            model->date.second = (model->date.second > 0) ? model->date.second - 1 : 59;
        }
    } else if(event->key == InputKeyLeft || event->key == InputKeyRight) {
        return saflok_date_input_navigation_callback(event, model);
    }

    return value_changed;
}

static bool saflok_date_input_date_input_callback(InputEvent* event, DateInputModel* model) {
    bool value_changed = false;

    if(event->key == InputKeyOk) {
        model->editing = !model->editing;
        return true;
    }

    if(!model->editing) {
        return saflok_date_input_navigation_callback(event, model);
    }

    if(event->key == InputKeyUp) {
        value_changed = true;
        if(model->column == 2) {
            if(model->date.day < 31) model->date.day++;
        } else if(model->column == 1) {
            if(model->date.month < 12) model->date.month++;
        } else if(model->column == 0) {
            if(model->date.year < 2099) model->date.year++;
        }
    } else if(event->key == InputKeyDown) {
        value_changed = true;
        if(model->column == 2) {
            if(model->date.day > 1) model->date.day--;
        } else if(model->column == 1) {
            if(model->date.month > 1) model->date.month--;
        } else if(model->column == 0) {
            if(model->date.year > 1979) model->date.year--;
        }
    } else if(event->key == InputKeyLeft || event->key == InputKeyRight) {
        return saflok_date_input_navigation_callback(event, model);
    }

    if(value_changed) {
        saflok_date_input_cleanup_date(&model->date);
    }
    return value_changed;
}

static bool saflok_date_input_input_callback(InputEvent* event, void* context) {
    furi_assert(context);
    DateInput* date_input = context;
    bool consumed = false;
    bool model_changed = false;

    with_view_model(
        date_input->view,
        DateInputModel* model,
        {
            if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
                bool previous_editing = model->editing;
                if(model->editing) {
                    if(model->row == 0) {
                        consumed = saflok_date_input_date_input_callback(event, model);
                        if(consumed) model_changed = true;
                    } else if(model->row == 1) {
                        consumed = saflok_date_input_time_input_callback(event, model);
                        if(consumed) model_changed = true;
                    } else {
                        furi_crash();
                    }
                } else {
                    consumed = saflok_date_input_navigation_callback(event, model);
                    if(consumed) model_changed = true;
                }

                if(model->editing != previous_editing) {
                    model_changed = true;
                }
            }
        },
        model_changed);

    return consumed;
}

static void saflok_date_input_enter_callback(void* context) {
    furi_assert(context);
}

static void saflok_date_input_exit_callback(void* context) {
    furi_assert(context);
    DateInput* date_input = context;
    if(date_input->timer) {
        furi_event_loop_timer_stop(date_input->timer);
    }
}

DateInput* saflok_date_input_alloc() {

    DateInput* date_input = malloc(sizeof(DateInput));

    date_input->view = view_alloc();

    date_input->timer = NULL;

    view_set_context(date_input->view, date_input);

    view_allocate_model(date_input->view, ViewModelTypeLocking, sizeof(DateInputModel));

    view_set_draw_callback(date_input->view, saflok_date_input_draw_callback);
    view_set_input_callback(date_input->view, saflok_date_input_input_callback);
    view_set_enter_callback(date_input->view, saflok_date_input_enter_callback);
    view_set_exit_callback(date_input->view, saflok_date_input_exit_callback);

    with_view_model(
        date_input->view,
        DateInputModel* model,
        {
            model->row = 0;
            model->column = 0;
            model->editing = false;
            model->date.year = 2024;
            model->date.month = 1;
            model->date.day = 1;
            model->date.hour = 0;
            model->date.minute = 0;
            model->date.second = 0;
        },
        false);

    return date_input;
}

void saflok_date_input_reset(DateInput* date_input) {
    furi_assert(date_input);

    with_view_model(
        date_input->view,
        DateInputModel * model,
        {
            model->row = 0;
            model->column = 0;
            model->editing = false;
            model->date.year = 2020;
            model->date.month = 1;
            model->date.day = 1;
            model->date.hour = 0;
            model->date.minute = 0;
            model->date.second = 0;
        },
        false);
}

void saflok_date_input_free(DateInput* date_input) {
    furi_assert(date_input);
    view_free(date_input->view);
    free(date_input);
}

View* saflok_date_input_get_view(DateInput* date_input) {
    furi_assert(date_input);
    return date_input->view;
}
