#pragma once

#include <furi_hal.h>
#include <stdint.h>
#include <stdbool.h>
#include <storage/storage.h>
#include <lib/subghz/types.h>

#define SUBGHZ_LAST_SETTING_FREQUENCY_ANALYZER_TRIGGER        (-93.0f)

#define SUBGHZ_LAST_SETTING_DEFAULT_PRESET                    1
#define SUBGHZ_LAST_SETTING_DEFAULT_FREQUENCY                 433920000
#define SUBGHZ_LAST_SETTING_FREQUENCY_ANALYZER_FEEDBACK_LEVEL 2
#define SUBGHZ_LAST_SETTING_DEFAULT_PRESET_HOPPING_THRESHOLD  (-80.0f)

#define SUBGHZ_LAST_SETTING_DEFAULT_VISUALIZER_MODE          0u
#define SUBGHZ_LAST_SETTING_DEFAULT_RAW_ZOOM_LEVEL           0u
#define SUBGHZ_LAST_SETTING_DEFAULT_DUAL_INT_PRESET          1u
#define SUBGHZ_LAST_SETTING_DEFAULT_DUAL_EXT_PRESET          3u

typedef struct {
    uint32_t frequency;
    uint32_t preset_index;
    uint32_t frequency_analyzer_feedback_level;
    float frequency_analyzer_trigger;
    bool protocol_file_names;
    bool enable_hopping;
    uint32_t ignore_filter;
    uint32_t filter;
    float rssi;

    uint32_t predefined_index;
    bool delete_old_signals;
    bool auto_save;
    bool capture_unknown_raw; /* Unknown RAW +21 protocol capture on Read screen */
    float hopping_threshold;
    bool enable_preset_hopping;
    float preset_hopping_threshold;
    bool leds_and_amp;
    uint8_t  tx_power;
    uint32_t visualizer_display_mode;
    uint32_t raw_playback_zoom_level;

    uint8_t protocol_filter_data[256];
    uint8_t mod_filter_data[64];
    bool    protocol_filter_present;
    bool    mod_filter_present;
    bool    bypass_region_lock;
    char    file_prefix[12];

    int32_t  frequency_offset;

    uint32_t dual_int_frequency;
    uint32_t dual_int_preset_index;
    uint32_t dual_ext_frequency;
    uint32_t dual_ext_preset_index;
} SubGhzLastSettings;

void subghz_last_settings_set_protocol_filter(SubGhzLastSettings* s,
                                               const uint8_t* data, size_t count);
void subghz_last_settings_set_mod_filter(SubGhzLastSettings* s,
                                          const uint8_t* data, size_t count);

SubGhzLastSettings* subghz_last_settings_alloc(void);

void subghz_last_settings_free(SubGhzLastSettings* instance);

void subghz_last_settings_load(SubGhzLastSettings* instance, size_t preset_count);

bool subghz_last_settings_save(SubGhzLastSettings* instance);
