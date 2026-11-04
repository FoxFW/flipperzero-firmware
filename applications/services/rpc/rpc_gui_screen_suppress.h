#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void rpc_gui_screen_stream_set_suppressed(bool suppressed);

/* Set the first line of the "Operation in Progress!" placeholder frame shown
 * to a connected PC while the CLI/USB is soft-locked. Pass the app name (e.g.
 * "NFC"); pass NULL or "" to restore the default ("Sub-GHz"). Safe to call
 * from any app that suppresses the screen stream. */
void rpc_gui_screen_stream_set_placeholder_label(const char* label);

bool rpc_gui_screen_stream_is_suppressed(void);

bool rpc_gui_screen_stream_is_active(void);

void rpc_gui_screen_stream_mark_active(bool active);

bool rpc_gui_screen_stream_get_placeholder_frame(uint8_t* out_buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif
