#include "rpc_gui_screen_suppress.h"

#include <furi.h>
#include <string.h>
#include <u8g2.h>
#include <toolbox/compress.h>
#include <gui/icon.h>
#include <gui/canvas_i.h>
#include <assets_icons.h>

#define TAG "RpcGuiScreenSuppress"

#define PLACEHOLDER_BUFFER_WIDTH  128u
#define PLACEHOLDER_BUFFER_HEIGHT 64u
#define PLACEHOLDER_BUFFER_SIZE   (PLACEHOLDER_BUFFER_WIDTH * PLACEHOLDER_BUFFER_HEIGHT / 8u)
#define PLACEHOLDER_TILE_HEIGHT   (PLACEHOLDER_BUFFER_HEIGHT / 8u)

extern uint8_t u8x8_d_st756x_flipper(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr);

static bool s_suppressed = false;
static uint16_t s_active_stream_count = 0;

static bool s_placeholder_ready = false;
static uint8_t s_placeholder_buf[PLACEHOLDER_BUFFER_SIZE];

/* First line of the "Operation in Progress!" placeholder frame shown to a
 * connected PC (qFlipper) while an app soft-locks the CLI/USB. Defaults to
 * "Sub-GHz" (the Sub-GHz garage app, the original caller). Other apps that
 * suppress the stream set this to name themselves, then clear it back to NULL
 * on unlock. Changing the label invalidates the cached frame so it is rebuilt
 * with the new text on the next request. */
#define PLACEHOLDER_DEFAULT_FIRST_LINE "Sub-GHz"
static const char* s_placeholder_first_line = PLACEHOLDER_DEFAULT_FIRST_LINE;

void rpc_gui_screen_stream_set_suppressed(bool suppressed) {
    s_suppressed = suppressed;
}

void rpc_gui_screen_stream_set_placeholder_label(const char* label) {
    const char* new_label =
        (label != NULL && label[0] != '\0') ? label : PLACEHOLDER_DEFAULT_FIRST_LINE;
    if(new_label != s_placeholder_first_line) {
        s_placeholder_first_line = new_label;
        s_placeholder_ready = false; /* force rebuild with the new first line */
    }
}

bool rpc_gui_screen_stream_is_suppressed(void) {
    return s_suppressed;
}

void rpc_gui_screen_stream_mark_active(bool active) {
    if(active) {
        s_active_stream_count++;
    } else if(s_active_stream_count > 0) {
        s_active_stream_count--;
    }
}

bool rpc_gui_screen_stream_is_active(void) {
    return s_active_stream_count > 0;
}

static bool build_placeholder_frame(void) {
    memset(s_placeholder_buf, 0, sizeof(s_placeholder_buf));

    u8g2_t u8g2;
    memset(&u8g2, 0, sizeof(u8g2));
    u8g2_SetupDisplay(&u8g2, u8x8_d_st756x_flipper, u8x8_cad_001, u8x8_dummy_cb, u8x8_dummy_cb);
    u8g2_SetupBuffer(
        &u8g2,
        s_placeholder_buf,
        (uint8_t)PLACEHOLDER_TILE_HEIGHT,
        u8g2_ll_hvline_vertical_top_lsb,
        U8G2_R0);

    CompressIcon* compress_icon = compress_icon_alloc(ICON_DECOMPRESSOR_BUFFER_SIZE);
    if(compress_icon) {
        uint8_t* icon_data = NULL;
        compress_icon_decode(compress_icon, icon_get_frame_data(&I_fox_32x32, 0), &icon_data);
        if(icon_data) {
            canvas_draw_u8g2_bitmap(
                &u8g2,
                4,
                16,
                icon_get_width(&I_fox_32x32),
                icon_get_height(&I_fox_32x32),
                icon_data,
                IconRotation0);
        }
        compress_icon_free(compress_icon);
    } else {
        FURI_LOG_W(TAG, "build_placeholder_frame: icon decompressor alloc failed, text only");
    }

    u8g2_SetFontDirection(&u8g2, 0);
    u8g2_SetFontMode(&u8g2, 1);
    u8g2_SetFont(&u8g2, u8g2_font_helvB08_tr);

    const char* lines[] = {s_placeholder_first_line, "Operation", "in", "Progress!"};
    const size_t line_count = COUNT_OF(lines);
    int32_t text_area_center_x = (40 + PLACEHOLDER_BUFFER_WIDTH) / 2;
    int32_t line_gap = 16;
    int32_t center_y = PLACEHOLDER_BUFFER_HEIGHT / 2;
    int32_t first_y = center_y - (((int32_t)line_count - 1) * line_gap) / 2 +
                       (u8g2_GetAscent(&u8g2) / 2);

    for(size_t i = 0; i < line_count; i++) {
        int32_t y = first_y + (int32_t)i * line_gap;
        u8g2_DrawStr(
            &u8g2, text_area_center_x - (u8g2_GetStrWidth(&u8g2, lines[i]) / 2), y, lines[i]);
    }

    return true;
}

bool rpc_gui_screen_stream_get_placeholder_frame(uint8_t* out_buffer, size_t buffer_size) {
    furi_check(out_buffer);
    if(buffer_size != PLACEHOLDER_BUFFER_SIZE) {
        FURI_LOG_E(
            TAG,
            "get_placeholder_frame: buffer_size %zu != expected %u",
            buffer_size,
            (unsigned)PLACEHOLDER_BUFFER_SIZE);
        return false;
    }

    if(!s_placeholder_ready) {
        s_placeholder_ready = build_placeholder_frame();
        if(!s_placeholder_ready) return false;
    }

    memcpy(out_buffer, s_placeholder_buf, buffer_size);
    return true;
}
