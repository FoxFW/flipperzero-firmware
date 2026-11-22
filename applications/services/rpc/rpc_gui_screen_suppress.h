#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void rpc_gui_screen_stream_set_suppressed(bool suppressed);

void rpc_gui_screen_stream_set_placeholder_label(const char* label);

bool rpc_gui_screen_stream_is_suppressed(void);

bool rpc_gui_screen_stream_is_active(void);

void rpc_gui_screen_stream_mark_active(bool active);

bool rpc_gui_screen_stream_get_placeholder_frame(uint8_t* out_buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif
