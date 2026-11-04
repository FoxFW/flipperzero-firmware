#include "fox_ch_settings.h"
// Import Companion Settings
//
// External System app. Desktop launches it when the companion app (over BLE/USB
// storage RPC) drops a request flag. It reads the pending settings file and
// applies each section LIVE using the same subsystem calls as foxr_companion.c
// (no reboot except a Device Name change), or writes the current settings out
// for the companion app to read. Shows a spinner (~0.5s) then "Done" (~0.5s),
// clears the flag, and exits. Being EXTERNAL, it costs no internal flash.

#include <furi.h>
#include <furi_hal.h>
#include <input/input.h>
#include <input/input_settings.h>
#include <storage/storage.h>
#include <notification/notification_app.h>
#include <locale/locale.h>
#include <gui/modules/fox_theme.h>
#include <power/power_service/power.h>
#include <bt/bt_service/bt.h>
#include <bt/bt_service/bt_settings_api_i.h>
#include <desktop/desktop.h>
#include <cli/cli_settings.h>
#include <flipper_format/flipper_format.h>
#include <applications/services/namechanger/namechanger.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define FOXI_DIR      EXT_PATH("apps_data/fox_companion")
#define FOXI_REQUEST  EXT_PATH("apps_data/fox_companion/request.flag")  // "READ" or "WRITE"
#define FOXI_PENDING  EXT_PATH("apps_data/fox_companion/pending.txt")   // sections for WRITE
#define FOXI_CURRENT  EXT_PATH("apps_data/fox_companion/current.txt")   // 42-field line for READ

// ---- value tables (copied from foxr_companion.c) ----
#define FOXR_NIGHT_SHIFT_COUNT 7
static const float foxr_night_shift_value[FOXR_NIGHT_SHIFT_COUNT] =
    {1.0f, 0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f};
#define FOXR_VIBRO_TOUCH_LEVEL_COUNT 10
static const uint32_t foxr_vibro_touch_level_value[FOXR_VIBRO_TOUCH_LEVEL_COUNT] =
    {0, 13, 16, 19, 21, 24, 27, 30, 33, 36};
#define FOXR_VIBRO_TOUCH_TRIGGER_COUNT 3
static const uint32_t foxr_vibro_touch_trigger_value[FOXR_VIBRO_TOUCH_TRIGGER_COUNT] = {
    (1u << InputTypePress),
    (1u << InputTypeRelease),
    (1u << InputTypePress) | (1u << InputTypeRelease)};
#define FOXR_AUTO_POWEROFF_COUNT 8
static const uint32_t foxr_auto_poweroff_value[FOXR_AUTO_POWEROFF_COUNT] =
    {0, 300000, 600000, 900000, 1800000, 2700000, 3600000, 5400000};
#define FOXR_LIMIT_CHARGE_COUNT 6
static const uint32_t foxr_limit_charge_value[FOXR_LIMIT_CHARGE_COUNT] = {0, 90, 85, 80, 75, 70};

static uint8_t foxi_index_of_u32(uint32_t val, const uint32_t* arr, uint8_t count) {
    for(uint8_t i = 0; i < count; i++)
        if(arr[i] == val) return i;
    return 0;
}
static uint8_t foxi_index_of_float(float val, const float* arr, uint8_t count) {
    for(uint8_t i = 0; i < count; i++)
        if(arr[i] == val) return i;
    return 0;
}

// ---- apply functions (adapted 1:1 from foxr_companion.c, minus the UART reply) ----

static void apply_notif(const char* p) {
    int contrast;
    unsigned backlight_pct, led_pct, volume_pct, delay_ms, vibro, inversion;
    int n = sscanf(
        p, "%d,%u,%u,%u,%u,%u,%u", &contrast, &backlight_pct, &led_pct, &volume_pct, &delay_ms,
        &vibro, &inversion);
    if(n != 7 || contrast < -8 || contrast > 8 || backlight_pct > 100 || led_pct > 100 ||
       volume_pct > 100 || delay_ms > 3600000u || vibro > 1 || inversion > 1)
        return;
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification->settings.contrast = (int8_t)contrast;
    notification->settings.display_brightness = (float)backlight_pct / 100.0f;
    notification->settings.led_brightness = (float)led_pct / 100.0f;
    notification->settings.speaker_volume = (float)volume_pct / 100.0f;
    notification->settings.display_off_delay_ms = delay_ms;
    notification->settings.vibro_on = (vibro != 0);
    notification->settings.lcd_inversion = (inversion != 0);
    notification_message(notification, &sequence_display_backlight_force_on);
    notification_message_save_settings(notification);
    furi_record_close(RECORD_NOTIFICATION);
}

static void apply_locale(const char* p) {
    unsigned time_fmt, date_fmt, units, hand;
    int n = sscanf(p, "%u,%u,%u,%u", &time_fmt, &date_fmt, &units, &hand);
    if(n != 4 || time_fmt > 1 || date_fmt > 2 || units > 1 || hand > 1) return;
    locale_set_time_format((LocaleTimeFormat)time_fmt);
    locale_set_date_format((LocaleDateFormat)date_fmt);
    locale_set_measurement_unit((LocaleMeasurementUnits)units);
    if(hand)
        furi_hal_rtc_set_flag(FuriHalRtcFlagHandOrient);
    else
        furi_hal_rtc_reset_flag(FuriHalRtcFlagHandOrient);
}

static void apply_theme(const char* p) {
    unsigned style;
    if(sscanf(p, "%u", &style) != 1 || style > 4) return;
    fox_theme_set_style((uint8_t)style);
}

static void apply_nightshift(const char* p) {
    unsigned mode, start_min, end_min;
    int n = sscanf(p, "%u,%u,%u", &mode, &start_min, &end_min);
    if(n != 3 || mode >= FOXR_NIGHT_SHIFT_COUNT || start_min > 1439 || end_min > 1439) return;
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification->settings.night_shift = foxr_night_shift_value[mode];
    notification->settings.night_shift_start = start_min;
    notification->settings.night_shift_end = end_min;
    notification_message_save_settings(notification);
    furi_record_close(RECORD_NOTIFICATION);
}

static void apply_rgb(const char* p) {
    unsigned installed, white_mode, led1, led2, led3, effect, speed_ms, step, saturation, wave_wide;
    int n = sscanf(
        p, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u", &installed, &white_mode, &led1, &led2, &led3, &effect,
        &speed_ms, &step, &saturation, &wave_wide);
    uint8_t color_count = rgb_backlight_get_color_count();
    if(n != 10 || installed > 1 || white_mode > 1 || led1 >= color_count || led2 >= color_count ||
       led3 >= color_count || effect > 2 || speed_ms < 100 || speed_ms > 1000 || step < 1 ||
       step > 3 || saturation < 1 || saturation > 255 ||
       (wave_wide != 30 && wave_wide != 40 && wave_wide != 50))
        return;
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification->settings.rgb.rgb_backlight_installed = (installed != 0);
    notification->settings.rgb.white_backlight_mode = (white_mode != 0);
    notification->settings.rgb.led_2_color_index = (uint8_t)led1;
    notification->settings.rgb.led_1_color_index = (uint8_t)led2;
    notification->settings.rgb.led_0_color_index = (uint8_t)led3;
    notification->settings.rgb.rainbow_mode = effect;
    notification->settings.rgb.rainbow_speed_ms = speed_ms;
    notification->settings.rgb.rainbow_step = step;
    notification->settings.rgb.rainbow_saturation = (uint8_t)saturation;
    notification->settings.rgb.rainbow_wide = wave_wide;
    set_rgb_backlight_installed_variable(installed != 0);
    set_rgb_backlight_white_mode_variable(white_mode != 0);
    if(!installed) {
        rgb_backlight_set_led_static_color(2, 0);
        rgb_backlight_set_led_static_color(1, 0);
        rgb_backlight_set_led_static_color(0, 0);
        SK6805_update();
        rainbow_timer_stop(notification);
    } else if(effect > 0) {
        rainbow_timer_starter(notification);
    } else {
        rainbow_timer_stop(notification);
        rgb_backlight_set_led_static_color(2, (uint8_t)led1);
        rgb_backlight_set_led_static_color(1, (uint8_t)led2);
        rgb_backlight_set_led_static_color(0, (uint8_t)led3);
        rgb_backlight_update(
            notification->settings.display_brightness * notification->current_night_shift);
    }
    notification_message_save_settings(notification);
    furi_record_close(RECORD_NOTIFICATION);
}

static void apply_system(const char* p) {
    unsigned sleep_method, file_naming;
    int consumed = 0;
    int n = sscanf(p, "%u,%u%n", &sleep_method, &file_naming, &consumed);
    if(n != 2 || sleep_method > 1 || file_naming > 1 || p[consumed] != ',') return;
    const char* name = p + consumed + 1;
    size_t name_len = strlen(name);
    if(name_len >= FURI_HAL_VERSION_ARRAY_NAME_LENGTH) return;
    for(size_t i = 0; i < name_len; i++) {
        char ch = name[i];
        bool ok = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
        if(!ok) return;
    }
    if(sleep_method)
        furi_hal_rtc_set_flag(FuriHalRtcFlagLegacySleep);
    else
        furi_hal_rtc_reset_flag(FuriHalRtcFlagLegacySleep);
    if(file_naming)
        furi_hal_rtc_set_flag(FuriHalRtcFlagDetailedFilename);
    else
        furi_hal_rtc_reset_flag(FuriHalRtcFlagDetailedFilename);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(name_len == 0) {
        storage_simply_remove(storage, NAMECHANGER_PATH);
    } else {
        FlipperFormat* file = flipper_format_file_alloc(storage);
        do {
            if(!flipper_format_file_open_always(file, NAMECHANGER_PATH)) break;
            if(!flipper_format_write_header_cstr(file, NAMECHANGER_HEADER, NAMECHANGER_VERSION)) break;
            if(!flipper_format_write_string_cstr(file, "Name", name)) break;
        } while(false);
        flipper_format_free(file);
    }
    furi_record_close(RECORD_STORAGE);
}

static void apply_input(const char* p) {
    unsigned level_idx, trigger_idx;
    int n = sscanf(p, "%u,%u", &level_idx, &trigger_idx);
    if(n != 2 || level_idx >= FOXR_VIBRO_TOUCH_LEVEL_COUNT ||
       trigger_idx >= FOXR_VIBRO_TOUCH_TRIGGER_COUNT)
        return;
    InputSettings* live = furi_record_open(RECORD_INPUT_SETTINGS);
    live->vibro_touch_level = foxr_vibro_touch_level_value[level_idx];
    live->vibro_touch_trigger_mask = foxr_vibro_touch_trigger_value[trigger_idx];
    InputSettings snapshot = *live;
    furi_record_close(RECORD_INPUT_SETTINGS);
    input_settings_save(&snapshot);
}

static void apply_bt(const char* p) {
    unsigned enabled;
    if(sscanf(p, "%u", &enabled) != 1 || enabled > 1) return;
    Bt* bt = furi_record_open(RECORD_BT);
    BtSettings settings;
    bt_get_settings(bt, &settings);
    settings.enabled = (enabled != 0);
    bt_set_settings(bt, &settings);
    furi_record_close(RECORD_BT);
}

static void apply_power(const char* p) {
    unsigned poweroff_idx, charge_idx;
    int n = sscanf(p, "%u,%u", &poweroff_idx, &charge_idx);
    if(n != 2 || poweroff_idx >= FOXR_AUTO_POWEROFF_COUNT || charge_idx >= FOXR_LIMIT_CHARGE_COUNT)
        return;
    PowerSettings settings;
    settings.auto_poweroff_delay_ms = foxr_auto_poweroff_value[poweroff_idx];
    settings.charge_supress_percent = (uint8_t)foxr_limit_charge_value[charge_idx];
    power_settings_save(&settings);
    Power* power = furi_record_open(RECORD_POWER);
    power_api_set_settings(power, &settings);
    furi_record_close(RECORD_POWER);
}

static void apply_desktop(const char* p) {
    unsigned battery_view, show_clock, midnight_fmt, wifi_icon_hidden, statusbar_icons, shell_color_idx;
    int n = sscanf(
        p, "%u,%u,%u,%u,%u,%u", &battery_view, &show_clock, &midnight_fmt, &wifi_icon_hidden,
        &statusbar_icons, &shell_color_idx);
    if(n != 6 || battery_view > 5 || show_clock > 1 || midnight_fmt > 1 || wifi_icon_hidden > 1 ||
       statusbar_icons > 1 || shell_color_idx > 7)
        return;
    Desktop* desktop = furi_record_open(RECORD_DESKTOP);
    DesktopSettings settings;
    desktop_api_get_settings(desktop, &settings);
    settings.displayBatteryPercentage = (uint8_t)battery_view;
    settings.display_clock = (uint8_t)show_clock;
    settings.clock_midnight_zero = (uint8_t)midnight_fmt;
    settings.wifi_icon_hidden = (uint8_t)wifi_icon_hidden;
    settings.statusbar_show_icons = (uint8_t)statusbar_icons;
    desktop_settings_save(&settings);
    desktop_api_set_settings(desktop, &settings);
    furi_record_close(RECORD_DESKTOP);
    CliSettings cli_settings;
    cli_settings.shell_color_index = (uint8_t)shell_color_idx;
    cli_settings_save(&cli_settings);
}

static void apply_alarm(const char* p) {
    unsigned keep_backlight, beep, vibrate;
    int n = sscanf(p, "%u,%u,%u", &keep_backlight, &beep, &vibrate);
    if(n != 3 || keep_backlight > 1 || beep > 1 || vibrate > 1) return;
    Desktop* desktop = furi_record_open(RECORD_DESKTOP);
    DesktopSettings settings;
    desktop_api_get_settings(desktop, &settings);
    settings.alarm_keep_backlight_all_night = (uint8_t)keep_backlight;
    settings.alarm_beep_enabled = (uint8_t)beep;
    settings.alarm_vibrate_enabled = (uint8_t)vibrate;
    desktop_settings_save(&settings);
    desktop_api_set_settings(desktop, &settings);
    furi_record_close(RECORD_DESKTOP);
}

// ---- READ: write current settings (42-field line, foxr_handle_settings_get order) ----
static void write_current(Storage* storage) {
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    int contrast = notification->settings.contrast;
    unsigned backlight_pct = (unsigned)(notification->settings.display_brightness * 100.0f + 0.5f);
    unsigned led_pct = (unsigned)(notification->settings.led_brightness * 100.0f + 0.5f);
    unsigned volume_pct = (unsigned)(notification->settings.speaker_volume * 100.0f + 0.5f);
    unsigned delay_ms = notification->settings.display_off_delay_ms;
    unsigned vibro = notification->settings.vibro_on ? 1u : 0u;
    unsigned inversion = notification->settings.lcd_inversion ? 1u : 0u;
    unsigned ns_idx = foxi_index_of_float(
        notification->settings.night_shift, foxr_night_shift_value, FOXR_NIGHT_SHIFT_COUNT);
    unsigned ns_start = notification->settings.night_shift_start;
    unsigned ns_end = notification->settings.night_shift_end;
    unsigned rgb_installed = notification->settings.rgb.rgb_backlight_installed ? 1u : 0u;
    unsigned rgb_white = notification->settings.rgb.white_backlight_mode ? 1u : 0u;
    unsigned rgb_led1 = notification->settings.rgb.led_2_color_index;
    unsigned rgb_led2 = notification->settings.rgb.led_1_color_index;
    unsigned rgb_led3 = notification->settings.rgb.led_0_color_index;
    unsigned rgb_effect = notification->settings.rgb.rainbow_mode;
    unsigned rgb_speed = notification->settings.rgb.rainbow_speed_ms;
    unsigned rgb_step = notification->settings.rgb.rainbow_step;
    unsigned rgb_sat = notification->settings.rgb.rainbow_saturation;
    unsigned rgb_wave = notification->settings.rgb.rainbow_wide;
    furi_record_close(RECORD_NOTIFICATION);

    unsigned time_fmt = (unsigned)locale_get_time_format();
    unsigned date_fmt = (unsigned)locale_get_date_format();
    unsigned units = (unsigned)locale_get_measurement_unit();
    unsigned hand = furi_hal_rtc_is_flag_set(FuriHalRtcFlagHandOrient) ? 1u : 0u;
    unsigned theme = fox_theme_get_style();
    unsigned sleep_method = furi_hal_rtc_is_flag_set(FuriHalRtcFlagLegacySleep) ? 1u : 0u;
    unsigned file_naming = furi_hal_rtc_is_flag_set(FuriHalRtcFlagDetailedFilename) ? 1u : 0u;

    InputSettings* input_settings = furi_record_open(RECORD_INPUT_SETTINGS);
    unsigned vibro_level_idx = foxi_index_of_u32(
        input_settings->vibro_touch_level, foxr_vibro_touch_level_value,
        FOXR_VIBRO_TOUCH_LEVEL_COUNT);
    unsigned vibro_trigger_idx = foxi_index_of_u32(
        input_settings->vibro_touch_trigger_mask, foxr_vibro_touch_trigger_value,
        FOXR_VIBRO_TOUCH_TRIGGER_COUNT);
    furi_record_close(RECORD_INPUT_SETTINGS);

    Bt* bt = furi_record_open(RECORD_BT);
    BtSettings bt_settings;
    bt_get_settings(bt, &bt_settings);
    unsigned bt_enabled = bt_settings.enabled ? 1u : 0u;
    furi_record_close(RECORD_BT);

    Power* power = furi_record_open(RECORD_POWER);
    PowerSettings power_settings;
    power_api_get_settings(power, &power_settings);
    unsigned auto_poweroff_idx = foxi_index_of_u32(
        power_settings.auto_poweroff_delay_ms, foxr_auto_poweroff_value, FOXR_AUTO_POWEROFF_COUNT);
    unsigned limit_charge_idx = foxi_index_of_u32(
        power_settings.charge_supress_percent, foxr_limit_charge_value, FOXR_LIMIT_CHARGE_COUNT);
    furi_record_close(RECORD_POWER);

    Desktop* desktop = furi_record_open(RECORD_DESKTOP);
    DesktopSettings ds;
    desktop_api_get_settings(desktop, &ds);
    furi_record_close(RECORD_DESKTOP);

    CliSettings cli_settings;
    cli_settings_load(&cli_settings);
    unsigned shell_color_idx = cli_settings.shell_color_index;

    const char* device_name = furi_hal_version_get_name_ptr();

    FuriString* s = furi_string_alloc();
    furi_string_printf(
        s,
        "%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%s",
        contrast, backlight_pct, led_pct, volume_pct, delay_ms, vibro, inversion, time_fmt,
        date_fmt, units, hand, theme, ns_idx, ns_start, ns_end, rgb_installed, rgb_white, rgb_led1,
        rgb_led2, rgb_led3, rgb_effect, rgb_speed, rgb_step, rgb_sat, rgb_wave, sleep_method,
        file_naming, vibro_level_idx, vibro_trigger_idx, bt_enabled, auto_poweroff_idx,
        limit_charge_idx, ds.displayBatteryPercentage, ds.display_clock, ds.clock_midnight_zero,
        ds.wifi_icon_hidden, ds.statusbar_show_icons, shell_color_idx,
        ds.alarm_keep_backlight_all_night, ds.alarm_beep_enabled, ds.alarm_vibrate_enabled,
        device_name ? device_name : "");

    File* f = storage_file_alloc(storage);
    if(storage_file_open(f, FOXI_CURRENT, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(f, furi_string_get_cstr(s), furi_string_size(s));
        storage_file_close(f);
    }
    storage_file_free(f);
    furi_string_free(s);
}

static void dispatch_section(const char* tag, const char* payload) {
    if(strcmp(tag, "NOTIF") == 0)
        apply_notif(payload);
    else if(strcmp(tag, "LOCALE") == 0)
        apply_locale(payload);
    else if(strcmp(tag, "THEME") == 0)
        apply_theme(payload);
    else if(strcmp(tag, "NIGHTSHIFT") == 0)
        apply_nightshift(payload);
    else if(strcmp(tag, "RGB") == 0)
        apply_rgb(payload);
    else if(strcmp(tag, "SYSTEM") == 0)
        apply_system(payload);
    else if(strcmp(tag, "INPUT") == 0)
        apply_input(payload);
    else if(strcmp(tag, "BT") == 0)
        apply_bt(payload);
    else if(strcmp(tag, "POWER") == 0)
        apply_power(payload);
    else if(strcmp(tag, "DESKTOP") == 0)
        apply_desktop(payload);
    else if(strcmp(tag, "ALARM") == 0)
        apply_alarm(payload);
}

static void process_request(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);

    char mode[16] = {0};
    File* rf = storage_file_alloc(storage);
    if(storage_file_open(rf, FOXI_REQUEST, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint16_t rd = storage_file_read(rf, mode, sizeof(mode) - 1);
        mode[rd] = '\0';
        storage_file_close(rf);
    }
    storage_file_free(rf);
    for(int i = (int)strlen(mode) - 1; i >= 0 && (mode[i] == '\n' || mode[i] == '\r' || mode[i] == ' ');
        i--)
        mode[i] = '\0';

    if(strncmp(mode, "READ", 4) == 0) {
        write_current(storage);
    } else if(strncmp(mode, "WRITE", 5) == 0) {
        File* pf = storage_file_alloc(storage);
        if(storage_file_open(pf, FOXI_PENDING, FSAM_READ, FSOM_OPEN_EXISTING)) {
            uint64_t sz = storage_file_size(pf);
            if(sz > 0 && sz < 4096) {
                char* buf = malloc((size_t)sz + 1);
                if(buf) {
                    uint16_t rd = storage_file_read(pf, buf, (uint16_t)sz);
                    buf[rd] = '\0';
                    // Manual line split (strtok_r is disabled for external apps)
                    char* cur = buf;
                    while(*cur) {
                        char* line = cur;
                        char* nl = strchr(cur, '\n');
                        if(nl) {
                            *nl = '\0';
                            cur = nl + 1;
                        } else {
                            cur = cur + strlen(cur);
                        }
                        size_t ll = strlen(line);
                        if(ll && line[ll - 1] == '\r') line[ll - 1] = '\0';
                        if(line[0] != '\0') {
                            char* sp = strchr(line, ' ');
                            const char* payload = "";
                            if(sp) {
                                *sp = '\0';
                                payload = sp + 1;
                            }
                            dispatch_section(line, payload);
                        }
                    }
                    free(buf);
                }
            }
            storage_file_close(pf);
        }
        storage_file_free(pf);
        storage_simply_remove(storage, FOXI_PENDING);
    }

    storage_simply_remove(storage, FOXI_REQUEST);
    furi_record_close(RECORD_STORAGE);
}

void fox_settings_process(void) {
    process_request();
}
