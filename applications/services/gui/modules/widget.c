#include "widget.h"
#include "widget_elements/widget_element_i.h"
#include <furi.h>
#include <m-array.h>
#include <gui/canvas.h>
#include <gui/icon.h>
#include <assets_icons.h>
#include <string.h>

ARRAY_DEF(ElementArray, WidgetElement*, M_PTR_OPLIST);

struct Widget {
    View* view;
    void* context;
};

typedef struct {
    ElementArray_t element;
    uint8_t fox_focus;
    bool fox_focus_set;
} GuiWidgetModel;

#define WIDGET_FOX_BAR_H 16

static bool widget_fox_is_dismiss(const char* label) {
    if(!label) return false;
    static const char* const dismiss[] = {
        "Back",  "Cancel", "Stay", "Skip",  "Discard", "No",
        "Close", "Abort",  "Quit", "Exit",  "Dismiss"};
    for(size_t i = 0; i < sizeof(dismiss) / sizeof(dismiss[0]); i++) {
        if(strcmp(label, dismiss[i]) == 0) return true;
    }
    return false;
}

static uint8_t widget_fox_default_focus(WidgetElement* const order[], uint8_t n) {
    for(uint8_t i = 0; i < n; i++) {
        const char* t = NULL;
        widget_element_button_get(order[i], NULL, &t);
        if(!widget_fox_is_dismiss(t)) return i;
    }
    return 0;
}

static void widget_fox_draw_buttons(
    Canvas* canvas,
    WidgetElement* btn_left,
    WidgetElement* btn_center,
    WidgetElement* btn_right,
    uint8_t focus) {
    WidgetElement* order[3];
    const char* labels[3];
    uint8_t n = 0;
    if(btn_left) order[n++] = btn_left;
    if(btn_center) order[n++] = btn_center;
    if(btn_right) order[n++] = btn_right;
    if(n == 0) return;
    for(uint8_t i = 0; i < n; i++) {
        const char* t = NULL;
        widget_element_button_get(order[i], NULL, &t);
        labels[i] = t ? t : "";
    }
    if(focus >= n) focus = n - 1;

    const int32_t h = WIDGET_FOX_BAR_H;
    const int32_t bar_y = 64 - h;
    canvas_set_font(canvas, FontSecondary);

    if(n == 1) {
        const Icon* icon = &I_ButtonCenter_7x7;
        int32_t icon_w = icon_get_width(icon);
        int32_t icon_h = icon_get_height(icon);
        int32_t icon_gap = 3;
        int32_t pad_x = 10;
        int32_t content_w = icon_w + icon_gap + (int32_t)canvas_string_width(canvas, labels[0]);
        int32_t btn_w = content_w + pad_x * 2;
        int32_t x = (128 - btn_w) / 2;
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rbox(canvas, x, bar_y, btn_w, h, 3);
        canvas_set_color(canvas, ColorWhite);
        int32_t gx = x + (btn_w - content_w) / 2;
        int32_t gy_icon = bar_y + (h - icon_h) / 2;
        canvas_draw_icon(canvas, gx, gy_icon, icon);
        canvas_draw_str_aligned(
            canvas, gx + icon_w + icon_gap, bar_y + h / 2, AlignLeft, AlignCenter, labels[0]);
        canvas_set_color(canvas, ColorBlack);
        return;
    }

    int32_t gap = (n == 2) ? 4 : 3;
    int32_t btn_w = (128 - gap * (n + 1)) / n;
    for(uint8_t i = 0; i < n; i++) {
        int32_t x = gap + i * (btn_w + gap);
        canvas_set_color(canvas, ColorBlack);
        if(i == focus) {
            canvas_draw_rbox(canvas, x, bar_y, btn_w, h, 3);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, x, bar_y, btn_w, h, 3);
        }
        canvas_draw_str_aligned(
            canvas, x + btn_w / 2, bar_y + h / 2, AlignCenter, AlignCenter, labels[i]);
        canvas_set_color(canvas, ColorBlack);
    }
}

static void gui_widget_view_draw_callback(Canvas* canvas, void* _model) {
    GuiWidgetModel* model = _model;
    canvas_clear(canvas);

    WidgetElement* btn_left = NULL;
    WidgetElement* btn_center = NULL;
    WidgetElement* btn_right = NULL;

    ElementArray_it_t it;
    ElementArray_it(it, model->element);
    while(!ElementArray_end_p(it)) {
        WidgetElement* element = *ElementArray_ref(it);
        if(widget_element_is_button(element)) {
            GuiButtonType t;
            widget_element_button_get(element, &t, NULL);
            if(t == GuiButtonTypeLeft) {
                btn_left = element;
            } else if(t == GuiButtonTypeCenter) {
                btn_center = element;
            } else if(t == GuiButtonTypeRight) {
                btn_right = element;
            }
        } else if(element->draw != NULL) {
            element->draw(canvas, element);
        }
        ElementArray_next(it);
    }

    WidgetElement* order[3];
    uint8_t n = 0;
    if(btn_left) order[n++] = btn_left;
    if(btn_center) order[n++] = btn_center;
    if(btn_right) order[n++] = btn_right;
    if(n > 0 && !model->fox_focus_set) {
        model->fox_focus = widget_fox_default_focus(order, n);
        model->fox_focus_set = true;
    }
    widget_fox_draw_buttons(canvas, btn_left, btn_center, btn_right, model->fox_focus);
}

static bool gui_widget_view_input_callback(InputEvent* event, void* context) {
    Widget* widget = context;
    bool consumed = false;

    with_view_model(
        widget->view,
        GuiWidgetModel * model,
        {
            WidgetElement* order[3] = {0};
            uint8_t n = 0;
            WidgetElement* bl = NULL;
            WidgetElement* bc = NULL;
            WidgetElement* br = NULL;
            ElementArray_it_t bit;
            ElementArray_it(bit, model->element);
            while(!ElementArray_end_p(bit)) {
                WidgetElement* e = *ElementArray_ref(bit);
                if(widget_element_is_button(e)) {
                    GuiButtonType t;
                    widget_element_button_get(e, &t, NULL);
                    if(t == GuiButtonTypeLeft) {
                        bl = e;
                    } else if(t == GuiButtonTypeCenter) {
                        bc = e;
                    } else if(t == GuiButtonTypeRight) {
                        br = e;
                    }
                }
                ElementArray_next(bit);
            }
            if(bl) order[n++] = bl;
            if(bc) order[n++] = bc;
            if(br) order[n++] = br;

            if(n > 0 && !model->fox_focus_set) {
                model->fox_focus = widget_fox_default_focus(order, n);
                model->fox_focus_set = true;
            }

            if(n > 0 && event->type == InputTypeShort &&
               (event->key == InputKeyLeft || event->key == InputKeyRight ||
                event->key == InputKeyOk)) {
                if(model->fox_focus >= n) model->fox_focus = n - 1;
                if(event->key == InputKeyLeft) {
                    if(model->fox_focus > 0) model->fox_focus--;
                } else if(event->key == InputKeyRight) {
                    if(model->fox_focus < n - 1) model->fox_focus++;
                } else {
                    widget_element_button_invoke(order[model->fox_focus], event->type);
                }
                consumed = true;
            }

            if(!consumed) {
                ElementArray_it_t it;
                ElementArray_it(it, model->element);
                while(!ElementArray_end_p(it)) {
                    WidgetElement* element = *ElementArray_ref(it);
                    if(element->input != NULL && !widget_element_is_button(element)) {
                        consumed |= element->input(event, element);
                    }
                    ElementArray_next(it);
                }
            }
        },
        true);

    return consumed;
}

Widget* widget_alloc(void) {
    Widget* widget = malloc(sizeof(Widget));
    widget->view = view_alloc();
    view_set_context(widget->view, widget);
    view_allocate_model(widget->view, ViewModelTypeLocking, sizeof(GuiWidgetModel));
    view_set_draw_callback(widget->view, gui_widget_view_draw_callback);
    view_set_input_callback(widget->view, gui_widget_view_input_callback);

    with_view_model(
        widget->view,
        GuiWidgetModel * model,
        {
            ElementArray_init(model->element);
            model->fox_focus = 0;
            model->fox_focus_set = false;
        },
        true);

    return widget;
}

void widget_reset(Widget* widget) {
    furi_check(widget);

    with_view_model(
        widget->view,
        GuiWidgetModel * model,
        {
            ElementArray_it_t it;
            ElementArray_it(it, model->element);
            while(!ElementArray_end_p(it)) {
                WidgetElement* element = *ElementArray_ref(it);
                furi_assert(element->free);
                element->free(element);
                ElementArray_next(it);
            }
            ElementArray_reset(model->element);
            model->fox_focus = 0;
            model->fox_focus_set = false;
        },
        true);
}

void widget_free(Widget* widget) {
    furi_check(widget);

    widget_reset(widget);

    with_view_model(
        widget->view, GuiWidgetModel * model, { ElementArray_clear(model->element); }, true);

    view_free(widget->view);
    free(widget);
}

View* widget_get_view(Widget* widget) {
    furi_check(widget);
    return widget->view;
}


static void widget_add_element(Widget* widget, WidgetElement* element) {
    furi_assert(widget);
    furi_assert(element);

    with_view_model(
        widget->view,
        GuiWidgetModel * model,
        {
            element->parent = widget;
            ElementArray_push_back(model->element, element);
        },
        true);
}

void widget_add_string_multiline_element(
    Widget* widget,
    uint8_t x,
    uint8_t y,
    Align horizontal,
    Align vertical,
    Font font,
    const char* text) {
    furi_check(widget);
    WidgetElement* string_multiline_element =
        widget_element_string_multiline_create(x, y, horizontal, vertical, font, text);
    widget_add_element(widget, string_multiline_element);
}

void widget_add_string_element(
    Widget* widget,
    uint8_t x,
    uint8_t y,
    Align horizontal,
    Align vertical,
    Font font,
    const char* text) {
    furi_check(widget);
    WidgetElement* string_element =
        widget_element_string_create(x, y, horizontal, vertical, font, text);
    widget_add_element(widget, string_element);
}

void widget_add_text_box_element(
    Widget* widget,
    uint8_t x,
    uint8_t y,
    uint8_t width,
    uint8_t height,
    Align horizontal,
    Align vertical,
    const char* text,
    bool strip_to_dots) {
    furi_check(widget);
    WidgetElement* text_box_element = widget_element_text_box_create(
        x, y, width, height, horizontal, vertical, text, strip_to_dots);
    widget_add_element(widget, text_box_element);
}

void widget_add_text_scroll_element(
    Widget* widget,
    uint8_t x,
    uint8_t y,
    uint8_t width,
    uint8_t height,
    const char* text) {
    furi_check(widget);
    WidgetElement* text_scroll_element =
        widget_element_text_scroll_create(x, y, width, height, text);
    widget_add_element(widget, text_scroll_element);
}

void widget_add_button_element(
    Widget* widget,
    GuiButtonType button_type,
    const char* text,
    ButtonCallback callback,
    void* context) {
    furi_check(widget);
    WidgetElement* button_element =
        widget_element_button_create(button_type, text, callback, context);
    widget_add_element(widget, button_element);
}

void widget_add_icon_element(Widget* widget, uint8_t x, uint8_t y, const Icon* icon) {
    furi_check(widget);
    furi_check(icon);
    WidgetElement* icon_element = widget_element_icon_create(x, y, icon);
    widget_add_element(widget, icon_element);
}

void widget_add_rect_element(
    Widget* widget,
    uint8_t x,
    uint8_t y,
    uint8_t width,
    uint8_t height,
    uint8_t radius,
    bool fill) {
    furi_check(widget);
    WidgetElement* rect_element = widget_element_rect_create(x, y, width, height, radius, fill);
    widget_add_element(widget, rect_element);
}

void widget_add_circle_element(Widget* widget, uint8_t x, uint8_t y, uint8_t radius, bool fill) {
    furi_check(widget);
    WidgetElement* circle_element = widget_element_circle_create(x, y, radius, fill);
    widget_add_element(widget, circle_element);
}

void widget_add_line_element(Widget* widget, uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2) {
    furi_check(widget);
    WidgetElement* line_element = widget_element_line_create(x1, y1, x2, y2);
    widget_add_element(widget, line_element);
}
