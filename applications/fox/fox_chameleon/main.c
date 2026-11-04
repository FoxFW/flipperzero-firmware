#include "app.h"
#include "chameleon_protocol.h"
#include "key_dictionary.h"
#include "gpio_remap_compat.h"
#include "fox_chameleon_icons.h"

#include <storage/storage.h>
#include <furi_hal_rtc.h>
#include <gui/elements.h>
#include <gui/icon.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#define CHAMELEON_DUMP_BLOCK_COUNT 64

#define FOX_TERMINAL_LOG_MAX_CHARS 4000

static void action_check_esp32(App* app);
static void check_button_callback(GuiButtonType result, InputType type, void* context);
static void render_main_menu(App* app);
static void ensure_dir_path(Storage* storage, const char* path);
static void app_render_log(App* app);
static bool navigation_callback(void* context);

typedef struct {
    FuriHalSerialId serial_id;
    const char* label;
} PinOption;

static const PinOption pin_options[] = {
    {FuriHalSerialIdUsart, "13/14 (USART)"},
    {FuriHalSerialIdLpuart, "15/16 (LPUART)"},
};
#define PIN_OPTION_COUNT (sizeof(pin_options) / sizeof(pin_options[0]))

static const uint32_t baud_options[] =
    {9600, 19200, 38400, 57600, 74880, 115200, 230400, 460800, 921600};
#define BAUD_OPTION_COUNT (sizeof(baud_options) / sizeof(baud_options[0]))
#define BAUD_OPTION_DEFAULT_INDEX 5

#define CHAMELEON_RESPONSE_BUFFER_MAX 48

static void hex_encode(const uint8_t* data, size_t length, FuriString* out) {
    furi_string_reset(out);
    for(size_t i = 0; i < length; i++) {
        furi_string_cat_printf(out, "%02X", data[i]);
    }
}

static size_t hex_decode(const char* hex, uint8_t* out, size_t out_capacity) {
    size_t len = strlen(hex);
    if(len == 0 || len % 2 != 0) return 0;
    size_t byte_count = len / 2;
    if(byte_count > out_capacity) return 0;

    for(size_t i = 0; i < byte_count; i++) {
        unsigned value = 0;
        if(sscanf(hex + i * 2, "%2x", &value) != 1) return 0;
        out[i] = (uint8_t)value;
    }
    return byte_count;
}

static bool parse_found_line(
    const char* line,
    char* mac_out,
    size_t mac_out_capacity,
    int* rssi_out,
    char* name_out,
    size_t name_out_capacity) {
    if(strncmp(line, "FOUND:", 6) != 0) return false;
    const char* mac_start = line + 6;

    const char* space = strchr(mac_start, ' ');
    if(space == NULL) return false;
    size_t mac_len = (size_t)(space - mac_start);
    if(mac_len == 0 || mac_len >= mac_out_capacity) return false;
    memcpy(mac_out, mac_start, mac_len);
    mac_out[mac_len] = '\0';

    const char* rssi_tag = strstr(space, "rssi:");
    if(rssi_tag == NULL) return false;
    *rssi_out = atoi(rssi_tag + 5);

    const char* name_tag = strstr(rssi_tag, "name:");
    if(name_tag == NULL) return false;
    strncpy(name_out, name_tag + 5, name_out_capacity - 1);
    name_out[name_out_capacity - 1] = '\0';

    return true;
}

static void app_log_reset(App* app) {
    furi_string_reset(app->log);
}

static void app_terminal_log_line(App* app, const char* line) {
    if(furi_string_size(app->terminal_log_path) == 0) return;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(
           file, furi_string_get_cstr(app->terminal_log_path), FSAM_WRITE, FSOM_OPEN_APPEND)) {
        storage_file_write(file, line, strlen(line));
        storage_file_write(file, "\n", 1);
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

static void app_terminal_start_session(App* app) {
    app_log_reset(app);
    app->terminal_scroll = 0;

    DateTime now;
    furi_hal_rtc_get_datetime(&now);

    FuriString* filename = furi_string_alloc_printf(
        "%s/%04u%02u%02u-%02u%02u%02u.txt",
        FOX_CHAMELEON_LOG_DIR,
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    ensure_dir_path(storage, FOX_CHAMELEON_LOG_DIR);
    furi_record_close(RECORD_STORAGE);

    furi_string_set(app->terminal_log_path, filename);
    furi_string_free(filename);
}

static void app_log(App* app, const char* fmt, ...) {
    char buffer[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if(furi_string_size(app->log) > 0) furi_string_cat(app->log, "\n");
    furi_string_cat(app->log, buffer);

    if(furi_string_size(app->log) > FOX_TERMINAL_LOG_MAX_CHARS) {
        size_t excess = furi_string_size(app->log) - FOX_TERMINAL_LOG_MAX_CHARS;
        size_t cut = furi_string_search_char(app->log, '\n', excess);
        cut = (cut == FURI_STRING_FAILURE) ? excess : (cut + 1);
        furi_string_right(app->log, cut);
    }

    app_terminal_log_line(app, buffer);
}

static void app_render_log(App* app) {
    app->terminal_scroll = (size_t)-1;
    app->current_view = FoxChameleonViewTerminal;
    view_dispatcher_switch_to_view(app->view_dispatcher, FoxChameleonViewTerminal);
}

static char* next_config_line(char** cursor) {
    if(*cursor == NULL) return NULL;
    char* start = *cursor;
    while(*start == '\r' || *start == '\n') start++;
    if(*start == '\0') {
        *cursor = NULL;
        return NULL;
    }
    char* end = start;
    while(*end != '\0' && *end != '\r' && *end != '\n') end++;
    if(*end != '\0') {
        *end = '\0';
        end++;
    }
    *cursor = end;
    return start;
}

static void app_load_config(App* app) {
    furi_string_reset(app->chameleon_mac);
    furi_string_reset(app->gatt_service_uuid);
    furi_string_reset(app->gatt_write_char_uuid);
    furi_string_reset(app->gatt_notify_char_uuid);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);

    if(storage_file_open(file, FOX_CHAMELEON_CONFIG_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buffer[512];
        uint16_t read = storage_file_read(file, buffer, sizeof(buffer) - 1);
        buffer[read] = '\0';

        char* cursor = buffer;
        char* line = next_config_line(&cursor);
        while(line != NULL) {
            char key[32] = {0};
            char value[64] = {0};
            if(sscanf(line, "%31[^=]=%63s", key, value) == 2) {
                if(strcmp(key, "mac") == 0) {
                    furi_string_set(app->chameleon_mac, value);
                } else if(strcmp(key, "service") == 0) {
                    furi_string_set(app->gatt_service_uuid, value);
                } else if(strcmp(key, "write_char") == 0) {
                    furi_string_set(app->gatt_write_char_uuid, value);
                } else if(strcmp(key, "notify_char") == 0) {
                    furi_string_set(app->gatt_notify_char_uuid, value);
                }
            }
            line = next_config_line(&cursor);
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

static bool app_expect_ok(App* app, uint32_t timeout_ms) {
    EspAtMsg msg;
    uint32_t deadline = furi_get_tick() + timeout_ms;

    while(furi_get_tick() < deadline) {
        uint32_t remaining = deadline - furi_get_tick();
        if(!esp_at_receive(app->esp_at, &msg, remaining)) break;

        app_log(app, "%s", msg.line);
        if(strcmp(msg.line, "OK") == 0) return true;
        if(strcmp(msg.line, "ERROR") == 0) return false;
    }
    return false;
}

static bool app_await_chameleon_response(
    App* app,
    uint8_t* decode_buffer,
    size_t decode_buffer_capacity,
    ChameleonFrame* parsed,
    uint32_t timeout_ms) {
    EspAtMsg msg;
    uint32_t deadline = furi_get_tick() + timeout_ms;

    while(furi_get_tick() < deadline) {
        uint32_t remaining = deadline - furi_get_tick();
        if(!esp_at_receive(app->esp_at, &msg, remaining)) break;

        if(strncmp(msg.line, "NOTIFY:", 7) == 0) {
            size_t decoded_len = hex_decode(msg.line + 7, decode_buffer, decode_buffer_capacity);
            if(decoded_len == 0) {
                app_log(app, "Malformed response frame");
                app_log(app, "hex decode failed, raw line:");
                app_log(app, "%.100s", msg.line);
                return false;
            }
            ChameleonFrameError parse_error = ChameleonFrameOk;
            if(!chameleon_parse_frame_ex(decode_buffer, decoded_len, parsed, &parse_error)) {
                app_log(app, "Malformed response frame");
                app_log(app, "%s", chameleon_frame_error_str(parse_error));
                app_log(app, "%u bytes decoded:", (unsigned)decoded_len);
                FuriString* rehex = furi_string_alloc();
                hex_encode(decode_buffer, decoded_len, rehex);
                app_log(app, "%.100s", furi_string_get_cstr(rehex));
                furi_string_free(rehex);
                return false;
            }
            return true;
        }
        app_log(app, "%s", msg.line);
    }

    app_log(app, "No response from Chameleon");
    return false;
}

static bool app_write_command_and_await(
    App* app,
    const uint8_t* frame,
    size_t frame_len,
    uint8_t* decode_buffer,
    size_t decode_buffer_capacity,
    ChameleonFrame* parsed) {
    FuriString* hex = furi_string_alloc();
    hex_encode(frame, frame_len, hex);

    FuriString* cmd = furi_string_alloc_printf("BLEWRITE:%s", furi_string_get_cstr(hex));
    furi_string_free(hex);
    esp_at_send(app->esp_at, furi_string_get_cstr(cmd));
    furi_string_free(cmd);

    if(!app_expect_ok(app, 3000)) {
        app_log(app, "Write failed");
        return false;
    }

    return app_await_chameleon_response(app, decode_buffer, decode_buffer_capacity, parsed, 4000);
}

#define RAW_CAPTURE_DIR "/ext/apps_data/fox_chameleon/debug"
#define RAW_CAPTURE_ROW_LEN 16

static void ensure_dir_path(Storage* storage, const char* path) {
    char buffer[128];
    strncpy(buffer, path, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    for(size_t i = 1; buffer[i] != '\0'; i++) {
        if(buffer[i] == '/') {
            buffer[i] = '\0';
            storage_common_mkdir(storage, buffer);
            buffer[i] = '/';
        }
    }
    storage_common_mkdir(storage, buffer);
}

static void app_ensure_config_defaults(App* app) {
    if(furi_string_size(app->gatt_service_uuid) == 0) {
        furi_string_set(app->gatt_service_uuid, FOX_CHAMELEON_DEFAULT_SERVICE_UUID);
    }
    if(furi_string_size(app->gatt_write_char_uuid) == 0) {
        furi_string_set(app->gatt_write_char_uuid, FOX_CHAMELEON_DEFAULT_WRITE_CHAR_UUID);
    }
    if(furi_string_size(app->gatt_notify_char_uuid) == 0) {
        furi_string_set(app->gatt_notify_char_uuid, FOX_CHAMELEON_DEFAULT_NOTIFY_CHAR_UUID);
    }
}

static void app_save_config(App* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    ensure_dir_path(storage, FOX_CHAMELEON_CONFIG_DIR);
    File* file = storage_file_alloc(storage);

    if(storage_file_open(file, FOX_CHAMELEON_CONFIG_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FuriString* content = furi_string_alloc_printf(
            "mac=%s\nservice=%s\nwrite_char=%s\nnotify_char=%s\n",
            furi_string_get_cstr(app->chameleon_mac),
            furi_string_get_cstr(app->gatt_service_uuid),
            furi_string_get_cstr(app->gatt_write_char_uuid),
            furi_string_get_cstr(app->gatt_notify_char_uuid));
        storage_file_write(
            file, furi_string_get_cstr(content), (uint16_t)furi_string_size(content));
        furi_string_free(content);
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

static bool dump_raw_capture_to_sd(App* app, const char* filename, const char* header) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    ensure_dir_path(storage, RAW_CAPTURE_DIR);
    File* file = storage_file_alloc(storage);

    FuriString* path = furi_string_alloc_printf("%s/%s", RAW_CAPTURE_DIR, filename);
    bool opened =
        storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_ALWAYS);
    furi_string_free(path);

    if(opened) {
        char line[96];
        size_t n = (size_t)snprintf(line, sizeof(line), "%.90s\n\n", header);
        storage_file_write(file, line, (uint16_t)n);

        uint8_t buf[RAW_CAPTURE_ROW_LEN];
        size_t total = 0;
        size_t got;
        while((got = esp_at_raw_capture_read(app->esp_at, buf, sizeof(buf), 50)) > 0) {
            size_t pos = (size_t)snprintf(line, sizeof(line), "%04X  ", (unsigned)total);
            for(size_t i = 0; i < RAW_CAPTURE_ROW_LEN; i++) {
                if(i < got) {
                    pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "%02X ", buf[i]);
                } else {
                    pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "   ");
                }
            }
            pos += (size_t)snprintf(line + pos, sizeof(line) - pos, " ");
            for(size_t i = 0; i < got && pos < sizeof(line) - 2; i++) {
                char c = (char)buf[i];
                line[pos++] = (c >= 32 && c < 127) ? c : '.';
            }
            line[pos++] = '\n';
            storage_file_write(file, line, (uint16_t)pos);
            total += got;
        }

        if(total == 0) {
            const char* msg = "No bytes captured - nothing arrived on this UART during this attempt.\n";
            storage_file_write(file, msg, (uint16_t)strlen(msg));
        } else {
            char footer[48];
            size_t fn =
                (size_t)snprintf(footer, sizeof(footer), "\n%u bytes total.\n", (unsigned)total);
            storage_file_write(file, footer, (uint16_t)fn);
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return opened;
}

static void log_dump_result(App* app, bool saved, const char* filename) {
    if(saved) {
        app_log(app, "Raw dump: debug/%s", filename);
    } else {
        app_log(app, "Could not save debug/%s", filename);
        app_log(app, "Check the SD card is");
        app_log(app, "inserted and writable.");
    }
}

static void action_check_esp32(App* app) {
    app_log(app, "Checking for ESP32...");
    app_render_log(app);

    esp_at_send(app->esp_at, "AT");
    app->esp32_detected = app_expect_ok(app, 2500);

    if(app->esp32_detected) {
        app->current_view = FoxChameleonViewMenu;
        view_dispatcher_switch_to_view(app->view_dispatcher, FoxChameleonViewMenu);
        return;
    }

    bool dumped =
        dump_raw_capture_to_sd(app, "check_esp32_raw.txt", "Raw bytes seen while waiting for an AT reply.");

    widget_reset(app->widget);
    widget_add_text_scroll_element(
        app->widget,
        2,
        2,
        124,
        45,
        dumped ? "This app requires an\n"
                 "external ESP32 running\n"
                 "Fox ESP32 Firmware,\n"
                 "wired to the\n"
                 "selected GPIO pins.\n"
                 "\n"
                 "No response. Raw dump\n"
                 "in debug/ on SD card."
               : "This app requires an\n"
                 "external ESP32 running\n"
                 "Fox ESP32 Firmware,\n"
                 "wired to the\n"
                 "selected GPIO pins.\n"
                 "\n"
                 "No response, and the\n"
                 "debug dump failed too\n"
                 "- check the SD card.");
    widget_add_button_element(app->widget, GuiButtonTypeCenter, "Retry", check_button_callback, app);
    widget_add_button_element(app->widget, GuiButtonTypeLeft, "Skip", check_button_callback, app);
    app->current_view = FoxChameleonViewMessage;
    view_dispatcher_switch_to_view(app->view_dispatcher, FoxChameleonViewMessage);
}

static void check_button_callback(GuiButtonType result, InputType type, void* context) {
    App* app = context;
    if(type != InputTypeShort) return;

    if(result == GuiButtonTypeCenter) {
        action_check_esp32(app);
    } else if(result == GuiButtonTypeLeft) {
        app->esp32_detected = true;
        app->current_view = FoxChameleonViewMenu;
        view_dispatcher_switch_to_view(app->view_dispatcher, FoxChameleonViewMenu);
    }
}

static bool action_connect_with_mac(App* app, const char* mac) {
    FuriString* cmd = furi_string_alloc_printf("BLECONN:%s", mac);
    esp_at_send(app->esp_at, furi_string_get_cstr(cmd));
    furi_string_free(cmd);

    app_log(app, "Connecting to Chameleon,");
    app_log(app, "can take up to 45s...");
    app_render_log(app);

    app->ble_connected = app_expect_ok(app, 45000);

    if(!app->ble_connected) {
        app_log(app, "Connection failed");
        bool dumped = dump_raw_capture_to_sd(
            app, "connect_raw.txt", "Raw bytes seen during the full Connect sequence.");
        log_dump_result(app, dumped, "connect_raw.txt");
        render_main_menu(app);
        app_render_log(app);
        return false;
    }

    cmd = furi_string_alloc_printf("BLESVC:%s", furi_string_get_cstr(app->gatt_service_uuid));
    esp_at_send(app->esp_at, furi_string_get_cstr(cmd));
    furi_string_free(cmd);
    if(!app_expect_ok(app, 3000)) {
        app_log(app, "Service not found");
        bool dumped = dump_raw_capture_to_sd(
            app, "connect_raw.txt", "Raw bytes seen during the full Connect sequence.");
        log_dump_result(app, dumped, "connect_raw.txt");
        app->ble_connected = false;
        render_main_menu(app);
        app_render_log(app);
        return false;
    }

    cmd = furi_string_alloc_printf(
        "BLECHAR:%s,%s",
        furi_string_get_cstr(app->gatt_write_char_uuid),
        furi_string_get_cstr(app->gatt_notify_char_uuid));
    esp_at_send(app->esp_at, furi_string_get_cstr(cmd));
    furi_string_free(cmd);
    bool chars_ok = app_expect_ok(app, 3000);

    app_log(app, chars_ok ? "Connected" : "Characteristics not found");
    bool dumped =
        dump_raw_capture_to_sd(app, "connect_raw.txt", "Raw bytes seen during the full Connect sequence.");
    if(!chars_ok) {
        log_dump_result(app, dumped, "connect_raw.txt");
        app->ble_connected = false;
    }

    render_main_menu(app);
    app_render_log(app);
    return app->ble_connected;
}

static App* s_candidate_view_app = NULL;

#define CANDIDATE_ROW_HEADER_H 14
#define CANDIDATE_ROW_H        22
#define CANDIDATE_ROW_VIS      2

static void candidate_draw_cb(Canvas* canvas, void* model) {
    UNUSED(model);
    App* app = s_candidate_view_app;
    if(app == NULL) return;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Select a Chameleon");

    if(app->candidate_count == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignCenter, "None found");
        return;
    }

    for(size_t i = app->candidate_scroll;
        i < app->candidate_count && (i - app->candidate_scroll) < CANDIDATE_ROW_VIS;
        i++) {
        int row = (int)(i - app->candidate_scroll);
        int ry = CANDIDATE_ROW_HEADER_H + row * CANDIDATE_ROW_H;
        int by = ry + 1;
        int bh = CANDIDATE_ROW_H - 2;
        bool selected = (i == app->candidate_selected);

        if(selected) {
            canvas_draw_rbox(canvas, 8, by, 112, bh, 3);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, 8, by, 112, bh, 3);
        }

        char line1[40];
        snprintf(line1, sizeof(line1), "Chameleon Ultra [%ddBm]", app->candidates[i].rssi);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, by + 5, AlignCenter, AlignCenter, line1);

        char line2[32];
        snprintf(line2, sizeof(line2), "MAC: %.23s", app->candidates[i].mac);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, by + 15, AlignCenter, AlignCenter, line2);

        canvas_set_color(canvas, ColorBlack);
    }

    if(app->candidate_count > CANDIDATE_ROW_VIS) {

        int available_h = 64 - CANDIDATE_ROW_HEADER_H;
        elements_scrollbar_pos(
            canvas,
            128,
            CANDIDATE_ROW_HEADER_H,
            (size_t)available_h,
            (size_t)app->candidate_scroll,
            (size_t)app->candidate_count);
    }
}

static void candidate_select(App* app, size_t index) {
    if(index >= app->candidate_count) return;
    furi_string_set(app->chameleon_mac, app->candidates[index].mac);
    app_save_config(app);
    action_connect_with_mac(app, app->candidates[index].mac);
}

static bool candidate_input_cb(InputEvent* event, void* context) {
    App* app = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;
    if(app->candidate_count == 0) return false;

    switch(event->key) {
    case InputKeyUp:
        if(app->candidate_selected > 0) {
            app->candidate_selected--;
            if(app->candidate_selected < app->candidate_scroll) {
                app->candidate_scroll = app->candidate_selected;
            }
        } else {
            app->candidate_selected = app->candidate_count - 1;
            app->candidate_scroll = (app->candidate_count > CANDIDATE_ROW_VIS) ?
                                         app->candidate_count - CANDIDATE_ROW_VIS :
                                         0;
        }
        with_view_model(app->candidate_view, uint8_t * _m, { UNUSED(_m); }, true);
        return true;
    case InputKeyDown:
        if(app->candidate_selected + 1 < app->candidate_count) {
            app->candidate_selected++;
            if(app->candidate_selected >= app->candidate_scroll + CANDIDATE_ROW_VIS) {
                app->candidate_scroll = app->candidate_selected - CANDIDATE_ROW_VIS + 1;
            }
        } else {
            app->candidate_selected = 0;
            app->candidate_scroll = 0;
        }
        with_view_model(app->candidate_view, uint8_t * _m, { UNUSED(_m); }, true);
        return true;
    case InputKeyOk:
    case InputKeyRight:
        candidate_select(app, app->candidate_selected);
        return true;
    case InputKeyBack:
    case InputKeyLeft:
        return false;
    default:
        return false;
    }
}

static App* s_terminal_view_app = NULL;

#define TERMINAL_HEADER_H         10
#define TERMINAL_MAX_WRAPPED_LINES 256
#define TERMINAL_MEASURE_BUF_MAX  136
#define TERMINAL_HIDE_BTN_H       11

static size_t terminal_chars_per_line(Canvas* canvas, int32_t max_width) {
    uint16_t w = canvas_string_width(canvas, "W");
    if(w == 0) w = 6;
    size_t n = (size_t)(max_width / w);
    return n < 4 ? 4 : n;
}

typedef struct {
    uint16_t offset;
    uint16_t length;
} TerminalWrapLine;

static size_t terminal_wrap_log(
    const char* text,
    size_t text_len,
    size_t chars_per_line,
    TerminalWrapLine* out,
    size_t out_capacity) {
    size_t count = 0;
    size_t line_start = 0;

    while(line_start <= text_len && count < out_capacity) {
        size_t line_end = line_start;
        while(line_end < text_len && text[line_end] != '\n') line_end++;

        if(line_end == line_start) {
            out[count].offset = (uint16_t)line_start;
            out[count].length = 0;
            count++;
        } else {
            size_t pos = line_start;
            while(pos < line_end && count < out_capacity) {
                size_t remaining = line_end - pos;
                size_t take = remaining < chars_per_line ? remaining : chars_per_line;
                size_t chunk_end = pos + take;

                if(take == chars_per_line && chunk_end < line_end) {
                    size_t min_break = pos + (chars_per_line / 3);
                    for(size_t i = chunk_end; i > pos && i > min_break; i--) {
                        if(text[i - 1] == ' ') {
                            chunk_end = i - 1;
                            break;
                        }
                    }
                }

                out[count].offset = (uint16_t)pos;
                out[count].length = (uint16_t)(chunk_end - pos);
                count++;
                pos = chunk_end;
                if(pos < line_end && text[pos] == ' ') pos++;
            }
        }

        if(line_end == text_len) break;
        line_start = line_end + 1;
    }

    return count;
}

static void terminal_draw_cb(Canvas* canvas, void* model) {
    UNUSED(model);
    App* app = s_terminal_view_app;
    if(app == NULL) return;

    canvas_clear(canvas);

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, TERMINAL_HEADER_H);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas, 64, TERMINAL_HEADER_H / 2, AlignCenter, AlignCenter, "TERMINAL");
    canvas_set_color(canvas, ColorBlack);

    const char* text = furi_string_get_cstr(app->log);
    size_t text_len = furi_string_size(app->log);

    int32_t max_width = 122;
    size_t chars_per_line = terminal_chars_per_line(canvas, max_width);

    static TerminalWrapLine lines[TERMINAL_MAX_WRAPPED_LINES];
    size_t total = terminal_wrap_log(text, text_len, chars_per_line, lines, TERMINAL_MAX_WRAPPED_LINES);

    size_t line_height = canvas_current_font_height(canvas);
    if(line_height == 0) line_height = 8;
    size_t content_top = TERMINAL_HEADER_H + 1;
    size_t content_height = 64 - content_top;
    size_t visible_rows = content_height / line_height;
    if(visible_rows == 0) visible_rows = 1;

    size_t max_scroll = total > visible_rows ? total - visible_rows : 0;
    if(app->terminal_scroll > max_scroll) app->terminal_scroll = max_scroll;

    for(size_t row = 0; row < visible_rows && (app->terminal_scroll + row) < total; row++) {
        const TerminalWrapLine* wl = &lines[app->terminal_scroll + row];
        char buf[TERMINAL_MEASURE_BUF_MAX];
        size_t n = wl->length < (TERMINAL_MEASURE_BUF_MAX - 1) ? wl->length :
                                                                  (TERMINAL_MEASURE_BUF_MAX - 1);
        memcpy(buf, text + wl->offset, n);
        buf[n] = '\0';
        int32_t y = (int32_t)(content_top + row * line_height + line_height - 1);
        canvas_draw_str(canvas, 2, y, buf);
    }

    if(total > visible_rows) {
        int32_t bar_x = 126;
        int32_t bar_top = (int32_t)content_top;
        int32_t bar_h = (int32_t)content_height;
        canvas_draw_line(canvas, bar_x, bar_top, bar_x, bar_top + bar_h);

        int32_t dot_h = bar_h * (int32_t)visible_rows / (int32_t)total;
        if(dot_h < 3) dot_h = 3;
        int32_t dot_y =
            bar_top + (bar_h - dot_h) * (int32_t)app->terminal_scroll / (int32_t)max_scroll;
        canvas_draw_box(canvas, bar_x - 1, dot_y, 3, dot_h);
    }

    {

        canvas_set_font(canvas, FontSecondary);
        const char* label = "Hide";
        const Icon* icon = &I_ButtonCenter_7x7;
        int32_t icon_w = icon_get_width(icon);
        int32_t icon_h = icon_get_height(icon);
        int32_t icon_gap = 3;
        int32_t pad_x = 10;
        int32_t content_w = icon_w + icon_gap + (int32_t)canvas_string_width(canvas, label);
        int32_t btn_w = content_w + pad_x * 2;

        int32_t btn_y2 = 63;
        int32_t btn_y1 = btn_y2 - TERMINAL_HIDE_BTN_H;
        int32_t btn_x1 = (128 - btn_w) / 2;

        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rbox(canvas, btn_x1, btn_y1, btn_w, TERMINAL_HIDE_BTN_H, 2);
        canvas_set_color(canvas, ColorWhite);

        int32_t gx = btn_x1 + (btn_w - content_w) / 2;
        int32_t gy_icon = btn_y1 + (TERMINAL_HIDE_BTN_H - icon_h) / 2;
        canvas_draw_icon(canvas, gx, gy_icon, icon);
        canvas_draw_str_aligned(
            canvas,
            gx + icon_w + icon_gap,
            btn_y1 + TERMINAL_HIDE_BTN_H / 2,
            AlignLeft,
            AlignCenter,
            label);

        canvas_set_color(canvas, ColorBlack);
    }
}

static bool terminal_input_cb(InputEvent* event, void* context) {
    App* app = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    switch(event->key) {
    case InputKeyUp:
        if(app->terminal_scroll > 0) app->terminal_scroll--;
        with_view_model(app->terminal_view, uint8_t * _m, { UNUSED(_m); }, true);
        return true;
    case InputKeyDown:
        app->terminal_scroll++;
        with_view_model(app->terminal_view, uint8_t * _m, { UNUSED(_m); }, true);
        return true;
    case InputKeyOk:

        if(event->type == InputTypeShort) {
            navigation_callback(app);
        }
        return true;
    case InputKeyLeft:
    case InputKeyRight:

        return true;
    case InputKeyBack:
        return false;
    default:
        return false;
    }
}

static void action_scan_for_chameleons(App* app) {
    app_log(app, "Scanning for Chameleon");
    app_log(app, "Ultra devices nearby...");
    app_render_log(app);

    esp_at_send(app->esp_at, "BLESCAN");

    app->candidate_count = 0;
    EspAtMsg msg;

    uint32_t deadline = furi_get_tick() + 8000;
    while(furi_get_tick() < deadline) {
        uint32_t remaining = deadline - furi_get_tick();
        if(!esp_at_receive(app->esp_at, &msg, remaining)) break;
        if(strcmp(msg.line, "SCANDONE") == 0) break;

        char mac[24];
        int rssi = 0;
        char name[32];
        if(parse_found_line(msg.line, mac, sizeof(mac), &rssi, name, sizeof(name)) &&
           strstr(name, "Chameleon") != NULL && app->candidate_count < CHAMELEON_CANDIDATE_MAX) {
            strncpy(
                app->candidates[app->candidate_count].mac,
                mac,
                sizeof(app->candidates[0].mac) - 1);
            app->candidates[app->candidate_count].mac[sizeof(app->candidates[0].mac) - 1] = '\0';
            app->candidates[app->candidate_count].rssi = rssi;
            app->candidate_count++;
        }
    }

    if(app->candidate_count == 0) {
        app_log(app, "No Chameleon Ultra");
        app_log(app, "devices found nearby.");
        app_render_log(app);
        return;
    }

    if(app->candidate_count == 1) {
        candidate_select(app, 0);
        return;
    }

    app->candidate_selected = 0;
    app->candidate_scroll = 0;
    app->current_view = FoxChameleonViewCandidateMenu;
    view_dispatcher_switch_to_view(app->view_dispatcher, FoxChameleonViewCandidateMenu);
}

static void action_disconnect_current_cu_if_any(App* app) {
    if(!app->ble_connected) return;
    esp_at_send(app->esp_at, "BLEDISC");
    app_expect_ok(app, 3000);
    app->ble_connected = false;
    render_main_menu(app);
}

static void action_connect(App* app) {
    app_terminal_start_session(app);
    app_log(app, "Starting BLE link");
    app_render_log(app);

    esp_at_raw_capture_start(app->esp_at);

    esp_at_send(app->esp_at, "AT");
    app_expect_ok(app, 2000);

    esp_at_send(app->esp_at, "BLEINIT");
    if(!app_expect_ok(app, 3000)) {
        app_log(app, "BLE init failed");
        bool dumped = dump_raw_capture_to_sd(app, "connect_raw.txt", "Raw bytes seen during AT / BLEINIT.");
        log_dump_result(app, dumped, "connect_raw.txt");
        app_render_log(app);
        return;
    }
    app->ble_initialized = true;
    render_main_menu(app);

    if(furi_string_size(app->chameleon_mac) > 0) {
        if(action_connect_with_mac(app, furi_string_get_cstr(app->chameleon_mac))) {
            return;
        }
        app_log(app, "Saved MAC didn't work -");
        app_log(app, "scanning instead...");
        app_render_log(app);
    }

    action_scan_for_chameleons(app);
}

static void action_reconnect_saved(App* app) {
    if(furi_string_size(app->chameleon_mac) == 0) {
        app_log(app, "No saved Chameleon Ultra");
        app_log(app, "MAC. Use Search for C.U");
        app_log(app, "instead.");
        app_render_log(app);
        return;
    }

    action_disconnect_current_cu_if_any(app);
    action_connect_with_mac(app, furi_string_get_cstr(app->chameleon_mac));
}

static void action_search_for_cu(App* app) {
    action_disconnect_current_cu_if_any(app);
    action_scan_for_chameleons(app);
}

static void action_disconnect_ble(App* app) {
    action_disconnect_current_cu_if_any(app);

    app->ble_initialized = false;
    render_main_menu(app);

    app_log(app, "BLE turned off.");
    app_log(app, "Press Connect to start");
    app_log(app, "a new BLE session.");
    app->current_view = FoxChameleonViewMenu;
    view_dispatcher_switch_to_view(app->view_dispatcher, FoxChameleonViewMenu);
    app_render_log(app);
}

static void action_open_connection_menu(App* app) {
    app->current_view = FoxChameleonViewConnectionMenu;
    view_dispatcher_switch_to_view(app->view_dispatcher, FoxChameleonViewConnectionMenu);
}

static void connection_submenu_callback(void* context, uint32_t index) {
    App* app = context;
    switch(index) {
    case ConnectionMenuIndexConnectToCU:
        action_reconnect_saved(app);
        break;
    case ConnectionMenuIndexSearchForCU:
        action_search_for_cu(app);
        break;
    case ConnectionMenuIndexDisconnectBLE:
        action_disconnect_ble(app);
        break;
    default:
        break;
    }
}

static void action_disconnect_and_quit(App* app) {
    app_log(app, "Disconnecting and closing");
    app_log(app, "Fox Chameleon...");

    if(app->esp_at != NULL) {
        if(app->ble_connected) {
            esp_at_send(app->esp_at, "BLEDISC");
            app_expect_ok(app, 3000);
        }
        esp_at_free(app->esp_at);
        app->esp_at = NULL;
    }

    view_dispatcher_stop(app->view_dispatcher);
}

static void action_chameleon_command_ex(
    App* app,
    size_t (*builder)(uint8_t*, size_t),
    const char* label,
    void (*formatter)(const ChameleonFrame*, char*, size_t)) {
    if(!app->ble_connected) {
        app_log(app, "Not connected");
        app_render_log(app);
        return;
    }

    uint8_t frame[32];
    size_t frame_len = builder(frame, sizeof(frame));
    if(frame_len == 0) {
        app_log(app, "Failed to build frame");
        app_render_log(app);
        return;
    }

    app_log(app, "%s", label);
    app_render_log(app);

    ChameleonFrame parsed;
    uint8_t decode_buffer[CHAMELEON_RESPONSE_BUFFER_MAX];
    if(!app_write_command_and_await(
           app, frame, frame_len, decode_buffer, sizeof(decode_buffer), &parsed)) {
        app_render_log(app);
        return;
    }

    char text[80];
    formatter(&parsed, text, sizeof(text));
    app_log(app, "status 0x%04X", parsed.status);
    app_log(app, "%s", text);
    app_render_log(app);
}

static void
    action_chameleon_command(App* app, size_t (*builder)(uint8_t*, size_t), const char* label) {
    action_chameleon_command_ex(app, builder, label, chameleon_format_response);
}

static size_t build_change_mode_reader(uint8_t* out, size_t out_capacity) {
    return chameleon_build_change_device_mode(0x01, out, out_capacity);
}

static size_t build_change_mode_emulator(uint8_t* out, size_t out_capacity) {
    return chameleon_build_change_device_mode(0x00, out, out_capacity);
}

static size_t build_read_slot_block0(uint8_t* out, size_t out_capacity) {
    return chameleon_build_mf1_read_emu_block(0, 1, out, out_capacity);
}

static void format_response_as_uid_block(const ChameleonFrame* frame, char* out, size_t out_capacity) {
    if(frame->data_len < 7) {
        snprintf(out, out_capacity, "block too short (%u bytes)", frame->data_len);
        return;
    }
    chameleon_format_uid_block(frame->data, out, out_capacity);
}

static void action_set_active_slot(App* app, uint8_t slot) {
    if(!app->ble_connected) {
        app_log(app, "Not connected");
        app_render_log(app);
        return;
    }

    uint8_t frame[16];
    size_t frame_len = chameleon_build_set_active_slot(slot, frame, sizeof(frame));
    if(frame_len == 0) {
        app_log(app, "Failed to build frame");
        app_render_log(app);
        return;
    }

    app_log(app, "Switching to slot %u", slot);
    app_render_log(app);

    ChameleonFrame parsed;
    uint8_t decode_buffer[CHAMELEON_RESPONSE_BUFFER_MAX];
    if(!app_write_command_and_await(
           app, frame, frame_len, decode_buffer, sizeof(decode_buffer), &parsed)) {
        app_render_log(app);
        return;
    }

    char text[80];
    chameleon_format_response(&parsed, text, sizeof(text));
    app_log(app, "status 0x%04X", parsed.status);
    app_log(app, "%s", text);
    app_render_log(app);
}

static void action_read_card_with_dictionary(App* app) {
    if(!app->ble_connected) {
        app_log(app, "Not connected");
        app_render_log(app);
        return;
    }

    KeyDictionary* dictionary = malloc(sizeof(KeyDictionary));
    size_t loaded = key_dictionary_load(dictionary);
    if(loaded == 0) {
        app_log(app, "No dictionary keys found.");
        app_log(app, "Expected on SD card at:");
        app_log(app, "/nfc/assets/mf_classic_dict.nfc");
        app_render_log(app);
        free(dictionary);
        return;
    }

    app_log(app, "Loaded %u keys", (unsigned)loaded);
    app_render_log(app);

    uint8_t mode_frame[16];
    size_t mode_len = chameleon_build_change_device_mode(0x01, mode_frame, sizeof(mode_frame));
    ChameleonFrame mode_response;
    uint8_t mode_decode_buffer[CHAMELEON_RESPONSE_BUFFER_MAX];
    if(mode_len == 0 ||
       !app_write_command_and_await(
           app, mode_frame, mode_len, mode_decode_buffer, sizeof(mode_decode_buffer), &mode_response)) {
        app_log(app, "Could not switch to reader mode");
        app_render_log(app);
        free(dictionary);
        return;
    }

    app_log(app, "Scanning for a card...");
    app_render_log(app);

    uint8_t scan_frame[16];
    size_t scan_len = chameleon_build_hf14a_scan(scan_frame, sizeof(scan_frame));
    ChameleonFrame scan_response;
    uint8_t scan_decode_buffer[CHAMELEON_RESPONSE_BUFFER_MAX];
    if(scan_len == 0 ||
       !app_write_command_and_await(
           app, scan_frame, scan_len, scan_decode_buffer, sizeof(scan_decode_buffer), &scan_response) ||
       scan_response.data_len < 1) {
        app_log(app, "No card detected");
        app_render_log(app);
        free(dictionary);
        return;
    }

    app_log(app, "Card found. Trying keys");
    app_log(app, "on sector 0 key A...");
    app_render_log(app);

    bool found = false;
    uint8_t found_key[KEY_DICTIONARY_KEY_LEN];
    uint8_t block_data[16];
    uint8_t key_decode_buffer[CHAMELEON_RESPONSE_BUFFER_MAX];

    for(size_t i = 0; i < dictionary->count; i++) {
        uint8_t frame[16];
        size_t frame_len = chameleon_build_mf1_read_one_block(
            0x60, 0, dictionary->keys[i], frame, sizeof(frame));
        if(frame_len == 0) continue;

        ChameleonFrame parsed;
        if(!app_write_command_and_await(
               app, frame, frame_len, key_decode_buffer, sizeof(key_decode_buffer), &parsed)) {
            app_log(app, "Lost the card mid-search");
            app_render_log(app);
            free(dictionary);
            return;
        }

        if(parsed.data_len >= 16) {
            found = true;
            memcpy(found_key, dictionary->keys[i], KEY_DICTIONARY_KEY_LEN);
            memcpy(block_data, parsed.data, 16);
            break;
        }

        if(i % 10 == 0) {
            app_log(app, "tried %u/%u", (unsigned)i, (unsigned)dictionary->count);
            app_render_log(app);
        }
    }

    free(dictionary);

    if(!found) {
        app_log(app, "No dictionary key opened");
        app_log(app, "sector 0 key A");
        app_render_log(app);
        return;
    }

    char uid_text[80];
    chameleon_format_uid_block(block_data, uid_text, sizeof(uid_text));

    app_log(
        app,
        "Key %02X%02X%02X%02X%02X%02X",
        found_key[0],
        found_key[1],
        found_key[2],
        found_key[3],
        found_key[4],
        found_key[5]);
    app_log(app, "%s", uid_text);
    app_render_log(app);
}

static void action_dump_slot_to_sd(App* app) {
    if(!app->ble_connected) {
        app_log(app, "Not connected");
        app_render_log(app);
        return;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);

    ensure_dir_path(storage, FOX_CHAMELEON_DUMP_DIR);
    File* file = storage_file_alloc(storage);

    if(!storage_file_open(file, FOX_CHAMELEON_DUMP_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        app_log(app, "Could not open dump file");
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        app_render_log(app);
        return;
    }

    app_log(app, "Dumping slot (%u blocks)", CHAMELEON_DUMP_BLOCK_COUNT);
    app_render_log(app);

    bool ok = true;
    uint8_t dump_decode_buffer[CHAMELEON_RESPONSE_BUFFER_MAX];
    for(uint16_t block = 0; block < CHAMELEON_DUMP_BLOCK_COUNT; block++) {
        uint8_t frame[16];
        size_t frame_len = chameleon_build_mf1_read_emu_block((uint8_t)block, 1, frame, sizeof(frame));
        if(frame_len == 0) {
            app_log(app, "Failed to build read for block %u", block);
            ok = false;
            break;
        }

        ChameleonFrame parsed;
        if(!app_write_command_and_await(
               app, frame, frame_len, dump_decode_buffer, sizeof(dump_decode_buffer), &parsed)) {
            app_log(app, "Stopped at block %u", block);
            ok = false;
            break;
        }

        if(parsed.data_len < 16) {
            app_log(app, "Short reply at block %u", block);
            ok = false;
            break;
        }

        uint16_t written = storage_file_write(file, parsed.data, 16);
        if(written != 16) {
            app_log(app, "SD write failed at block %u", block);
            ok = false;
            break;
        }

        if(block % 8 == 0) {
            app_log(app, "block %u/%u", block, CHAMELEON_DUMP_BLOCK_COUNT);
            app_render_log(app);
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    app_log(app, ok ? "Saved to:" : "Dump incomplete.");
    app_log(app, FOX_CHAMELEON_DUMP_FILE);
    app_render_log(app);
}

static void action_open_slot_menu(App* app) {
    app->current_view = FoxChameleonViewSlotMenu;
    view_dispatcher_switch_to_view(app->view_dispatcher, FoxChameleonViewSlotMenu);
}

static void slot_submenu_callback(void* context, uint32_t index) {
    App* app = context;
    action_set_active_slot(app, (uint8_t)index);
}

static void submenu_callback(void* context, uint32_t index) {
    App* app = context;
    switch(index) {
    case MenuIndexConnect:

        if(app->ble_initialized) {
            action_open_connection_menu(app);
        } else {
            action_connect(app);
        }
        break;
    case MenuIndexGetVersion:
        action_chameleon_command(
            app, chameleon_build_get_app_version, "Requesting firmware version");
        break;
    case MenuIndexGetGitVersion:
        action_chameleon_command(app, chameleon_build_get_git_version, "Requesting git version");
        break;
    case MenuIndexGetBattery:
        action_chameleon_command(app, chameleon_build_get_battery_info, "Requesting battery info");
        break;
    case MenuIndexGetSlot:
        action_chameleon_command(app, chameleon_build_get_active_slot, "Requesting active slot");
        break;
    case MenuIndexGetModel:
        action_chameleon_command(app, chameleon_build_get_device_model, "Requesting device model");
        break;
    case MenuIndexGetEnabledSlots:
        action_chameleon_command(
            app, chameleon_build_get_enabled_slots, "Requesting enabled slots");
        break;
    case MenuIndexGetChipId:
        action_chameleon_command(app, chameleon_build_get_device_chip_id, "Requesting chip ID");
        break;
    case MenuIndexGetAddress:
        action_chameleon_command(
            app, chameleon_build_get_device_address, "Requesting BLE address");
        break;
    case MenuIndexSelectSlot:
        action_open_slot_menu(app);
        break;
    case MenuIndexEnterReaderMode:
        action_chameleon_command(app, build_change_mode_reader, "Switching to reader mode");
        break;
    case MenuIndexEnterEmulatorMode:
        action_chameleon_command(app, build_change_mode_emulator, "Switching to emulator mode");
        break;
    case MenuIndexGetDeviceMode:
        action_chameleon_command(app, chameleon_build_get_device_mode, "Requesting device mode");
        break;
    case MenuIndexDetectMifareSupport:
        action_chameleon_command(
            app, chameleon_build_mf1_detect_support, "Checking Mifare Classic support");
        break;
    case MenuIndexScanCard:
        action_chameleon_command_ex(
            app, chameleon_build_hf14a_scan, "Scanning for a card", chameleon_format_hf14a_scan);
        break;
    case MenuIndexReadSlotBlock0:
        action_chameleon_command_ex(
            app, build_read_slot_block0, "Reading slot block 0", format_response_as_uid_block);
        break;
    case MenuIndexReadCardWithDictionary:
        action_read_card_with_dictionary(app);
        break;
    case MenuIndexDumpSlotToSd:
        action_dump_slot_to_sd(app);
        break;
    case MenuIndexDisconnectAndQuit:
        action_disconnect_and_quit(app);
        break;
    default:
        break;
    }
}

static void render_main_menu(App* app) {
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Fox Chameleon");
    submenu_add_item(
        app->submenu,
        app->ble_initialized ? "Connection" : "Connect",
        MenuIndexConnect,
        submenu_callback,
        app);

    if(app->ble_connected) {
        submenu_add_item(
            app->submenu, "Get firmware version", MenuIndexGetVersion, submenu_callback, app);
        submenu_add_item(
            app->submenu, "Get git version", MenuIndexGetGitVersion, submenu_callback, app);
        submenu_add_item(
            app->submenu, "Get battery", MenuIndexGetBattery, submenu_callback, app);
        submenu_add_item(
            app->submenu, "Get active slot", MenuIndexGetSlot, submenu_callback, app);
        submenu_add_item(
            app->submenu, "Get device model", MenuIndexGetModel, submenu_callback, app);
        submenu_add_item(
            app->submenu, "Get enabled slots", MenuIndexGetEnabledSlots, submenu_callback, app);
        submenu_add_item(
            app->submenu, "Get chip ID", MenuIndexGetChipId, submenu_callback, app);
        submenu_add_item(
            app->submenu, "Get BLE address", MenuIndexGetAddress, submenu_callback, app);
        submenu_add_item(
            app->submenu, "Set active slot", MenuIndexSelectSlot, submenu_callback, app);
        submenu_add_item(
            app->submenu, "Enter reader mode", MenuIndexEnterReaderMode, submenu_callback, app);
        submenu_add_item(
            app->submenu,
            "Enter emulator mode",
            MenuIndexEnterEmulatorMode,
            submenu_callback,
            app);
        submenu_add_item(
            app->submenu, "Get device mode", MenuIndexGetDeviceMode, submenu_callback, app);
        submenu_add_item(
            app->submenu,
            "Detect Mifare support",
            MenuIndexDetectMifareSupport,
            submenu_callback,
            app);
        submenu_add_item(app->submenu, "Scan for card", MenuIndexScanCard, submenu_callback, app);
        submenu_add_item(
            app->submenu, "Read slot block 0", MenuIndexReadSlotBlock0, submenu_callback, app);
        submenu_add_item(
            app->submenu,
            "Read card (dictionary)",
            MenuIndexReadCardWithDictionary,
            submenu_callback,
            app);
        submenu_add_item(
            app->submenu, "Dump slot to SD", MenuIndexDumpSlotToSd, submenu_callback, app);
    }

    submenu_add_item(
        app->submenu,
        "Disconnect GPIO & Exit",
        MenuIndexDisconnectAndQuit,
        submenu_callback,
        app);
}

static void action_start(App* app) {
    app_terminal_start_session(app);

    if(app->esp_at != NULL) {
        esp_at_free(app->esp_at);
        app->esp_at = NULL;
    }

    app->ble_initialized = false;
    app->ble_connected = false;

    app->esp_at =
        esp_at_alloc(pin_options[app->pin_option_index].serial_id, baud_options[app->baud_option_index]);

    if(app->esp_at == NULL) {
        app_log(app, "Could not claim that");
        app_log(app, "UART. Another app may");
        app_log(app, "be using it - close it");
        app_log(app, "and try again.");
        app_render_log(app);
        return;
    }

    esp_at_raw_capture_start(app->esp_at);
    action_check_esp32(app);
}

static App* s_settings_view_app = NULL;

#define SETTINGS_ROW_COUNT 3
#define SETTINGS_ROW_TOP   15
#define SETTINGS_ROW_STEP  15
#define SETTINGS_ROW_H     13
#define SETTINGS_BOX_X     4
#define SETTINGS_BOX_W     120

static void settings_draw_cb(Canvas* canvas, void* model) {
    UNUSED(model);
    App* app = s_settings_view_app;
    if(app == NULL) return;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Fox Chameleon");

    char pins_text[24];
    snprintf(pins_text, sizeof(pins_text), "%s", pin_options[app->pin_option_index].label);
    char baud_text[16];
    snprintf(baud_text, sizeof(baud_text), "%lu", (unsigned long)baud_options[app->baud_option_index]);

    const char* row_text[SETTINGS_ROW_COUNT] = {pins_text, baud_text, "Start"};
    bool row_has_arrows[SETTINGS_ROW_COUNT] = {true, true, false};

    canvas_set_font(canvas, FontSecondary);
    for(size_t i = 0; i < SETTINGS_ROW_COUNT; i++) {
        int32_t y = SETTINGS_ROW_TOP + (int32_t)i * SETTINGS_ROW_STEP;
        bool selected = (i == app->settings_selected);

        if(selected) {
            canvas_draw_rbox(canvas, SETTINGS_BOX_X, y, SETTINGS_BOX_W, SETTINGS_ROW_H, 3);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, SETTINGS_BOX_X, y, SETTINGS_BOX_W, SETTINGS_ROW_H, 3);
        }

        int32_t text_y = y + SETTINGS_ROW_H / 2;
        canvas_draw_str_aligned(canvas, 64, text_y, AlignCenter, AlignCenter, row_text[i]);
        if(row_has_arrows[i]) {
            canvas_draw_str_aligned(
                canvas, SETTINGS_BOX_X + 6, text_y, AlignLeft, AlignCenter, "<");
            canvas_draw_str_aligned(
                canvas, SETTINGS_BOX_X + SETTINGS_BOX_W - 6, text_y, AlignRight, AlignCenter, ">");
        }

        canvas_set_color(canvas, ColorBlack);
    }
}

static bool settings_input_cb(InputEvent* event, void* context) {
    App* app = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    switch(event->key) {
    case InputKeyUp:
        app->settings_selected =
            (app->settings_selected == 0) ? (SETTINGS_ROW_COUNT - 1) : (app->settings_selected - 1);
        with_view_model(app->settings_view, uint8_t * _m, { UNUSED(_m); }, true);
        return true;
    case InputKeyDown:
        app->settings_selected = (app->settings_selected + 1) % SETTINGS_ROW_COUNT;
        with_view_model(app->settings_view, uint8_t * _m, { UNUSED(_m); }, true);
        return true;
    case InputKeyLeft:
        if(app->settings_selected == SettingsIndexPins) {
            app->pin_option_index =
                (app->pin_option_index == 0) ? (PIN_OPTION_COUNT - 1) : (app->pin_option_index - 1);
            GpioRemapSettings gpio_remap_left = {.esp32_uart_channel = (uint8_t)app->pin_option_index};
            gpio_remap_settings_save(&gpio_remap_left);
            with_view_model(app->settings_view, uint8_t * _m, { UNUSED(_m); }, true);
        } else if(app->settings_selected == SettingsIndexBaud) {
            app->baud_option_index = (app->baud_option_index == 0) ? (BAUD_OPTION_COUNT - 1) :
                                                                      (app->baud_option_index - 1);
            with_view_model(app->settings_view, uint8_t * _m, { UNUSED(_m); }, true);
        }

        return true;
    case InputKeyRight:
        if(app->settings_selected == SettingsIndexPins) {
            app->pin_option_index = (app->pin_option_index + 1) % PIN_OPTION_COUNT;
            GpioRemapSettings gpio_remap_right = {.esp32_uart_channel = (uint8_t)app->pin_option_index};
            gpio_remap_settings_save(&gpio_remap_right);
            with_view_model(app->settings_view, uint8_t * _m, { UNUSED(_m); }, true);
        } else if(app->settings_selected == SettingsIndexBaud) {
            app->baud_option_index = (app->baud_option_index + 1) % BAUD_OPTION_COUNT;
            with_view_model(app->settings_view, uint8_t * _m, { UNUSED(_m); }, true);
        }
        return true;
    case InputKeyOk:
        if(app->settings_selected == SettingsIndexStart) {
            action_start(app);
        }
        return true;
    case InputKeyBack:
        return false;
    default:
        return false;
    }
}

static bool navigation_callback(void* context) {
    App* app = context;

    if(app->current_view == FoxChameleonViewSlotMenu ||
       app->current_view == FoxChameleonViewCandidateMenu ||
       app->current_view == FoxChameleonViewConnectionMenu) {
        app->current_view = FoxChameleonViewMenu;
        view_dispatcher_switch_to_view(app->view_dispatcher, FoxChameleonViewMenu);
        return true;
    }

    if(app->current_view == FoxChameleonViewMessage ||
       app->current_view == FoxChameleonViewTerminal) {
        if(app->esp32_detected) {
            app->current_view = FoxChameleonViewMenu;
            view_dispatcher_switch_to_view(app->view_dispatcher, FoxChameleonViewMenu);
        } else {
            app->current_view = FoxChameleonViewSettings;
            view_dispatcher_switch_to_view(app->view_dispatcher, FoxChameleonViewSettings);
        }
        return true;
    }

    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

static bool app_probe_default_uart(App* app, size_t pin_index) {
    app->esp_at =
        esp_at_alloc(pin_options[pin_index].serial_id, baud_options[BAUD_OPTION_DEFAULT_INDEX]);
    if(app->esp_at == NULL) return false;

    esp_at_send(app->esp_at, "AT");
    bool ok = app_expect_ok(app, 1500);

    if(!ok) {
        esp_at_free(app->esp_at);
        app->esp_at = NULL;
        return false;
    }

    app->pin_option_index = pin_index;
    app->baud_option_index = BAUD_OPTION_DEFAULT_INDEX;

    GpioRemapSettings gpio_remap = {.esp32_uart_channel = (uint8_t)pin_index};
    gpio_remap_settings_save(&gpio_remap);

    return true;
}

static App* app_alloc(void) {
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));

    app->chameleon_mac = furi_string_alloc();
    app->gatt_service_uuid = furi_string_alloc();
    app->gatt_write_char_uuid = furi_string_alloc();
    app->gatt_notify_char_uuid = furi_string_alloc();
    app->log = furi_string_alloc();
    app->terminal_log_path = furi_string_alloc();
    app->baud_option_index = BAUD_OPTION_DEFAULT_INDEX;

    GpioRemapSettings gpio_remap;
    gpio_remap_settings_load(&gpio_remap);
    if(gpio_remap.esp32_uart_channel < PIN_OPTION_COUNT) {
        app->pin_option_index = gpio_remap.esp32_uart_channel;
    }

    app_load_config(app);
    app_ensure_config_defaults(app);

    app_save_config(app);

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, navigation_callback);

    app->settings_view = view_alloc();
    view_set_draw_callback(app->settings_view, settings_draw_cb);
    view_set_input_callback(app->settings_view, settings_input_cb);
    view_set_context(app->settings_view, app);
    view_allocate_model(app->settings_view, ViewModelTypeLocking, sizeof(uint8_t));
    s_settings_view_app = app;

    app->submenu = submenu_alloc();
    render_main_menu(app);

    app->connection_submenu = submenu_alloc();
    submenu_set_header(app->connection_submenu, "Connection");
    submenu_add_item(
        app->connection_submenu,
        "Connect to C.U",
        ConnectionMenuIndexConnectToCU,
        connection_submenu_callback,
        app);
    submenu_add_item(
        app->connection_submenu,
        "Search for C.U",
        ConnectionMenuIndexSearchForCU,
        connection_submenu_callback,
        app);
    submenu_add_item(
        app->connection_submenu,
        "Disconnect BLE",
        ConnectionMenuIndexDisconnectBLE,
        connection_submenu_callback,
        app);

    app->slot_submenu = submenu_alloc();
    submenu_set_header(app->slot_submenu, "Select slot");
    char slot_label[8];
    for(uint32_t i = 0; i < 8; i++) {
        snprintf(slot_label, sizeof(slot_label), "Slot %lu", (unsigned long)i);
        submenu_add_item(app->slot_submenu, slot_label, i, slot_submenu_callback, app);
    }

    app->candidate_view = view_alloc();
    view_set_draw_callback(app->candidate_view, candidate_draw_cb);
    view_set_input_callback(app->candidate_view, candidate_input_cb);
    view_set_context(app->candidate_view, app);
    view_allocate_model(app->candidate_view, ViewModelTypeLocking, sizeof(uint8_t));
    s_candidate_view_app = app;

    app->terminal_view = view_alloc();
    view_set_draw_callback(app->terminal_view, terminal_draw_cb);
    view_set_input_callback(app->terminal_view, terminal_input_cb);
    view_set_context(app->terminal_view, app);
    view_allocate_model(app->terminal_view, ViewModelTypeLocking, sizeof(uint8_t));
    s_terminal_view_app = app;

    app->widget = widget_alloc();

    view_dispatcher_add_view(app->view_dispatcher, FoxChameleonViewSettings, app->settings_view);
    view_dispatcher_add_view(
        app->view_dispatcher, FoxChameleonViewMenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, FoxChameleonViewMessage, widget_get_view(app->widget));
    view_dispatcher_add_view(
        app->view_dispatcher,
        FoxChameleonViewSlotMenu,
        submenu_get_view(app->slot_submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, FoxChameleonViewCandidateMenu, app->candidate_view);
    view_dispatcher_add_view(
        app->view_dispatcher,
        FoxChameleonViewConnectionMenu,
        submenu_get_view(app->connection_submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, FoxChameleonViewTerminal, app->terminal_view);

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    if(app_probe_default_uart(app, 0) || app_probe_default_uart(app, 1)) {
        esp_at_raw_capture_start(app->esp_at);
        app_terminal_start_session(app);
        app_log(app, "ESP32 auto-detected on");
        app_log(
            app,
            "%s @ %lu",
            pin_options[app->pin_option_index].label,
            (unsigned long)baud_options[app->baud_option_index]);
        app->esp32_detected = true;
        app->current_view = FoxChameleonViewMenu;
        view_dispatcher_switch_to_view(app->view_dispatcher, FoxChameleonViewMenu);
    } else {
        app->current_view = FoxChameleonViewSettings;
        view_dispatcher_switch_to_view(app->view_dispatcher, FoxChameleonViewSettings);
    }

    return app;
}

static void app_free(App* app) {
    if(app->esp_at != NULL) {
        if(app->ble_connected) {
            esp_at_send(app->esp_at, "BLEDISC");
        }
        esp_at_free(app->esp_at);
    }

    view_dispatcher_remove_view(app->view_dispatcher, FoxChameleonViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, FoxChameleonViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, FoxChameleonViewMessage);
    view_dispatcher_remove_view(app->view_dispatcher, FoxChameleonViewSlotMenu);
    view_dispatcher_remove_view(app->view_dispatcher, FoxChameleonViewCandidateMenu);
    view_dispatcher_remove_view(app->view_dispatcher, FoxChameleonViewConnectionMenu);
    view_dispatcher_remove_view(app->view_dispatcher, FoxChameleonViewTerminal);

    view_free(app->settings_view);
    s_settings_view_app = NULL;
    submenu_free(app->submenu);
    submenu_free(app->slot_submenu);
    submenu_free(app->connection_submenu);
    view_free(app->candidate_view);
    s_candidate_view_app = NULL;
    view_free(app->terminal_view);
    s_terminal_view_app = NULL;
    widget_free(app->widget);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);

    furi_string_free(app->chameleon_mac);
    furi_string_free(app->gatt_service_uuid);
    furi_string_free(app->gatt_write_char_uuid);
    furi_string_free(app->gatt_notify_char_uuid);
    furi_string_free(app->log);
    furi_string_free(app->terminal_log_path);
    free(app);
}

int32_t fox_chameleon_main(void* p) {
    UNUSED(p);
    App* app = app_alloc();
    view_dispatcher_run(app->view_dispatcher);
    app_free(app);
    return 0;
}
