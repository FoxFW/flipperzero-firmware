#include "system_settings.h"
#include <furi_hal.h>
#include <loader/loader.h>
#include <lib/toolbox/value_index.h>
#include <locale/locale.h>
#include <flipper_format/flipper_format.h>
#include <power/power_service/power.h>
#include <applications/services/namechanger/namechanger.h>

const char* const log_level_text[] = {
    "Default",
    "None",
    "Error",
    "Warning",
    "Info",
    "Debug",
    "Trace",
};

const uint32_t log_level_value[] = {
    FuriLogLevelDefault,
    FuriLogLevelNone,
    FuriLogLevelError,
    FuriLogLevelWarn,
    FuriLogLevelInfo,
    FuriLogLevelDebug,
    FuriLogLevelTrace,
};

static void log_level_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, log_level_text[index]);
    furi_hal_rtc_set_log_level(log_level_value[index]);
}

const char* const log_device_text[] = {
    "USART",
    "LPUART",
    "None",
};

const uint32_t log_device_value[] = {
    FuriHalRtcLogDeviceUsart,
    FuriHalRtcLogDeviceLpuart,
    FuriHalRtcLogDeviceNone};

static void log_device_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, log_device_text[index]);
    furi_hal_rtc_set_log_device(log_device_value[index]);
}

const char* const log_baud_rate_text[] = {
    "9600",
    "38400",
    "57600",
    "115200",
    "230400",
    "460800",
    "921600",
    "1843200",
};

const uint32_t log_baud_rate_value[] = {
    FuriHalRtcLogBaudRate9600,
    FuriHalRtcLogBaudRate38400,
    FuriHalRtcLogBaudRate57600,
    FuriHalRtcLogBaudRate115200,
    FuriHalRtcLogBaudRate230400,
    FuriHalRtcLogBaudRate460800,
    FuriHalRtcLogBaudRate921600,
    FuriHalRtcLogBaudRate1843200,
};

static void log_baud_rate_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, log_baud_rate_text[index]);
    furi_hal_rtc_set_log_baud_rate(log_baud_rate_value[index]);
}

#ifdef FURI_DEBUG
const char* const debug_text[] = {
    "OFF",
    "ON",
};

static void debug_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, debug_text[index]);
    if(index) {
        furi_hal_rtc_set_flag(FuriHalRtcFlagDebug);
    } else {
        furi_hal_rtc_reset_flag(FuriHalRtcFlagDebug);
    }
}
#endif

const char* const heap_trace_mode_text[] = {
    "None",
    "Main",
#ifdef FURI_DEBUG
    "Tree",
    "All",
#endif
};

const uint32_t heap_trace_mode_value[] = {
    FuriHalRtcHeapTrackModeNone,
    FuriHalRtcHeapTrackModeMain,
#ifdef FURI_DEBUG
    FuriHalRtcHeapTrackModeTree,
    FuriHalRtcHeapTrackModeAll,
#endif
};

static void heap_trace_mode_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, heap_trace_mode_text[index]);
    furi_hal_rtc_set_heap_track_mode(heap_trace_mode_value[index]);
}

const char* const measurement_units_text[] = {
    "Metric",
    "Imperial",
};

const uint32_t measurement_units_value[] = {
    LocaleMeasurementUnitsMetric,
    LocaleMeasurementUnitsImperial,
};

static void measurement_units_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, measurement_units_text[index]);
    locale_set_measurement_unit(measurement_units_value[index]);
}

const char* const time_format_text[] = {
    "24h",
    "12h",
};

const uint32_t time_format_value[] = {
    LocaleTimeFormat24h,
    LocaleTimeFormat12h,
};

static void time_format_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, time_format_text[index]);
    locale_set_time_format(time_format_value[index]);
}

const char* const date_format_text[] = {
    "D/M/Y",
    "M/D/Y",
    "Y/M/D",
};

const uint32_t date_format_value[] = {
    LocaleDateFormatDMY,
    LocaleDateFormatMDY,
    LocaleDateFormatYMD,
};

static void date_format_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, date_format_text[index]);
    locale_set_date_format(date_format_value[index]);
}

const char* const hand_mode[] = {
    "Righty",
    "Lefty",
};

static void hand_orient_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, hand_mode[index]);
    if(index) {
        furi_hal_rtc_set_flag(FuriHalRtcFlagHandOrient);
    } else {
        furi_hal_rtc_reset_flag(FuriHalRtcFlagHandOrient);
    }
}

const char* const sleep_method[] = {
    "Default",
    "Legacy",
};

static void sleep_method_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, sleep_method[index]);
    if(index) {
        furi_hal_rtc_set_flag(FuriHalRtcFlagLegacySleep);
    } else {
        furi_hal_rtc_reset_flag(FuriHalRtcFlagLegacySleep);
    }
}

const char* const filename_scheme[] = {
    "Default",
    "Detailed",
};

static void filename_scheme_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, filename_scheme[index]);
    if(index) {
        furi_hal_rtc_set_flag(FuriHalRtcFlagDetailedFilename);
    } else {
        furi_hal_rtc_reset_flag(FuriHalRtcFlagDetailedFilename);
    }
}

#define DEVICE_NAME_ITEM_INDEX 11

static bool system_settings_device_name_validator(
    const char* text,
    FuriString* error,
    void* context) {
    UNUSED(context);
    for(; *text; ++text) {
        const char c = *text;
        if((c < '0' || c > '9') && (c < 'A' || c > 'Z') && (c < 'a' || c > 'z')) {
            furi_string_printf(error, "Letters and\nnumbers only!");
            return false;
        }
    }
    return true;
}

static void system_settings_device_name_callback(void* context) {
    SystemSettings* app = context;

    FlipperFormat* file = flipper_format_file_alloc(app->storage);
    bool saved = false;
    do {
        if(app->device_name[0] == '\0') {

            storage_simply_remove(app->storage, NAMECHANGER_PATH);
            saved = true;
            break;
        }
        if(!flipper_format_file_open_always(file, NAMECHANGER_PATH)) break;
        if(!flipper_format_write_header_cstr(file, NAMECHANGER_HEADER, NAMECHANGER_VERSION)) break;
        if(!flipper_format_write_string_cstr(file, "Name", app->device_name)) break;
        saved = true;
    } while(false);
    flipper_format_free(file);

    if(saved && app->device_name[0] != '\0') {
        furi_hal_version_set_name(app->device_name);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, SystemSettingsViewVarItemList);
}

static void system_settings_enter_callback(void* context, uint32_t index) {
    SystemSettings* app = context;
    if(index == DEVICE_NAME_ITEM_INDEX) {
        text_input_reset(app->text_input);
        text_input_set_header_text(app->text_input, "Device Name (empty=reset)");
        text_input_set_validator(
            app->text_input, system_settings_device_name_validator, NULL);
        text_input_set_minimum_length(app->text_input, 0);
        text_input_set_result_callback(
            app->text_input,
            system_settings_device_name_callback,
            app,
            app->device_name,
            FURI_HAL_VERSION_ARRAY_NAME_LENGTH,
            false);
        view_dispatcher_switch_to_view(app->view_dispatcher, SystemSettingsViewTextInput);
    }
}

static uint32_t system_settings_text_input_back(void* context) {
    UNUSED(context);
    return SystemSettingsViewVarItemList;
}

static uint32_t system_settings_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

SystemSettings* system_settings_alloc(void) {
    SystemSettings* app = malloc(sizeof(SystemSettings));

    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    VariableItem* item;
    uint8_t value_index;
    app->var_item_list = variable_item_list_alloc();

    item = variable_item_list_add(
        app->var_item_list, "Hand Orient", COUNT_OF(hand_mode), hand_orient_changed, app);
    value_index = furi_hal_rtc_is_flag_set(FuriHalRtcFlagHandOrient) ? 1 : 0;
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, hand_mode[value_index]);

    item = variable_item_list_add(
        app->var_item_list,
        "Units",
        COUNT_OF(measurement_units_text),
        measurement_units_changed,
        app);
    value_index = value_index_uint32(
        locale_get_measurement_unit(), measurement_units_value, COUNT_OF(measurement_units_value));
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, measurement_units_text[value_index]);

    item = variable_item_list_add(
        app->var_item_list, "Time Format", COUNT_OF(time_format_text), time_format_changed, app);
    value_index = value_index_uint32(
        locale_get_time_format(), time_format_value, COUNT_OF(time_format_value));
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, time_format_text[value_index]);

    item = variable_item_list_add(
        app->var_item_list, "Date Format", COUNT_OF(date_format_text), date_format_changed, app);
    value_index = value_index_uint32(
        locale_get_date_format(), date_format_value, COUNT_OF(date_format_value));
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, date_format_text[value_index]);

    item = variable_item_list_add(
        app->var_item_list, "Log Level", COUNT_OF(log_level_text), log_level_changed, app);
    value_index = value_index_uint32(
        furi_hal_rtc_get_log_level(), log_level_value, COUNT_OF(log_level_text));
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, log_level_text[value_index]);

    item = variable_item_list_add(
        app->var_item_list, "Log Device", COUNT_OF(log_device_text), log_device_changed, app);
    value_index = value_index_uint32(
        furi_hal_rtc_get_log_device(), log_device_value, COUNT_OF(log_device_text));
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, log_device_text[value_index]);

    item = variable_item_list_add(
        app->var_item_list,
        "Log Baud Rate",
        COUNT_OF(log_baud_rate_text),
        log_baud_rate_changed,
        app);
    value_index = value_index_uint32(
        furi_hal_rtc_get_log_baud_rate(), log_baud_rate_value, COUNT_OF(log_baud_rate_text));
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, log_baud_rate_text[value_index]);

#ifdef FURI_DEBUG
    item = variable_item_list_add(
        app->var_item_list, "Debug", COUNT_OF(debug_text), debug_changed, app);
    value_index = furi_hal_rtc_is_flag_set(FuriHalRtcFlagDebug) ? 1 : 0;
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, debug_text[value_index]);
#endif

    item = variable_item_list_add(
        app->var_item_list,
        "Heap Trace",
        COUNT_OF(heap_trace_mode_text),
        heap_trace_mode_changed,
        app);
    value_index = value_index_uint32(
        furi_hal_rtc_get_heap_track_mode(), heap_trace_mode_value, COUNT_OF(heap_trace_mode_text));
    furi_hal_rtc_set_heap_track_mode(heap_trace_mode_value[value_index]);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, heap_trace_mode_text[value_index]);

    item = variable_item_list_add(
        app->var_item_list, "Sleep Method", COUNT_OF(sleep_method), sleep_method_changed, app);
    value_index = furi_hal_rtc_is_flag_set(FuriHalRtcFlagLegacySleep) ? 1 : 0;
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, sleep_method[value_index]);

    item = variable_item_list_add(
        app->var_item_list, "File Naming", COUNT_OF(filename_scheme), filename_scheme_changed, app);
    value_index = furi_hal_rtc_is_flag_set(FuriHalRtcFlagDetailedFilename) ? 1 : 0;
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, filename_scheme[value_index]);

    const char* current_name = furi_hal_version_get_name_ptr();
    strlcpy(
        app->device_name,
        current_name ? current_name : "",
        FURI_HAL_VERSION_ARRAY_NAME_LENGTH);
    item = variable_item_list_add(app->var_item_list, "Device Name", 0, NULL, app);
    variable_item_set_current_value_text(
        item, app->device_name[0] != '\0' ? app->device_name : "<default>");

    variable_item_list_set_enter_callback(
        app->var_item_list, system_settings_enter_callback, app);

    view_set_previous_callback(
        variable_item_list_get_view(app->var_item_list), system_settings_exit);
    view_dispatcher_add_view(
        app->view_dispatcher,
        SystemSettingsViewVarItemList,
        variable_item_list_get_view(app->var_item_list));

    app->text_input = text_input_alloc();
    view_set_previous_callback(
        text_input_get_view(app->text_input), system_settings_text_input_back);
    view_dispatcher_add_view(
        app->view_dispatcher,
        SystemSettingsViewTextInput,
        text_input_get_view(app->text_input));

    view_dispatcher_switch_to_view(app->view_dispatcher, SystemSettingsViewVarItemList);

    return app;
}

void system_settings_free(SystemSettings* app) {
    furi_assert(app);

    view_dispatcher_remove_view(app->view_dispatcher, SystemSettingsViewTextInput);
    text_input_free(app->text_input);

    view_dispatcher_remove_view(app->view_dispatcher, SystemSettingsViewVarItemList);
    variable_item_list_free(app->var_item_list);

    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t system_settings_app(void* p) {
    UNUSED(p);
    SystemSettings* app = system_settings_alloc();
    view_dispatcher_run(app->view_dispatcher);
    system_settings_free(app);
    return 0;
}
