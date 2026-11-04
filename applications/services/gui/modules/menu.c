#include "menu.h"

#include <gui/elements.h>
#include <gui/icon_i.h>
#include <gui/icon_animation_i.h>
#include <assets_icons.h>
#include <furi.h>
#include <furi_hal.h>
#include <m-array.h>
#include <string.h>
#include "../../desktop/desktop_settings.h"
#include "fox_theme.h"

struct Menu {
    View* view;
    FuriTimer* scroll_timer;
};

typedef struct {
    const char* label;
    IconAnimation* icon;
    uint32_t index;
    MenuItemCallback callback;
    void* callback_context;
} MenuItem;

ARRAY_DEF(MenuItemArray, MenuItem, M_POD_OPLIST);
#define M_OPL_MenuItemArray_t() ARRAY_OPLIST(MenuItemArray, M_POD_OPLIST)

typedef struct {
    MenuItemArray_t items;
    size_t position;
    uint8_t cached_theme;
    size_t scroll_counter;
} MenuModel;

#define FOX_CELL_W   40
#define FOX_CELL_H   30
#define FOX_CELL_GAP  3
#define FOX_COLS      3
#define FOX_ROWS      2
#define FOX_VISIBLE  (FOX_COLS * FOX_ROWS)

#define TINY_CELL_W    24
#define TINY_CELL_H    16
#define TINY_GAP        2
#define TINY_COLS       5
#define TINY_ROWS       3
#define TINY_VISIBLE  (TINY_COLS * TINY_ROWS)
#define TINY_HEADER_H  10

static void menu_process_up(Menu* menu);
static void menu_process_down(Menu* menu);
static void menu_process_left(Menu* menu);
static void menu_process_right(Menu* menu);
static void menu_process_ok(Menu* menu);

static size_t fox_shift(size_t position, size_t count) {
    if(count <= FOX_VISIBLE) return 0;
    size_t col = position / FOX_ROWS;
    if(col < 1) return 0;
    size_t last_col = (count - 1) / FOX_ROWS;
    if(col + 1 >= last_col) {
        size_t start_col = (last_col + 1 > FOX_COLS) ? last_col + 1 - FOX_COLS : 0;
        return start_col * FOX_ROWS;
    }
    return (col - 1) * FOX_ROWS;
}

static size_t tiny_shift(size_t position, size_t count) {
    if(count <= TINY_VISIBLE) return 0;
    size_t col = position / TINY_ROWS;
    if(col < 1) return 0;
    size_t last_col = (count - 1) / TINY_ROWS;
    if(col + 1 >= last_col) {
        size_t start_col = (last_col + 1 > TINY_COLS) ? last_col + 1 - TINY_COLS : 0;
        return start_col * TINY_ROWS;
    }
    return (col - 1) * TINY_ROWS;
}

static size_t tiny_last_row_in_col(size_t col, size_t count) {
    size_t remaining = count - col * TINY_ROWS;
    size_t rows_here = (remaining < TINY_ROWS) ? remaining : TINY_ROWS;
    return rows_here - 1;
}

/* Max visible characters for a label in the Fox theme's small boxes. Other
 * themes draw item->label in full (they don't call this helper), so this cap
 * applies ONLY to the Fox theme. "Sub-GHz" (7) is a protected default that must
 * fit, so 7 is the proven box width. */
#define FOX_LABEL_MAX 7

static uint8_t s_menu_theme = 1;

static const char* menu_fox_label(const char* label) {
    if(!label) return label;
    if(s_menu_theme != 1) return label;
    if(strcmp(label, "Sub-GHz") == 0)             return "SubGhz";
    if(strcmp(label, "125 kHz RFID") == 0)        return "RFID";
    if(strcmp(label, "Fox Settings") == 0)         return "Fox";
    if(strcmp(label, "Sub-GHz Bruteforcer") == 0) return "S-Brute";
    if(strcmp(label, "Infrared") == 0)            return "IR";
    /* Hard-cap anything longer than the box (renamed / user-added / unmapped
     * long names) so it can't spill. Static scratch is safe: the Fox-theme draw
     * paths consume the returned string synchronously before the next call. */
    if(strlen(label) > FOX_LABEL_MAX) {
        static char buf[FOX_LABEL_MAX + 1];
        memcpy(buf, label, FOX_LABEL_MAX);
        buf[FOX_LABEL_MAX] = '\0';
        return buf;
    }
    return label;
}

static void menu_centered_icon(
    Canvas* canvas, MenuItem* item, size_t x, size_t y, size_t width, size_t height) {
    canvas_draw_icon_animation(
        canvas,
        x + (width - item->icon->icon->width) / 2,
        y + (height - item->icon->icon->height) / 2,
        item->icon);
}

static size_t menu_scroll_counter(MenuModel* model, bool selected) {
    if(!selected) return 0;
    size_t scroll_counter = model->scroll_counter;
    if(scroll_counter > 0) {
        scroll_counter--;
    }
    return scroll_counter;
}

static void menu_draw_callback(Canvas* canvas, void* _model) {
    MenuModel* model = _model;
    canvas_clear(canvas);

    size_t position    = model->position;
    size_t items_count = MenuItemArray_size(model->items);
    if(!items_count) {
        canvas_draw_str(canvas, 2, 32, "Empty");
        return;
    }

    uint8_t theme = fox_theme_get_style();
    s_menu_theme = theme;

    model->cached_theme = theme;

    if(theme == 1) {
            size_t shift = fox_shift(position, items_count);

        for(uint8_t cell = 0; cell < FOX_VISIBLE; cell++) {
            size_t item_idx = shift + cell;
            if(item_idx >= items_count) break;

            uint8_t col = cell / FOX_ROWS;
            uint8_t row = cell % FOX_ROWS;
            int32_t x   = 1 + col * (FOX_CELL_W + FOX_CELL_GAP);
            int32_t y   = row * (FOX_CELL_H + FOX_CELL_GAP);

            MenuItem* item = MenuItemArray_get(model->items, item_idx);
            elements_fox_horizontal_menu_item(
                canvas, x, y, FOX_CELL_W, FOX_CELL_H,
                menu_fox_label(item->label),
                item->icon,
                item_idx == position);
        }
    } else if(theme == 2) {

        {

            const char* name = furi_hal_version_get_name_ptr();
            canvas_set_font(canvas, FontPrimary);
            size_t name_w = canvas_string_width(canvas, name);
            size_t name_h = canvas_current_font_height(canvas);
            size_t box_w  = name_w + 6;
            size_t box_h  = name_h + 3;
            int32_t box_x = (128 - (int32_t)box_w) / 2;
            int32_t box_y = 0;
            int32_t bx1   = box_x + (int32_t)box_w - 1;
            int32_t by1   = box_y + (int32_t)box_h - 1;

            canvas_draw_box(canvas, box_x, box_y, 2, box_h);
            canvas_draw_box(canvas, bx1 - 1, box_y, 2, box_h);
            canvas_draw_box(canvas, box_x, by1 - 1, box_w, 2);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_dot(canvas, box_x, by1);
            canvas_draw_dot(canvas, bx1, by1);
            canvas_set_color(canvas, ColorBlack);

            canvas_draw_str_aligned(canvas, 64, 1, AlignCenter, AlignTop, name);
        }

        for(int8_t i = -1; i <= 4; i++) {
            size_t shift_position = position + i;
            if(shift_position >= items_count) continue;

            MenuItem* item = MenuItemArray_get(model->items, shift_position);
            size_t width = 20;
            size_t height = 20;
            size_t pos_x = 36;
            size_t pos_y = 27;
            if(i == 0) {
                width += 10;
                height += 10;
                pos_y += 2;
                canvas_draw_box(canvas, pos_x - width / 2, pos_y + height / 2, width, 9);
                canvas_set_color(canvas, ColorWhite);
                canvas_set_font(canvas, FontBatteryPercent);
                canvas_draw_str_aligned(
                    canvas, pos_x, pos_y + height / 2 + 1, AlignCenter, AlignTop, "Start");

                canvas_set_color(canvas, ColorBlack);
                canvas_set_font(canvas, FontSecondary);
                size_t scroll_counter = menu_scroll_counter(model, true);
                elements_scrollable_text_line_str(
                    canvas,
                    pos_x + width / 2 + 2,
                    pos_y + height / 2 + 7,
                    74,
                    menu_fox_label(item->label),
                    scroll_counter,
                    false,
                    false);
            } else {
                pos_x += (width + 1) * i + (i < 0 ? -6 : 6);
            }
            canvas_draw_frame(canvas, pos_x - width / 2, pos_y - height / 2, width, height);
            menu_centered_icon(canvas, item, pos_x - 7, pos_y - 7, 14, 14);
        }
        elements_scrollbar_horizontal(canvas, 0, 64, 128, position, items_count);
    } else if(theme == 3) {

        for(int8_t i = -2; i <= 2; i++) {
            size_t shift_position = (position + items_count + (size_t)i) % items_count;
            MenuItem* item = MenuItemArray_get(model->items, shift_position);
            size_t width = 24;
            size_t height = 26;
            int32_t pos_x = 64;
            int32_t pos_y = 36;
            if(i == 0) {
                width += 6;
                height += 4;
                elements_bold_rounded_frame(
                    canvas, pos_x - (int32_t)width / 2, pos_y - (int32_t)height / 2, width, height + 5);
                canvas_set_font(canvas, FontBatteryPercent);
                canvas_draw_str_aligned(
                    canvas, pos_x - 9, pos_y + (int32_t)height / 2 + 1, AlignCenter, AlignBottom, "S");
                canvas_draw_str_aligned(
                    canvas, pos_x, pos_y + (int32_t)height / 2 + 1, AlignCenter, AlignBottom, "TAR");
                canvas_draw_str_aligned(
                    canvas, pos_x + 9, pos_y + (int32_t)height / 2 + 1, AlignCenter, AlignBottom, "T");

                canvas_draw_rframe(canvas, 0, 0, 128, 18, 3);
                canvas_draw_line(canvas, 60, 18, 64, 26);
                canvas_draw_line(canvas, 64, 26, 68, 18);
                canvas_set_color(canvas, ColorWhite);
                canvas_draw_line(canvas, 60, 17, 68, 17);
                canvas_draw_box(canvas, 62, 21, 5, 2);
                canvas_set_color(canvas, ColorBlack);

                canvas_set_font(canvas, FontPrimary);
                size_t scroll_counter = menu_scroll_counter(model, true);
                elements_scrollable_text_line_str(
                    canvas,
                    (uint8_t)pos_x,
                    (uint8_t)(pos_y - (int32_t)height / 2 - 8),
                    124,
                    menu_fox_label(item->label),
                    scroll_counter,
                    false,
                    true);
            } else {
                pos_x += (int32_t)(width + 6) * i;
                pos_y += 2;
                elements_slightly_rounded_frame(
                    canvas, pos_x - (int32_t)width / 2, pos_y - (int32_t)height / 2, width, height);
            }
            menu_centered_icon(canvas, item, pos_x - 7, pos_y - 7, 14, 14);
        }
        elements_scrollbar_horizontal(canvas, 0, 64, 128, position, items_count);
    } else if(theme == 4) {

        MenuItem* selected = MenuItemArray_get(model->items, position);
        canvas_set_font(canvas, FontSecondary);
        size_t scroll_counter = menu_scroll_counter(model, true);
        elements_scrollable_text_line_str(
            canvas, 1, 8, 126, menu_fox_label(selected->label), scroll_counter, false, false);

        size_t shift = tiny_shift(position, items_count);
        for(uint8_t cell = 0; cell < TINY_VISIBLE; cell++) {
            size_t item_idx = shift + cell;
            if(item_idx >= items_count) break;

            uint8_t col = cell / TINY_ROWS;
            uint8_t row = cell % TINY_ROWS;
            int32_t x   = col * (TINY_CELL_W + TINY_GAP);
            int32_t y   = TINY_HEADER_H + row * (TINY_CELL_H + TINY_GAP);

            MenuItem* item = MenuItemArray_get(model->items, item_idx);
            if(item_idx == position) {
                canvas_draw_frame(canvas, x, y, TINY_CELL_W, TINY_CELL_H);
            }
            menu_centered_icon(canvas, item, x, y, TINY_CELL_W, TINY_CELL_H);
        }
    } else {
            MenuItem* item;
        size_t shift_position;

        canvas_set_font(canvas, FontSecondary);
        shift_position = (position + items_count - 1) % items_count;
        item = MenuItemArray_get(model->items, shift_position);
        canvas_draw_icon_animation(canvas, 4, 3, item->icon);
        canvas_draw_str(canvas, 22, 14, item->label);

        canvas_set_font(canvas, FontPrimary);
        shift_position = position;
        item = MenuItemArray_get(model->items, shift_position);
        canvas_draw_icon_animation(canvas, 4, 25, item->icon);
        canvas_draw_str(canvas, 22, 36, item->label);

        canvas_set_font(canvas, FontSecondary);
        shift_position = (position + 1) % items_count;
        item = MenuItemArray_get(model->items, shift_position);
        canvas_draw_icon_animation(canvas, 4, 47, item->icon);
        canvas_draw_str(canvas, 22, 58, item->label);

        elements_frame(canvas, 0, 21, 128 - 5, 21);
        elements_scrollbar(canvas, position, items_count);
    }
}

static bool menu_input_callback(InputEvent* event, void* context) {
    Menu* menu = context;
    bool consumed = false;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        switch(event->key) {
        case InputKeyUp:
            consumed = true;
            menu_process_up(menu);
            break;
        case InputKeyDown:
            consumed = true;
            menu_process_down(menu);
            break;
        case InputKeyLeft:
            consumed = true;
            menu_process_left(menu);
            break;
        case InputKeyRight:
            consumed = true;
            menu_process_right(menu);
            break;
        case InputKeyOk:
            if(event->type != InputTypeRepeat) {
                consumed = true;
                menu_process_ok(menu);
            }
            break;
        default:
            break;
        }
    }
    return consumed;
}

static void menu_scroll_timer_callback(void* context) {
    Menu* menu = context;
    with_view_model(menu->view, MenuModel* model, { model->scroll_counter++; }, true);
}

static void menu_enter(void* context) {
    Menu* menu = context;
    with_view_model(menu->view, MenuModel* model, {
        size_t count = MenuItemArray_size(model->items);
        if(count) {
            if(model->cached_theme == 1 || model->cached_theme == 4) {

                for(size_t i = 0; i < count; i++) {
                    icon_animation_start(MenuItemArray_get(model->items, i)->icon);
                }
            } else {

                icon_animation_start(MenuItemArray_get(model->items, model->position)->icon);
            }
        }
        model->scroll_counter = 0;
    }, false);
    furi_timer_start(menu->scroll_timer, 333);
}

static void menu_exit(void* context) {
    Menu* menu = context;
    with_view_model(menu->view, MenuModel* model, {
        size_t count = MenuItemArray_size(model->items);
        if(count) {
            if(model->cached_theme == 1 || model->cached_theme == 4) {

                for(size_t i = 0; i < count; i++) {
                    icon_animation_stop(MenuItemArray_get(model->items, i)->icon);
                }
            } else {
                icon_animation_stop(MenuItemArray_get(model->items, model->position)->icon);
            }
        }
    }, false);
    furi_timer_stop(menu->scroll_timer);
}

Menu* menu_alloc(void) {
    Menu* menu = malloc(sizeof(Menu));
    menu->view = view_alloc();
    view_set_context(menu->view, menu);
    view_allocate_model(menu->view, ViewModelTypeLocking, sizeof(MenuModel));
    view_set_draw_callback(menu->view, menu_draw_callback);
    view_set_input_callback(menu->view, menu_input_callback);
    view_set_enter_callback(menu->view, menu_enter);
    view_set_exit_callback(menu->view, menu_exit);

    menu->scroll_timer = furi_timer_alloc(menu_scroll_timer_callback, FuriTimerTypePeriodic, menu);

    with_view_model(menu->view, MenuModel* model, {
        MenuItemArray_init(model->items);
        model->position = 0;
        model->cached_theme = MenuThemeClassic;
    }, true);

    return menu;
}

void menu_free(Menu* menu) {
    furi_check(menu);
    menu_reset(menu);
    with_view_model(menu->view, MenuModel* model, {
        MenuItemArray_clear(model->items);
    }, false);
    view_free(menu->view);
    furi_timer_free(menu->scroll_timer);
    free(menu);
}

View* menu_get_view(Menu* menu) {
    furi_check(menu);
    return menu->view;
}

void menu_add_item(
    Menu* menu,
    const char* label,
    const Icon* icon,
    uint32_t index,
    MenuItemCallback callback,
    void* context) {
    furi_check(menu);
    furi_check(label);
    with_view_model(menu->view, MenuModel* model, {
        MenuItem* item = MenuItemArray_push_new(model->items);
        item->label            = label;
        item->icon             = icon ?
            icon_animation_alloc(icon) : icon_animation_alloc(&A_Plugins_14);
        view_tie_icon_animation(menu->view, item->icon);
        item->index            = index;
        item->callback         = callback;
        item->callback_context = context;
    }, true);
}

void menu_reset(Menu* menu) {
    furi_check(menu);
    with_view_model(menu->view, MenuModel* model, {
        for M_EACH(item, model->items, MenuItemArray_t) {
            icon_animation_stop(item->icon);
            icon_animation_free(item->icon);
        }
        MenuItemArray_reset(model->items);
        model->position = 0;
    }, true);
}

void menu_set_selected_item(Menu* menu, uint32_t index) {
    furi_check(menu);
    with_view_model(menu->view, MenuModel* model, {
        if(index < MenuItemArray_size(model->items))
            model->position = index;
    }, true);
}

void menu_set_theme(Menu* menu, uint8_t theme) {
    furi_check(menu);
    with_view_model(menu->view, MenuModel* model, {
        model->cached_theme = theme;
    }, true);
}

static void menu_change_position(Menu* menu, size_t new_pos) {
    with_view_model(menu->view, MenuModel* model, {
        size_t count = MenuItemArray_size(model->items);
        if(count > 0) {
            bool grid_theme = (model->cached_theme == 1 || model->cached_theme == 4);
            if(!grid_theme) {

                icon_animation_stop(MenuItemArray_get(model->items, model->position)->icon);
            }

            model->position = new_pos % count;
            if(!grid_theme) {
                icon_animation_start(MenuItemArray_get(model->items, model->position)->icon);
            }
        }
    }, true);
}

static void menu_process_up(Menu* menu) {
    size_t pos = 0, count = 0;
    uint8_t theme = 1;
    with_view_model(menu->view, MenuModel* model, {
        pos   = model->position;
        count = MenuItemArray_size(model->items);
        theme = model->cached_theme;
    }, false);
    if(!count) return;

    if(theme == 2 || theme == 3) return;

    size_t new_pos;
    if(theme == 1) {

        new_pos = (pos % FOX_ROWS == 0) ?
            ((pos + 1 < count) ? pos + 1 : pos) : pos - 1;
    } else if(theme == 4) {
        size_t row = pos % TINY_ROWS;
        if(row > 0) {
            new_pos = pos - 1;
        } else {
            size_t col = pos / TINY_ROWS;
            new_pos = col * TINY_ROWS + tiny_last_row_in_col(col, count);
        }
    } else {
        new_pos = (pos > 0) ? pos - 1 : count - 1;
    }
    menu_change_position(menu, new_pos);
}

static void menu_process_down(Menu* menu) {
    size_t pos = 0, count = 0;
    uint8_t theme = 1;
    with_view_model(menu->view, MenuModel* model, {
        pos   = model->position;
        count = MenuItemArray_size(model->items);
        theme = model->cached_theme;
    }, false);
    if(!count) return;

    if(theme == 2 || theme == 3) return;

    size_t new_pos;
    if(theme == 1) {
        new_pos = (pos % FOX_ROWS == 0) ?
            ((pos + 1 < count) ? pos + 1 : pos) : pos - 1;
    } else if(theme == 4) {
        size_t row = pos % TINY_ROWS;
        size_t col = pos / TINY_ROWS;
        size_t last_row = tiny_last_row_in_col(col, count);
        new_pos = (row < last_row) ? pos + 1 : col * TINY_ROWS;
    } else {
        new_pos = (pos + 1 < count) ? pos + 1 : 0;
    }
    menu_change_position(menu, new_pos);
}

static void menu_process_left(Menu* menu) {
    size_t pos = 0, count = 0;
    uint8_t theme = 1;
    with_view_model(menu->view, MenuModel* model, {
        pos   = model->position;
        count = MenuItemArray_size(model->items);
        theme = model->cached_theme;
    }, false);
    if(!count) return;

    size_t new_pos;
    if(theme == 1) {
        if(pos >= FOX_ROWS) {
            new_pos = pos - FOX_ROWS;
        } else {
            size_t last_col_start = ((count - 1) / FOX_ROWS) * FOX_ROWS;
            new_pos = last_col_start + (pos % FOX_ROWS);
            if(new_pos >= count) new_pos = last_col_start;
        }
    } else if(theme == 4) {
        if(pos >= TINY_ROWS) {
            new_pos = pos - TINY_ROWS;
        } else {
            size_t last_col_start = ((count - 1) / TINY_ROWS) * TINY_ROWS;
            new_pos = last_col_start + (pos % TINY_ROWS);
            if(new_pos >= count) new_pos = last_col_start;
        }
    } else {
        new_pos = (pos > 0) ? pos - 1 : count - 1;
    }
    menu_change_position(menu, new_pos);
}

static void menu_process_right(Menu* menu) {
    size_t pos = 0, count = 0;
    uint8_t theme = 1;
    with_view_model(menu->view, MenuModel* model, {
        pos   = model->position;
        count = MenuItemArray_size(model->items);
        theme = model->cached_theme;
    }, false);
    if(!count) return;

    size_t new_pos;
    if(theme == 1) {
        size_t candidate = pos + FOX_ROWS;
        new_pos = (candidate < count) ? candidate : (pos % FOX_ROWS);
    } else if(theme == 4) {
        size_t candidate = pos + TINY_ROWS;
        new_pos = (candidate < count) ? candidate : (pos % TINY_ROWS);
    } else {
        new_pos = (pos + 1 < count) ? pos + 1 : 0;
    }
    menu_change_position(menu, new_pos);
}

static void menu_process_ok(Menu* menu) {
    MenuItem* item = NULL;
    with_view_model(menu->view, MenuModel* model, {
        if(MenuItemArray_size(model->items))
            item = MenuItemArray_get(model->items, model->position);
    }, true);
    if(item && item->callback)
        item->callback(item->callback_context, item->index);
}
