#ifndef HEX_INPUT_H
#define HEX_INPUT_H

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HexInput {
    View* view;
} HexInput;

typedef struct HexInputKey {
    const char text;
    const size_t x;
    const size_t y;
} HexInputKey;

typedef void (*HexInputCallback)(void* context, int32_t number);

HexInput* hex_input_alloc(void);

void hex_input_reset(HexInput* hex_input);

void hex_input_free(HexInput* hex_input);

View* hex_input_get_view(HexInput* hex_input);

void hex_input_set_result_callback(
    HexInput* hex_input,
    HexInputCallback input_callback,
    void* callback_context,
    int32_t current_number,
    int32_t min_value,
    int32_t max_value);

void hex_input_set_header_text(HexInput* hex_input, const char* text);

typedef struct HexInputModel {
    FuriString* header;
    FuriString* text_buffer;

    int32_t current_number;
    int32_t max_value;
    int32_t min_value;
    int32_t value;
    int32_t digit_count;
    int32_t cursor;

    HexInputCallback callback;
    void* callback_context;

    size_t selected_row;
    size_t selected_column;
} HexInputModel;

#ifdef __cplusplus
}
#endif

#endif
