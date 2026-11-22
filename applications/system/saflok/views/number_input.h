#ifndef NUMBER_INPUT_H
#define NUMBER_INPUT_H

#include <gui/view.h>
#include "../icons/assets_icons.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NumberInput {
    View* view;
} NumberInput;

typedef struct NumberInputKey{
    const char text;
    const size_t x;
    const size_t y;
} NumberInputKey;

typedef void (*NumberInputCallback)(void* context, int32_t number);

typedef struct NumberInputModel {
    FuriString* header;
    FuriString* text_buffer;

    int32_t current_number;
    int32_t max_value;
    int32_t min_value;
    int32_t value;
    int32_t digit_count;
    int32_t cursor;

    NumberInputCallback callback;
    void* callback_context;

    size_t selected_row;
    size_t selected_column;
} NumberInputModel;

NumberInput* number_input_alloc(void);

void number_input_reset(NumberInput* number_input);

void number_input_free(NumberInput* number_input);

View* number_input_get_view(NumberInput* number_input);

void number_input_set_result_callback(
    NumberInput* number_input,
    NumberInputCallback input_callback,
    void* callback_context,
    int32_t current_number,
    int32_t min_value,
    int32_t max_value);

void number_input_set_header_text(NumberInput* number_input, const char* text);

#ifdef __cplusplus
}
#endif

#endif
