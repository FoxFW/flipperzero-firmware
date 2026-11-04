#include "subghz_predefined.h"

#include <furi_hal.h>
#include <storage/storage.h>

#define TAG "SubGhzPredefined"

#define SUBGHZ_PREDEFINED_FILE EXT_PATH("subghz/assets/predefined_db/predefined.txt")

#define PREDEFINED_FILE_HEADER       "FoxFW SubGhz Predefined"
#define PREDEFINED_FILE_VERSION      1
#define PREDEFINED_FIELD_COUNT       "Count"
#define PREDEFINED_FIELD_NAME        "Name"
#define PREDEFINED_FIELD_FREQUENCY   "Frequency"
#define PREDEFINED_FIELD_PRESET      "Preset"
#define PREDEFINED_FIELD_CUSTOM_DATA "CustomPresetData"

bool subghz_predefined_get_by_index(
    SubGhzPredefined* entry,
    uint16_t index,
    uint16_t count,
    SubGhzSetting* settings) {
    uint32_t version = 0;
    uint32_t frequency = 0;
    uint8_t* preset_data = NULL;
    uint32_t preset_data_size = 0;
    bool error = true;
    bool custom_preset = false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* preset_name = furi_string_alloc();
    FuriString* name = furi_string_alloc();
    FuriString* header = furi_string_alloc();

    switch(index) {
    case 0:
        break; /* None */
    default: {
        if(!flipper_format_file_open_existing(ff, SUBGHZ_PREDEFINED_FILE)) {
            FURI_LOG_I(TAG, "predefined.txt not found");
            break;
        } else if(!flipper_format_read_header(ff, header, &version)) {
            FURI_LOG_W(TAG, "Failed to read header");
            break;
        } else if(furi_string_cmp_str(header, PREDEFINED_FILE_HEADER) != 0) {
            FURI_LOG_W(TAG, "Invalid header");
            break;
        } else if(version != PREDEFINED_FILE_VERSION) {
            FURI_LOG_W(TAG, "Unsupported version %lu", (unsigned long)version);
            break;
        }

        char name_index[16];
        char frequency_index[16];
        char preset_index_key[16];
        char preset_data_index[24];

        snprintf(name_index, sizeof(name_index), PREDEFINED_FIELD_NAME "%u", index);
        snprintf(frequency_index, sizeof(frequency_index), PREDEFINED_FIELD_FREQUENCY "%u", index);
        snprintf(preset_index_key, sizeof(preset_index_key), PREDEFINED_FIELD_PRESET "%u", index);
        snprintf(
            preset_data_index, sizeof(preset_data_index), PREDEFINED_FIELD_CUSTOM_DATA "%u", index);

        if(!flipper_format_read_string(ff, name_index, name)) {
            FURI_LOG_E(TAG, "Failed to read %s", name_index);
            break;
        }

        if(!flipper_format_read_uint32(ff, frequency_index, &frequency, 1)) {
            FURI_LOG_W(TAG, "Failed to read frequency");
            frequency = 0;
            break;
        }

        if(!flipper_format_read_string(ff, preset_index_key, preset_name)) {
            furi_string_set_str(preset_name, "Custom");
            custom_preset = true;
            flipper_format_rewind(ff);
        } else {
            const char* preset_name_long = furi_string_get_cstr(preset_name);
            const char* preset_name_short = preset_name_long;

            if(!strcmp(preset_name_long, "FuriHalSubGhzPresetOok270Async")) {
                preset_name_short = "AM270";
            } else if(!strcmp(preset_name_long, "FuriHalSubGhzPresetOok650Async")) {
                preset_name_short = "AM650";
            } else if(!strcmp(preset_name_long, "FuriHalSubGhzPreset2FSKDev238Async")) {
                preset_name_short = "FM238";
            } else if(!strcmp(preset_name_long, "FuriHalSubGhzPreset2FSKDev12KAsync")) {
                preset_name_short = "FM12K";
            } else if(!strcmp(preset_name_long, "FuriHalSubGhzPreset2FSKDev476Async")) {
                preset_name_short = "FM476";
            } else if(!strcmp(preset_name_long, "FuriHalSubGhzPresetCustom")) {
                preset_name_short = "Custom";
                custom_preset = true;
            }

            furi_string_set_str(preset_name, preset_name_short);

            if(!custom_preset) {
                size_t preset_idx = subghz_setting_get_preset_count(settings);
                for(size_t i = 0; i < subghz_setting_get_preset_count(settings); i++) {
                    if(!strcmp(subghz_setting_get_preset_name(settings, i), preset_name_short)) {
                        preset_idx = i;
                        break;
                    }
                }
                if(preset_idx >= subghz_setting_get_preset_count(settings)) {
                    preset_name_short = "AM650";
                    for(size_t i = 0; i < subghz_setting_get_preset_count(settings); i++) {
                        if(!strcmp(
                               subghz_setting_get_preset_name(settings, i), preset_name_short)) {
                            preset_idx = i;
                            break;
                        }
                    }
                    if(preset_idx >= subghz_setting_get_preset_count(settings)) {
                        FURI_LOG_E(TAG, "Failed to get preset index!");
                        break;
                    }
                }

                uint8_t* temp_preset_data = subghz_setting_get_preset_data(settings, preset_idx);
                uint32_t temp_preset_data_size =
                    subghz_setting_get_preset_data_size(settings, preset_idx);

                if(temp_preset_data == NULL) {
                    FURI_LOG_E(TAG, "Failed to get preset data!");
                    break;
                } else {
                    preset_data = malloc(temp_preset_data_size);
                    memcpy(preset_data, temp_preset_data, temp_preset_data_size);
                    preset_data_size = temp_preset_data_size;
                    error = false;
                }
            }
        }

        if(custom_preset) {
            if(flipper_format_get_value_count(ff, preset_data_index, &preset_data_size) &&
               preset_data_size > 0) {
                if(preset_data_size >= 1024) {
                    FURI_LOG_E(
                        TAG,
                        "%s too large: %lu",
                        preset_data_index,
                        (unsigned long)preset_data_size);
                    break;
                }
                preset_data = malloc(preset_data_size);
                if(!preset_data) {
                    FURI_LOG_E(
                        TAG,
                        "Malloc failed: %s (%lu bytes)",
                        preset_data_index,
                        (unsigned long)preset_data_size);
                    break;
                }
                flipper_format_rewind(ff);
                if(!flipper_format_read_hex(ff, preset_data_index, preset_data, preset_data_size)) {
                    break;
                }
                FURI_LOG_D(TAG, "Using custom preset data");
                error = false;
            }
        }
    }
    }

    if(!error) {
        size_t length = furi_string_utf8_length(name) + 1;
        if(entry->name) free(entry->name);
        entry->name = malloc(length);
        snprintf(entry->name, length, "%s", furi_string_get_cstr(name));
        entry->index = index;

        if(!entry->preset) {
            entry->preset = malloc(sizeof(SubGhzRadioPreset));
            entry->preset->name = furi_string_alloc();
        } else if(entry->preset->data) {
            free(entry->preset->data);
            entry->preset->data = NULL;
        }
        furi_string_set(entry->preset->name, preset_name);
        entry->preset->data = preset_data;
        entry->preset->data_size = preset_data_size;
        entry->preset->frequency = frequency;
    } else {
        const char* tmp = count ? "Select Predefined" : "No Entries in Database";
        if(entry->name) free(entry->name);
        size_t length = strlen(tmp) + 1;
        entry->name = malloc(length);
        snprintf(entry->name, length, "%s", tmp);
        entry->index = 0;

        if(entry->preset) {
            if(entry->preset->data) {
                free(entry->preset->data);
                entry->preset->data = NULL;
            }
            entry->preset->data_size = 0;
            if(entry->preset->name) {
                furi_string_free(entry->preset->name);
                entry->preset->name = NULL;
            }
            free(entry->preset);
            entry->preset = NULL;
        }
        if(preset_data) free(preset_data);
    }

    flipper_format_free(ff);
    furi_string_free(header);
    furi_string_free(name);
    furi_string_free(preset_name);
    furi_record_close(RECORD_STORAGE);
    return (!error);
}

uint16_t subghz_predefined_get_count(void) {
    uint32_t version = 0;
    uint32_t count = 0;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();

    if(!flipper_format_file_open_existing(ff, SUBGHZ_PREDEFINED_FILE)) {
        FURI_LOG_W(TAG, "Failed to open " SUBGHZ_PREDEFINED_FILE);
    } else if(!flipper_format_read_header(ff, header, &version)) {
        FURI_LOG_W(TAG, "Failed to read header.");
    } else if(
        version != PREDEFINED_FILE_VERSION ||
        furi_string_cmp_str(header, PREDEFINED_FILE_HEADER) != 0) {
        FURI_LOG_W(TAG, "Version/header invalid.");
    } else if(!flipper_format_read_uint32(ff, PREDEFINED_FIELD_COUNT, &count, 1)) {
        FURI_LOG_W(TAG, "Failed to read " PREDEFINED_FIELD_COUNT ".");
    }

    furi_string_free(header);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return (uint16_t)count;
}

void subghz_predefined_free(SubGhzPredefined* entry) {
    if(!entry) return;
    if(entry->name) {
        free(entry->name);
        entry->name = NULL;
    }
    if(entry->preset) {
        if(entry->preset->data) {
            free(entry->preset->data);
            entry->preset->data = NULL;
        }
        if(entry->preset->name) {
            furi_string_free(entry->preset->name);
            entry->preset->name = NULL;
        }
        free(entry->preset);
        entry->preset = NULL;
    }
    entry->index = 0;
}
