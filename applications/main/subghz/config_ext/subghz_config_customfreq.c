

#include "../helpers/subghz_config_scene.h"
#include "../helpers/subghz_custom_event.h"
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include <stdlib.h>
#include <string.h>

#define SUBGHZ_SETTING_USER_PATH  EXT_PATH("subghz/assets/setting_user")
#define SUBGHZ_SETTING_FILE_TYPE_ "Flipper SubGhz Setting File"
#define SUBGHZ_SETTING_FILE_VER_  1

#define CUSTOM_FREQ_MAX_ENTRIES 32

enum {
    CustomFreqIdxAddStatic = 0,
    CustomFreqIdxAddHopper,
    CustomFreqIdxToggleStandard,
    CustomFreqIdxLockedInfo,
    CustomFreqIdxRemoveBase,
};

typedef struct {
    uint32_t freq[CUSTOM_FREQ_MAX_ENTRIES];
    size_t count;
} CustomFreqList;

static const SubGhzConfigHostApi* g_api = NULL;

static bool cf_has_advanced(FlipperFormat* fff) {
    return flipper_format_key_exist(fff, "Custom_preset_name") ||
           flipper_format_key_exist(fff, "Hopping_Preset");
}

static bool cf_read(
    Storage* storage,
    bool* add_standard,
    uint32_t* default_frequency,
    bool* has_default_frequency,
    bool* locked,
    CustomFreqList* statics,
    CustomFreqList* hoppers) {
    FlipperFormat* fff = flipper_format_file_alloc(storage);
    FuriString* temp_str = furi_string_alloc();
    uint32_t temp_ver;
    bool file_exists = false;

    *add_standard = true;
    *has_default_frequency = false;
    *locked = false;
    statics->count = 0;
    hoppers->count = 0;

    do {
        if(!flipper_format_file_open_existing(fff, SUBGHZ_SETTING_USER_PATH)) break;
        file_exists = true;
        if(!flipper_format_read_header(fff, temp_str, &temp_ver)) break;
        if(strcmp(furi_string_get_cstr(temp_str), SUBGHZ_SETTING_FILE_TYPE_) ||
           temp_ver != SUBGHZ_SETTING_FILE_VER_) {
            break;
        }

        *locked = cf_has_advanced(fff);

        flipper_format_rewind(fff);
        flipper_format_read_bool(fff, "Add_standard_frequencies", add_standard, 1);

        flipper_format_rewind(fff);
        *has_default_frequency =
            flipper_format_read_uint32(fff, "Default_frequency", default_frequency, 1);

        flipper_format_rewind(fff);
        uint32_t val;
        while(statics->count < CUSTOM_FREQ_MAX_ENTRIES &&
              flipper_format_read_uint32(fff, "Frequency", &val, 1)) {
            statics->freq[statics->count++] = val;
        }

        flipper_format_rewind(fff);
        while(hoppers->count < CUSTOM_FREQ_MAX_ENTRIES &&
              flipper_format_read_uint32(fff, "Hopper_frequency", &val, 1)) {
            hoppers->freq[hoppers->count++] = val;
        }
    } while(false);

    furi_string_free(temp_str);
    flipper_format_free(fff);
    return file_exists;
}

static bool cf_write(
    Storage* storage,
    bool add_standard,
    uint32_t default_frequency,
    bool has_default_frequency,
    CustomFreqList* statics,
    CustomFreqList* hoppers) {
    FlipperFormat* fff = flipper_format_file_alloc(storage);
    bool result = false;

    do {
        if(!flipper_format_file_open_always(fff, SUBGHZ_SETTING_USER_PATH)) break;
        if(!flipper_format_write_header_cstr(
               fff, SUBGHZ_SETTING_FILE_TYPE_, SUBGHZ_SETTING_FILE_VER_))
            break;
        if(!flipper_format_write_bool(fff, "Add_standard_frequencies", &add_standard, 1)) break;
        if(has_default_frequency) {
            if(!flipper_format_write_uint32(fff, "Default_frequency", &default_frequency, 1))
                break;
        }
        bool ok = true;
        for(size_t i = 0; i < statics->count && ok; i++) {
            ok = flipper_format_write_uint32(fff, "Frequency", &statics->freq[i], 1);
        }
        if(!ok) break;
        for(size_t i = 0; i < hoppers->count && ok; i++) {
            ok = flipper_format_write_uint32(fff, "Hopper_frequency", &hoppers->freq[i], 1);
        }
        if(!ok) break;
        result = true;
    } while(false);

    flipper_format_free(fff);
    return result;
}

static bool cf_append(Storage* storage, const char* key, uint32_t value) {
    FlipperFormat* fff = flipper_format_file_alloc(storage);
    bool result = false;

    if(!storage_file_exists(storage, SUBGHZ_SETTING_USER_PATH)) {
        if(flipper_format_file_open_always(fff, SUBGHZ_SETTING_USER_PATH)) {
            flipper_format_write_header_cstr(
                fff, SUBGHZ_SETTING_FILE_TYPE_, SUBGHZ_SETTING_FILE_VER_);
            bool add_standard = true;
            flipper_format_write_bool(fff, "Add_standard_frequencies", &add_standard, 1);
            flipper_format_file_close(fff);
        }
    }

    if(flipper_format_file_open_append(fff, SUBGHZ_SETTING_USER_PATH)) {
        result = flipper_format_write_uint32(fff, key, &value, 1);
        flipper_format_file_close(fff);
    }

    flipper_format_free(fff);
    return result;
}

static void cf_submenu_callback(void* context, uint32_t index) {
    UNUSED(context);
    view_dispatcher_send_custom_event(g_api->get_view_dispatcher(g_api->ctx), index);
}

static void cf_rebuild(void) {
    Submenu* submenu = g_api->get_submenu(g_api->ctx);
    submenu_reset(submenu);
    submenu_set_header(submenu, "Custom Frequencies");

    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool add_standard, has_default, locked;
    uint32_t default_frequency;
    CustomFreqList statics, hoppers;
    cf_read(storage, &add_standard, &default_frequency, &has_default, &locked, &statics, &hoppers);
    furi_record_close(RECORD_STORAGE);

    submenu_add_item(
        submenu, "Add Static Frequency", CustomFreqIdxAddStatic, cf_submenu_callback, NULL);
    submenu_add_item(
        submenu, "Add Hopper Frequency", CustomFreqIdxAddHopper, cf_submenu_callback, NULL);

    if(locked) {
        submenu_add_item(
            submenu,
            "(Custom presets in file - edit via SD)",
            CustomFreqIdxLockedInfo,
            cf_submenu_callback,
            NULL);
    } else {
        submenu_add_item(
            submenu,
            add_standard ? "Standard Freqs: On" : "Standard Freqs: Off",
            CustomFreqIdxToggleStandard,
            cf_submenu_callback,
            NULL);

        char label[40];
        for(size_t i = 0; i < statics.count; i++) {
            snprintf(
                label,
                sizeof(label),
                "Remove %lu.%03lu MHz (S)",
                statics.freq[i] / 1000000,
                (statics.freq[i] / 1000) % 1000);
            submenu_add_item(
                submenu, label, CustomFreqIdxRemoveBase + i, cf_submenu_callback, NULL);
        }
        for(size_t i = 0; i < hoppers.count; i++) {
            snprintf(
                label,
                sizeof(label),
                "Remove %lu.%03lu MHz (H)",
                hoppers.freq[i] / 1000000,
                (hoppers.freq[i] / 1000) % 1000);
            submenu_add_item(
                submenu,
                label,
                CustomFreqIdxRemoveBase + statics.count + i,
                cf_submenu_callback,
                NULL);
        }
    }
}

static void cf_text_input_callback(void* context) {
    UNUSED(context);
    view_dispatcher_send_custom_event(
        g_api->get_view_dispatcher(g_api->ctx), SubGhzCustomEventSceneCustomFreqAdd);
}

static void customfreq_set_host_api(const SubGhzConfigHostApi* api) {
    g_api = api;
}

static void customfreq_on_enter(void) {
    cf_rebuild();
    g_api->switch_to_view(g_api->ctx, SubGhzConfigViewSubmenu);
}

static bool customfreq_on_event(SceneManagerEvent event) {
    if(event.type == SceneManagerEventTypeCustom) {
        uint32_t idx = event.event;

        if(idx == CustomFreqIdxAddStatic || idx == CustomFreqIdxAddHopper) {
            g_api->set_scene_state(g_api->ctx, idx == CustomFreqIdxAddHopper);
            char* name_tmp = g_api->get_file_name_tmp(g_api->ctx);
            name_tmp[0] = '\0';
            TextInput* text_input = g_api->get_text_input(g_api->ctx);
            text_input_set_header_text(text_input, "Freq. in MHz (e.g. 433.920)");
            text_input_set_result_callback(
                text_input, cf_text_input_callback, NULL, name_tmp, 16, true);
            g_api->switch_to_view(g_api->ctx, SubGhzConfigViewTextInput);
            return true;
        } else if(idx == SubGhzCustomEventSceneCustomFreqAdd) {
            float mhz = strtof(g_api->get_file_name_tmp(g_api->ctx), NULL);
            uint32_t hz = (uint32_t)(mhz * 1000000.0f + 0.5f);
            bool is_hopper = g_api->get_scene_state(g_api->ctx);

            if(hz > 0 && g_api->freq_is_valid(g_api->ctx, hz)) {
                Storage* storage = furi_record_open(RECORD_STORAGE);
                cf_append(storage, is_hopper ? "Hopper_frequency" : "Frequency", hz);
                furi_record_close(RECORD_STORAGE);
                g_api->set_error_str(
                    g_api->ctx, "Frequency added.\nRestart Sub-GHz to apply.");
            } else {
                g_api->set_error_str(g_api->ctx, "Invalid or unsupported\nfrequency.");
            }
            g_api->nav(g_api->ctx, SubGhzConfigNavNext, SubGhzConfigSceneShowErrorSub);
            return true;
        } else if(idx == CustomFreqIdxToggleStandard) {
            Storage* storage = furi_record_open(RECORD_STORAGE);
            bool add_standard, has_default, locked;
            uint32_t default_frequency;
            CustomFreqList statics, hoppers;
            cf_read(
                storage, &add_standard, &default_frequency, &has_default, &locked, &statics,
                &hoppers);
            if(!locked) {
                cf_write(
                    storage, !add_standard, default_frequency, has_default, &statics, &hoppers);
            }
            furi_record_close(RECORD_STORAGE);
            cf_rebuild();
            return true;
        } else if(idx == CustomFreqIdxLockedInfo) {
            return true;
        } else if(idx >= CustomFreqIdxRemoveBase) {
            Storage* storage = furi_record_open(RECORD_STORAGE);
            bool add_standard, has_default, locked;
            uint32_t default_frequency;
            CustomFreqList statics, hoppers;
            cf_read(
                storage, &add_standard, &default_frequency, &has_default, &locked, &statics,
                &hoppers);
            if(!locked) {
                size_t remove_idx = idx - CustomFreqIdxRemoveBase;
                if(remove_idx < statics.count) {
                    for(size_t i = remove_idx; i + 1 < statics.count; i++) {
                        statics.freq[i] = statics.freq[i + 1];
                    }
                    statics.count--;
                } else {
                    remove_idx -= statics.count;
                    if(remove_idx < hoppers.count) {
                        for(size_t i = remove_idx; i + 1 < hoppers.count; i++) {
                            hoppers.freq[i] = hoppers.freq[i + 1];
                        }
                        hoppers.count--;
                    }
                }
                cf_write(
                    storage, add_standard, default_frequency, has_default, &statics, &hoppers);
            }
            furi_record_close(RECORD_STORAGE);
            cf_rebuild();
            return true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        g_api->nav(g_api->ctx, SubGhzConfigNavPrevious, SubGhzConfigSceneNone);
        return true;
    }

    return false;
}

static void customfreq_on_exit(void) {
    submenu_reset(g_api->get_submenu(g_api->ctx));
    text_input_reset(g_api->get_text_input(g_api->ctx));
}

static const SubGhzConfigScenePlugin customfreq_plugin = {
    .set_host_api = customfreq_set_host_api,
    .on_enter = customfreq_on_enter,
    .on_event = customfreq_on_event,
    .on_exit = customfreq_on_exit,
};

static const FlipperAppPluginDescriptor customfreq_plugin_descriptor = {
    .appid = SUBGHZ_CONFIG_SCENE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_CONFIG_SCENE_PLUGIN_API_VERSION,
    .entry_point = &customfreq_plugin,
};

const FlipperAppPluginDescriptor* subghz_config_customfreq_ep(void) {
    return &customfreq_plugin_descriptor;
}
