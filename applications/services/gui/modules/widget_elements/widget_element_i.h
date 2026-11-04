#pragma once

#include "../widget.h"
#include "widget_element.h"
#include <furi.h>
#include <gui/view.h>
#include <input/input.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WidgetElement WidgetElement;
typedef struct Widget Widget;

struct WidgetElement {

    void (*draw)(Canvas* canvas, WidgetElement* element);
    bool (*input)(InputEvent* event, WidgetElement* element);

    void (*free)(WidgetElement* element);

    void* model;
    FuriMutex* model_mutex;

    Widget* parent;
};

WidgetElement* widget_element_string_multiline_create(
    uint8_t x,
    uint8_t y,
    Align horizontal,
    Align vertical,
    Font font,
    const char* text);

WidgetElement* widget_element_string_create(
    uint8_t x,
    uint8_t y,
    Align horizontal,
    Align vertical,
    Font font,
    const char* text);

WidgetElement* widget_element_text_box_create(
    uint8_t x,
    uint8_t y,
    uint8_t width,
    uint8_t height,
    Align horizontal,
    Align vertical,
    const char* text,
    bool strip_to_dots);

WidgetElement* widget_element_button_create(
    GuiButtonType button_type,
    const char* text,
    ButtonCallback callback,
    void* context);

bool widget_element_is_button(WidgetElement* element);

void widget_element_button_get(WidgetElement* element, GuiButtonType* type, const char** text);

void widget_element_button_invoke(WidgetElement* element, InputType input_type);

WidgetElement* widget_element_icon_create(uint8_t x, uint8_t y, const Icon* icon);

WidgetElement* widget_element_rect_create(
    uint8_t x,
    uint8_t y,
    uint8_t width,
    uint8_t height,
    uint8_t radius,
    bool fill);

WidgetElement* widget_element_text_scroll_create(
    uint8_t x,
    uint8_t y,
    uint8_t width,
    uint8_t height,
    const char* text);

WidgetElement* widget_element_circle_create(uint8_t x, uint8_t y, uint8_t radius, bool fill);

WidgetElement* widget_element_line_create(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2);

#ifdef __cplusplus
}
#endif
