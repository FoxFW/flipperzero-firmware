#include "gui_i.h"
#include "elements.h"
#include <assets_icons.h>
#include <string.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>
#ifndef FURI_RAM_EXEC

#include <loader/loader.h>
#endif

#define TAG "GuiSrv"

#define GUI_SCREENSHOT_DIR         EXT_PATH("Screenshots")
#define GUI_SCREENSHOT_HOLD_MS     1000
#define GUI_SCREENSHOT_OVERLAY_MS  1000
#define GUI_SCREENSHOT_BYTES       ((GUI_DISPLAY_WIDTH / 8) * GUI_DISPLAY_HEIGHT)

ViewPort* gui_view_port_find_enabled(ViewPortArray_t array) {
    ViewPortArray_it_t it;
    ViewPortArray_it_last(it, array);
    while(!ViewPortArray_end_p(it)) {
        ViewPort* view_port = *ViewPortArray_ref(it);
        if(view_port_is_enabled(view_port)) {
            return view_port;
        }
        ViewPortArray_previous(it);
    }
    return NULL;
}

size_t gui_active_view_port_count(Gui* gui, GuiLayer layer) {
    furi_assert(gui);
    furi_check(layer < GuiLayerMAX);
    size_t ret = 0;

    gui_lock(gui);
    ViewPortArray_it_t it;
    ViewPortArray_it_last(it, gui->layers[layer]);
    while(!ViewPortArray_end_p(it)) {
        ViewPort* view_port = *ViewPortArray_ref(it);
        if(view_port_is_enabled(view_port)) {
            ret++;
        }
        ViewPortArray_previous(it);
    }
    gui_unlock(gui);

    return ret;
}

void gui_update(Gui* gui) {
    furi_assert(gui);
    if(!gui->direct_draw) furi_thread_flags_set(gui->thread_id, GUI_THREAD_FLAG_DRAW);
}

void gui_input_events_callback(const void* value, void* ctx) {
    furi_assert(value);
    furi_assert(ctx);

    Gui* gui = ctx;

    furi_message_queue_put(gui->input_queue, value, FuriWaitForever);
    furi_thread_flags_set(gui->thread_id, GUI_THREAD_FLAG_INPUT);
}

static void gui_screenshot_draw_overlay(Gui* gui);

static bool gui_redraw_fs(Gui* gui) {
    canvas_set_orientation(gui->canvas, CanvasOrientationHorizontal);
    canvas_frame_set(gui->canvas, 0, 0, GUI_DISPLAY_WIDTH, GUI_DISPLAY_HEIGHT);
    ViewPort* view_port = gui_view_port_find_enabled(gui->layers[GuiLayerFullscreen]);
    if(view_port) {
        view_port_draw(view_port, gui->canvas);
        return true;
    } else {
        return false;
    }
}

static void gui_draw_desktop_notification(Gui* gui) {
    if(!gui->desktop_notify_active) return;
    int reveal = gui->desktop_notify_reveal;
    if(reveal <= 0) return;
    if(reveal > GUI_STATUS_BAR_HEIGHT - 1) reveal = GUI_STATUS_BAR_HEIGHT - 1; /* <= 12 */

    Canvas* canvas = gui->canvas;
    canvas_frame_set(canvas, 0, 0, GUI_DISPLAY_WIDTH, GUI_DISPLAY_HEIGHT);

    /* Erase the covered status-bar strip (rows 1..reveal) to background. The
     * double border is redrawn after us, so it always stays on top and the
     * banner appears to slide out from underneath it. */
    canvas_set_color(canvas, ColorBlack);
    {
        /* Soft bottom-left/right corners; top stays square (flush under the border). */
        const int banner_rad = 3;
        if((int)reveal >= 2 * banner_rad) {
            canvas_draw_rbox(canvas, 3, 1, GUI_DISPLAY_WIDTH - 6, (size_t)reveal, banner_rad);
            canvas_draw_box(canvas, 3, 1, GUI_DISPLAY_WIDTH - 6, (size_t)banner_rad);
        } else {
            canvas_draw_box(canvas, 3, 1, GUI_DISPLAY_WIDTH - 6, (size_t)reveal);
        }
    }

    /* Text (+ optional info glyph). Baseline rides the revealed edge so the text
     * slides down with the banner and is masked by the border while still high. */
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontPrimary);
    const char* text = gui->desktop_notify_text;
    int tw = (int)canvas_string_width(canvas, text);
    int icon_w = 0;
    if(gui->desktop_notify_icon == GuiDesktopNotifyIconInfo)
        icon_w = 10; /* 7px glyph + 3px gap */
    else if(gui->desktop_notify_icon == GuiDesktopNotifyIconWarning)
        icon_w = 12; /* 9px glyph + 3px gap */
    int x = (GUI_DISPLAY_WIDTH - (icon_w + tw)) / 2;
    if(x < 4) x = 4;
    int baseline = reveal - 1;
    if(gui->desktop_notify_icon == GuiDesktopNotifyIconInfo) {
        /* (i) in a circle */
        int cx = x + 3;
        int cy = baseline - 4;
        canvas_draw_circle(canvas, cx, cy, 3);
        canvas_draw_dot(canvas, cx, cy - 1);
        canvas_draw_line(canvas, cx, cy + 1, cx, cy + 2);
        x += icon_w;
    } else if(gui->desktop_notify_icon == GuiDesktopNotifyIconWarning) {
        /* (!) in an upward triangle */
        int cx = x + 4;
        int b = baseline;
        canvas_draw_line(canvas, cx, b - 8, cx - 4, b);
        canvas_draw_line(canvas, cx, b - 8, cx + 4, b);
        canvas_draw_line(canvas, cx - 4, b, cx + 4, b);
        canvas_draw_line(canvas, cx, b - 5, cx, b - 3); /* stem */
        canvas_draw_dot(canvas, cx, b - 1);             /* dot */
        x += icon_w;
    }
    canvas_draw_str(canvas, x, baseline, text);
}

void gui_desktop_notification_set(
    Gui* gui,
    const char* text,
    int8_t reveal,
    GuiDesktopNotifyIcon icon) {
    furi_check(gui);
    gui_lock(gui);
    gui->desktop_notify_active = true;
    gui->desktop_notify_reveal = reveal;
    gui->desktop_notify_icon = icon;
    if(text) {
        strncpy(gui->desktop_notify_text, text, sizeof(gui->desktop_notify_text) - 1);
        gui->desktop_notify_text[sizeof(gui->desktop_notify_text) - 1] = '\0';
    }
    gui_unlock(gui);
    gui_update(gui);
}

void gui_desktop_notification_clear(Gui* gui) {
    furi_check(gui);
    gui_lock(gui);
    gui->desktop_notify_active = false;
    gui->desktop_notify_reveal = 0;
    gui_unlock(gui);
    gui_update(gui);
}

static void gui_redraw_status_bar(Gui* gui, bool need_attention) {
    ViewPortArray_it_t it;
    uint8_t left_used = 0;
    uint8_t right_used = 0;
    uint8_t width;

    if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagHandOrient)) {
        canvas_set_orientation(gui->canvas, CanvasOrientationHorizontalFlip);
    } else {
        canvas_set_orientation(gui->canvas, CanvasOrientationHorizontal);
    }

    canvas_frame_set(
        gui->canvas, GUI_STATUS_BAR_X, GUI_STATUS_BAR_Y, GUI_DISPLAY_WIDTH, GUI_STATUS_BAR_HEIGHT);

    canvas_set_color(gui->canvas, ColorWhite);
    canvas_draw_box(gui->canvas, 1, 1, 9, 7);
    canvas_draw_box(gui->canvas, 7, 3, 58, 6);
    canvas_draw_box(gui->canvas, 61, 1, 32, 7);
    canvas_draw_box(gui->canvas, 89, 3, 38, 6);
    canvas_set_color(gui->canvas, ColorBlack);
    canvas_set_bitmap_mode(gui->canvas, 1);
    canvas_draw_icon(gui->canvas, 0, 0, &I_Background_128x11);
    canvas_set_bitmap_mode(gui->canvas, 0);

    if(!gui->statusbar_show_icons) return;

    uint8_t x = GUI_DISPLAY_WIDTH - 2;
    ViewPortArray_it(it, gui->layers[GuiLayerStatusBarRight]);
    while(!ViewPortArray_end_p(it) && right_used < GUI_STATUS_BAR_WIDTH) {
        ViewPort* view_port = *ViewPortArray_ref(it);
        if(view_port_is_enabled(view_port)) {
            width = view_port_get_width(view_port);
            if(!width) width = 8;
            right_used += (width + 2);
            x -= (width + 2);
            canvas_frame_set(
                gui->canvas,
                x - 1,
                GUI_STATUS_BAR_Y + 1,
                width + 2,
                GUI_STATUS_BAR_WORKAREA_HEIGHT + 3);
            canvas_set_color(gui->canvas, ColorWhite);
            canvas_draw_box(
                gui->canvas, 0, 0, canvas_width(gui->canvas), canvas_height(gui->canvas));
            canvas_set_color(gui->canvas, ColorBlack);
            canvas_frame_set(
                gui->canvas, x, GUI_STATUS_BAR_Y + 4, width, GUI_STATUS_BAR_WORKAREA_HEIGHT);
            view_port_draw(view_port, gui->canvas);
        }
        ViewPortArray_next(it);
    }

    x = 4;
    ViewPortArray_it(it, gui->layers[GuiLayerStatusBarLeft]);
    while(!ViewPortArray_end_p(it) && (right_used + left_used) < GUI_STATUS_BAR_WIDTH) {
        ViewPort* view_port = *ViewPortArray_ref(it);
        if(view_port_is_enabled(view_port)) {
            width = view_port_get_width(view_port);
            if(!width) width = 8;
            canvas_frame_set(
                gui->canvas,
                x - 1,
                GUI_STATUS_BAR_Y + 1,
                width + 2,
                GUI_STATUS_BAR_WORKAREA_HEIGHT + 3);
            canvas_set_color(gui->canvas, ColorWhite);
            canvas_draw_box(
                gui->canvas, 0, 0, canvas_width(gui->canvas), canvas_height(gui->canvas));
            canvas_set_color(gui->canvas, ColorBlack);
            canvas_frame_set(
                gui->canvas, x, GUI_STATUS_BAR_Y + 4, width, GUI_STATUS_BAR_WORKAREA_HEIGHT);
            view_port_draw(view_port, gui->canvas);
            left_used += (width + 2);
            x += (width + 2);
        }
        ViewPortArray_next(it);
    }

    if(need_attention) {
        width = icon_get_width(&I_Hidden_window_9x8);
        canvas_frame_set(
            gui->canvas,
            x - 1,
            GUI_STATUS_BAR_Y + 1,
            width + 2,
            GUI_STATUS_BAR_WORKAREA_HEIGHT + 3);
        canvas_set_color(gui->canvas, ColorWhite);
        canvas_draw_box(gui->canvas, 0, 0, canvas_width(gui->canvas), canvas_height(gui->canvas));
        canvas_set_color(gui->canvas, ColorBlack);
        canvas_frame_set(
            gui->canvas, x, GUI_STATUS_BAR_Y + 4, width, GUI_STATUS_BAR_WORKAREA_HEIGHT);
        canvas_draw_icon(gui->canvas, 0, 0, &I_Hidden_window_9x8);
    }

    {
        ViewPort* center_vp =
            gui_view_port_find_enabled(gui->layers[GuiLayerStatusBarCenter]);
        if(center_vp) {
            width = view_port_get_width(center_vp);
            if(!width) width = 25;
            int32_t cx = ((int32_t)GUI_DISPLAY_WIDTH - (int32_t)width) / 2;

            canvas_frame_set(
                gui->canvas,
                cx - 1,
                GUI_STATUS_BAR_Y + 1,
                width + 2,
                GUI_STATUS_BAR_WORKAREA_HEIGHT + 3);
            canvas_set_color(gui->canvas, ColorWhite);
            canvas_draw_box(
                gui->canvas, 0, 0, canvas_width(gui->canvas), canvas_height(gui->canvas));
            canvas_set_color(gui->canvas, ColorBlack);

            canvas_frame_set(
                gui->canvas, cx, GUI_STATUS_BAR_Y + 4, width, GUI_STATUS_BAR_WORKAREA_HEIGHT);
            view_port_draw(center_vp, gui->canvas);
        }
    }
}

static bool gui_redraw_window(Gui* gui) {
    canvas_set_orientation(gui->canvas, CanvasOrientationHorizontal);
    canvas_frame_set(gui->canvas, GUI_WINDOW_X, GUI_WINDOW_Y, GUI_WINDOW_WIDTH, GUI_WINDOW_HEIGHT);
    ViewPort* view_port = gui_view_port_find_enabled(gui->layers[GuiLayerWindow]);
    if(view_port) {
        view_port_draw(view_port, gui->canvas);
        return true;
    }
    return false;
}

static bool gui_redraw_desktop(Gui* gui) {
    canvas_set_orientation(gui->canvas, CanvasOrientationHorizontal);
    canvas_frame_set(gui->canvas, 0, 0, GUI_DISPLAY_WIDTH, GUI_DISPLAY_HEIGHT);
    ViewPort* view_port = gui_view_port_find_enabled(gui->layers[GuiLayerDesktop]);
    if(view_port) {
        view_port_draw(view_port, gui->canvas);
        return true;
    }
    return false;
}

static void gui_redraw(Gui* gui) {
    furi_assert(gui);
    gui_lock(gui);

    do {
        if(gui->direct_draw) break;

        canvas_reset(gui->canvas);

        if(gui->screenshot_overlay_active) {
            gui_screenshot_draw_overlay(gui);
        } else if(gui_is_lockdown(gui)) {
            gui_redraw_desktop(gui);

            if(!gui->hide_status_bar) {
                bool need_attention =
                    (gui_view_port_find_enabled(gui->layers[GuiLayerWindow]) != 0 ||
                     gui_view_port_find_enabled(gui->layers[GuiLayerFullscreen]) != 0);
                gui_redraw_status_bar(gui, need_attention);
            }
            canvas_frame_set(gui->canvas, 0, 0, GUI_DISPLAY_WIDTH, GUI_DISPLAY_HEIGHT);
            canvas_set_color(gui->canvas, ColorBlack);
            canvas_draw_frame(gui->canvas, 0, 0, GUI_DISPLAY_WIDTH, GUI_DISPLAY_HEIGHT);
            canvas_draw_frame(gui->canvas, 2, 2, GUI_DISPLAY_WIDTH - 4, GUI_DISPLAY_HEIGHT - 4);
        } else {
            if(!gui_redraw_fs(gui)) {
                if(!gui_redraw_window(gui)) {
                    gui_redraw_desktop(gui);
                }

                if(!gui->hide_status_bar) {
                    gui_redraw_status_bar(gui, false);
                }
                gui_draw_desktop_notification(gui);
                canvas_frame_set(gui->canvas, 0, 0, GUI_DISPLAY_WIDTH, GUI_DISPLAY_HEIGHT);
                canvas_set_color(gui->canvas, ColorBlack);
                canvas_draw_frame(gui->canvas, 0, 0, GUI_DISPLAY_WIDTH, GUI_DISPLAY_HEIGHT);
                canvas_draw_frame(
                    gui->canvas, 2, 2, GUI_DISPLAY_WIDTH - 4, GUI_DISPLAY_HEIGHT - 4);
            }
        }

        canvas_commit(gui->canvas);
    } while(false);

    gui_unlock(gui);
}

static void gui_screenshot_to_xbm(const uint8_t* fb, uint8_t* xbm) {
    memset(xbm, 0, GUI_SCREENSHOT_BYTES);
    const size_t tile_width = GUI_DISPLAY_WIDTH / 8;
    for(size_t y = 0; y < GUI_DISPLAY_HEIGHT; y++) {
        size_t fb_row = (y & ~(size_t)7) * tile_width;
        for(size_t x = 0; x < GUI_DISPLAY_WIDTH; x++) {
            if(fb[fb_row + x] & (1 << (y & 7))) {
                xbm[(y * tile_width) + (x / 8)] |= (1 << (x % 8));
            }
        }
    }
}

static void gui_screenshot_write_xbm_text(File* file, const uint8_t* raw, size_t len) {
    static const char header[] =
        "#define screenshot_width 128\n"
        "#define screenshot_height 64\n"
        "static unsigned char screenshot_bits[] = {\n";
    storage_file_write(file, header, strlen(header));

    char line[8];
    for(size_t i = 0; i < len; i++) {
        bool last = (i + 1 == len);
        bool eol = ((i + 1) % 12 == 0) || last;
        int n = snprintf(line, sizeof(line), last ? "0x%02x" : "0x%02x,", raw[i]);
        storage_file_write(file, line, n);
        storage_file_write(file, eol ? "\n" : " ", 1);
    }

    static const char footer[] = "};\n";
    storage_file_write(file, footer, strlen(footer));
}

static const NotificationSequence sequence_screenshot_saved = {
    &message_blue_255,
    &message_delay_100,
    &message_blue_0,
    &message_delay_100,
    &message_blue_255,
    &message_vibro_on,
    &message_delay_100,
    &message_blue_0,
    &message_vibro_off,
    NULL,
};

static void gui_screenshot_draw_camera_icon(Canvas* canvas, int32_t x, int32_t y) {
    canvas_draw_rframe(canvas, x, y + 3, 18, 11, 2);
    canvas_draw_box(canvas, x + 3, y, 6, 4);
    canvas_draw_circle(canvas, x + 9, y + 8, 3);
    canvas_draw_dot(canvas, x + 9, y + 8);
    canvas_draw_box(canvas, x + 14, y + 5, 2, 2);
}

static void
    gui_screenshot_draw_corner_borders(Canvas* canvas, int32_t x, int32_t y, int32_t w, int32_t h) {
    const int32_t len = 6, off = 3;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_line(canvas, x - off, y - off, x - off + len, y - off);
    canvas_draw_line(canvas, x - off, y - off, x - off, y - off + len);
    canvas_draw_line(canvas, x + w + off, y - off, x + w + off - len, y - off);
    canvas_draw_line(canvas, x + w + off, y - off, x + w + off, y - off + len);
    canvas_draw_line(canvas, x - off, y + h + off, x - off + len, y + h + off);
    canvas_draw_line(canvas, x - off, y + h + off, x - off, y + h + off - len);
    canvas_draw_line(canvas, x + w + off, y + h + off, x + w + off - len, y + h + off);
    canvas_draw_line(canvas, x + w + off, y + h + off, x + w + off, y + h + off - len);
    canvas_set_color(canvas, ColorBlack);
}

static void gui_screenshot_draw_overlay(Gui* gui) {
    Canvas* canvas = gui->canvas;
    canvas_set_orientation(canvas, CanvasOrientationHorizontal);
    canvas_frame_set(canvas, 0, 0, GUI_DISPLAY_WIDTH, GUI_DISPLAY_HEIGHT);

    const int32_t box_x = 10, box_y = 4, box_w = 108, box_h = 56;
    const int32_t cx = box_x + box_w / 2;

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, box_x, box_y, box_w, box_h);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, box_x, box_y, box_w, box_h);
    gui_screenshot_draw_corner_borders(canvas, box_x, box_y, box_w, box_h);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, cx, box_y + 5, AlignCenter, AlignTop, "Screenshot");

    gui_screenshot_draw_camera_icon(canvas, cx - 9, box_y + 21);

    canvas_draw_str_aligned(canvas, cx, box_y + 41, AlignCenter, AlignTop, "Saved");
}

static void gui_screenshot_overlay_timeout(void* context) {
    Gui* gui = context;
    gui_lock(gui);
    gui->screenshot_overlay_active = false;
    gui_unlock(gui);
    gui_update(gui);
}

static void gui_screenshot_make_name(char* name, size_t max_len) {
    FuriString* appid = furi_string_alloc_set("desktop");
#ifndef FURI_RAM_EXEC
    Loader* loader = furi_record_open(RECORD_LOADER);
    if(!loader_get_application_id(loader, appid) || furi_string_empty(appid)) {
        furi_string_set(appid, "desktop");
    }
    furi_record_close(RECORD_LOADER);
#endif

    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    snprintf(
        name,
        max_len,
        "Screenshot_%s_%04d%02d%02d_%02d%02d%02d",
        furi_string_get_cstr(appid),
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second);
    furi_string_free(appid);
}

static void gui_screenshot_capture(Gui* gui) {

    gui_lock(gui);
    gui->screenshot_overlay_active = true;
    gui_unlock(gui);

    uint8_t xbm[GUI_SCREENSHOT_BYTES];
    gui_screenshot_to_xbm(canvas_get_buffer(gui->canvas), xbm);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, GUI_SCREENSHOT_DIR);

    char name[64];
    gui_screenshot_make_name(name, sizeof(name));
    FuriString* path = furi_string_alloc_printf("%s/%s.xbm", GUI_SCREENSHOT_DIR, name);

    bool saved = false;
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        gui_screenshot_write_xbm_text(file, xbm, GUI_SCREENSHOT_BYTES);
        saved = true;
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_string_free(path);
    furi_record_close(RECORD_STORAGE);

    NotificationApp* notifications = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notifications, saved ? &sequence_screenshot_saved : &sequence_error);
    furi_record_close(RECORD_NOTIFICATION);

    if(saved) {

        furi_timer_start(
            gui->screenshot_overlay_timer, furi_ms_to_ticks(GUI_SCREENSHOT_OVERLAY_MS));
    } else {

        gui_lock(gui);
        gui->screenshot_overlay_active = false;
        gui_unlock(gui);
    }
    gui_update(gui);

    FURI_LOG_I(TAG, "Screenshot %s: %s", saved ? "saved" : "FAILED", name);
}

static bool gui_screenshot_check_combo(Gui* gui, const InputEvent* input_event) {
    static uint32_t combo_start = 0;
    static bool combo_fired = false;
    static bool combo_active = false;

    const uint8_t combo_mask = (1 << InputKeyBack) | (1 << InputKeyUp);
    bool both_held = (gui->ongoing_input & combo_mask) == combo_mask;
    bool either_held = (gui->ongoing_input & combo_mask) != 0;

    if(both_held) {
        combo_active = true;
        if(combo_start == 0) {
            combo_start = furi_get_tick();
        } else if(!combo_fired && (furi_get_tick() - combo_start) >= GUI_SCREENSHOT_HOLD_MS) {
            combo_fired = true;
            gui_screenshot_capture(gui);
        }
    } else if(!either_held) {
        combo_start = 0;
        combo_fired = false;
        combo_active = false;
    }

    return combo_active && (input_event->key == InputKeyBack || input_event->key == InputKeyUp);
}

static void gui_input(Gui* gui, InputEvent* input_event) {
    furi_assert(gui);
    furi_assert(input_event);

    uint8_t key_bit = (1 << input_event->key);
    if(input_event->type == InputTypeRelease) {
        gui->ongoing_input &= ~key_bit;
    } else if(input_event->type == InputTypePress) {
        gui->ongoing_input |= key_bit;
    } else if(!(gui->ongoing_input & key_bit)) {
        FURI_LOG_D(
            TAG,
            "non-complementary input, discarding key: %s type: %s, sequence: %p",
            input_get_key_name(input_event->key),
            input_get_type_name(input_event->type),
            (void*)input_event->sequence);
        return;
    }

    if(gui_screenshot_check_combo(gui, input_event)) {
        return;
    }

    if(gui->screenshot_overlay_active) {
        return;
    }

    gui_lock(gui);

    do {
        if(gui->direct_draw && !gui->ongoing_input_view_port) {
            break;
        }

        ViewPort* view_port = NULL;

        if(gui_is_lockdown(gui)) {
            view_port = gui_view_port_find_enabled(gui->layers[GuiLayerDesktop]);
        } else {
            view_port = gui_view_port_find_enabled(gui->layers[GuiLayerFullscreen]);
            if(!view_port) view_port = gui_view_port_find_enabled(gui->layers[GuiLayerWindow]);
            if(!view_port) view_port = gui_view_port_find_enabled(gui->layers[GuiLayerDesktop]);
        }

        if(!(gui->ongoing_input & ~key_bit) && input_event->type == InputTypePress) {
            gui->ongoing_input_view_port = view_port;
        }

        if(view_port && view_port == gui->ongoing_input_view_port) {
            view_port_input(view_port, input_event);
        } else if(gui->ongoing_input_view_port && input_event->type == InputTypeRelease) {
            FURI_LOG_D(
                TAG,
                "ViewPort changed while key press %p -> %p. Sending key: %s, type: %s, sequence: %p to previous view port",
                gui->ongoing_input_view_port,
                view_port,
                input_get_key_name(input_event->key),
                input_get_type_name(input_event->type),
                (void*)input_event->sequence);
            view_port_input(gui->ongoing_input_view_port, input_event);
        } else {
            FURI_LOG_D(
                TAG,
                "ViewPort changed while key press %p -> %p. Discarding key: %s, type: %s, sequence: %p",
                gui->ongoing_input_view_port,
                view_port,
                input_get_key_name(input_event->key),
                input_get_type_name(input_event->type),
                (void*)input_event->sequence);
        }
    } while(false);

    gui_unlock(gui);
}

void gui_lock(Gui* gui) {
    furi_assert(gui);
    furi_check(furi_mutex_acquire(gui->mutex, FuriWaitForever) == FuriStatusOk);
}

void gui_unlock(Gui* gui) {
    furi_assert(gui);
    furi_check(furi_mutex_release(gui->mutex) == FuriStatusOk);
}

void gui_add_view_port(Gui* gui, ViewPort* view_port, GuiLayer layer) {
    furi_check(gui);
    furi_check(view_port);
    furi_check(layer < GuiLayerMAX);

    ViewPortOrientation view_port_orientation = view_port_get_orientation(view_port);
    furi_check(
        (layer == GuiLayerFullscreen) ||
        ((view_port_orientation != ViewPortOrientationVertical) &&
         (view_port_orientation != ViewPortOrientationVerticalFlip)));

    gui_lock(gui);
    ViewPortArray_it_t it;
    for(size_t i = 0; i < GuiLayerMAX; i++) {
        ViewPortArray_it(it, gui->layers[i]);
        while(!ViewPortArray_end_p(it)) {
            furi_assert(*ViewPortArray_ref(it) != view_port);
            ViewPortArray_next(it);
        }
    }
    ViewPortArray_push_back(gui->layers[layer], view_port);
    view_port_gui_set(view_port, gui);
    gui_unlock(gui);

    gui_update(gui);
}

void gui_remove_view_port(Gui* gui, ViewPort* view_port) {
    furi_check(gui);
    furi_check(view_port);

    gui_lock(gui);
    view_port_gui_set(view_port, NULL);
    ViewPortArray_it_t it;
    for(size_t i = 0; i < GuiLayerMAX; i++) {
        ViewPortArray_it(it, gui->layers[i]);
        while(!ViewPortArray_end_p(it)) {
            if(*ViewPortArray_ref(it) == view_port) {
                ViewPortArray_remove(gui->layers[i], it);
            } else {
                ViewPortArray_next(it);
            }
        }
    }
    /* FoxFW: hand a layer's backing back to the heap once it becomes empty.
     * mlib arrays keep their capacity after _remove, so the first fullscreen
     * app of a boot would otherwise strand its layer-array backing high in the
     * heap for the rest of the session (the max free block never fully recovers
     * on app close). Freeing an emptied layer makes an app return ALL of its
     * heap; a later app simply reallocates on its next push. Populated layers
     * (desktop, status bar) are never empty, so they are untouched. */
    for(size_t gi = 0; gi < GuiLayerMAX; gi++) {
        if(ViewPortArray_empty_p(gui->layers[gi])) {
            ViewPortArray_clear(gui->layers[gi]);
            ViewPortArray_init(gui->layers[gi]);
        }
    }
    if(gui->ongoing_input_view_port == view_port) {
        gui->ongoing_input_view_port = NULL;
    }
    gui_unlock(gui);

    gui_update(gui);
}

void gui_view_port_send_to_front(Gui* gui, ViewPort* view_port) {
    furi_check(gui);
    furi_check(view_port);

    gui_lock(gui);
    GuiLayer layer = GuiLayerMAX;
    ViewPortArray_it_t it;
    for(size_t i = 0; i < GuiLayerMAX; i++) {
        ViewPortArray_it(it, gui->layers[i]);
        while(!ViewPortArray_end_p(it)) {
            if(*ViewPortArray_ref(it) == view_port) {
                ViewPortArray_remove(gui->layers[i], it);
                furi_check(layer == GuiLayerMAX);
                layer = i;
            } else {
                ViewPortArray_next(it);
            }
        }
    }
    furi_check(layer != GuiLayerMAX);
    ViewPortArray_push_back(gui->layers[layer], view_port);
    gui_unlock(gui);

    gui_update(gui);
}

void gui_view_port_send_to_back(Gui* gui, ViewPort* view_port) {
    furi_assert(gui);
    furi_assert(view_port);

    gui_lock(gui);
    GuiLayer layer = GuiLayerMAX;
    ViewPortArray_it_t it;
    for(size_t i = 0; i < GuiLayerMAX; i++) {
        ViewPortArray_it(it, gui->layers[i]);
        while(!ViewPortArray_end_p(it)) {
            if(*ViewPortArray_ref(it) == view_port) {
                ViewPortArray_remove(gui->layers[i], it);
                furi_assert(layer == GuiLayerMAX);
                layer = i;
            } else {
                ViewPortArray_next(it);
            }
        }
    }
    furi_assert(layer != GuiLayerMAX);
    ViewPortArray_push_at(gui->layers[layer], 0, view_port);
    gui_unlock(gui);

    gui_update(gui);
}

void gui_add_framebuffer_callback(Gui* gui, GuiCanvasCommitCallback callback, void* context) {
    furi_check(gui);
    canvas_add_framebuffer_callback(gui->canvas, callback, context);
    gui_update(gui);
}

void gui_remove_framebuffer_callback(Gui* gui, GuiCanvasCommitCallback callback, void* context) {
    furi_check(gui);
    canvas_remove_framebuffer_callback(gui->canvas, callback, context);
}

size_t gui_get_framebuffer_size(const Gui* gui) {
    furi_check(gui);
    return canvas_get_buffer_size(gui->canvas);
}

void gui_set_lockdown(Gui* gui, bool lockdown) {
    furi_check(gui);
    gui_lock(gui);
    gui->lockdown = lockdown;
    gui_unlock(gui);
    gui_update(gui);
}

void gui_set_lockdown_inhibit(Gui* gui, bool inhibit) {
    furi_check(gui);
    gui_lock(gui);
    gui->lockdown_inhibit = inhibit;
    gui_unlock(gui);
    gui_update(gui);
}

bool gui_is_lockdown(const Gui* gui) {
    furi_check(gui);
    return gui->lockdown && !gui->lockdown_inhibit;
}

void gui_set_hide_status_bar(Gui* gui, bool hide) {
    furi_check(gui);
    gui_lock(gui);
    gui->hide_status_bar = hide;
    gui_unlock(gui);
    gui_update(gui);
}

void gui_set_statusbar_show_icons(Gui* gui, bool show) {
    furi_check(gui);
    gui_lock(gui);
    gui->statusbar_show_icons = show;
    gui_unlock(gui);
    gui_update(gui);
}

Canvas* gui_direct_draw_acquire(Gui* gui) {
    furi_check(gui);
    gui_lock(gui);
    gui->direct_draw = true;
    gui_unlock(gui);
    canvas_set_orientation(gui->canvas, CanvasOrientationHorizontal);
    canvas_frame_set(gui->canvas, 0, 0, GUI_DISPLAY_WIDTH, GUI_DISPLAY_HEIGHT);
    canvas_reset(gui->canvas);
    canvas_commit(gui->canvas);
    return gui->canvas;
}

void gui_direct_draw_release(Gui* gui) {
    furi_check(gui);
    canvas_reset(gui->canvas);
    canvas_commit(gui->canvas);
    gui_lock(gui);
    gui->direct_draw = false;
    gui_unlock(gui);
    gui_update(gui);
}

Gui* gui_alloc(void) {
    Gui* gui = malloc(sizeof(Gui));
    gui->thread_id = furi_thread_get_current_id();
    gui->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    gui->statusbar_show_icons = true;

    for(size_t i = 0; i < GuiLayerMAX; i++) {
        ViewPortArray_init(gui->layers[i]);
    }

    gui->canvas = canvas_init();
    gui->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    gui->input_events = furi_record_open(RECORD_INPUT_EVENTS);
    furi_pubsub_subscribe(gui->input_events, gui_input_events_callback, gui);

    gui->screenshot_overlay_timer =
        furi_timer_alloc(gui_screenshot_overlay_timeout, FuriTimerTypeOnce, gui);

    return gui;
}

int32_t gui_srv(void* p) {
    UNUSED(p);
    Gui* gui = gui_alloc();
    furi_record_create(RECORD_GUI, gui);

    while(1) {
        uint32_t flags =
            furi_thread_flags_wait(GUI_THREAD_FLAG_ALL, FuriFlagWaitAny, FuriWaitForever);
        if(flags & GUI_THREAD_FLAG_INPUT) {
            InputEvent input_event;
            while(furi_message_queue_get(gui->input_queue, &input_event, 0) == FuriStatusOk) {
                gui_input(gui, &input_event);
            }
        }
        if(flags & GUI_THREAD_FLAG_DRAW) {
            furi_thread_flags_clear(GUI_THREAD_FLAG_DRAW);
            gui_redraw(gui);
        }
    }

    return 0;
}
