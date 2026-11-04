#pragma once

/* FoxFW2.0 config-externalization contract (2026-10-31, API v2).
 * The Host-API is the ONLY way a config .fal reaches SubGhz state. The resident
 * loader (subghz_config_scene_loader.c) is the sole place that dereferences the
 * SubGhz struct (and the only place that touches furi_hal radio ops), exposing
 * everything as index/value-oriented calls so config FALs stay pure UI and need
 * NO subghz_i.h and NO furi_hal headers.
 * To extend: APPEND fields at the END and bump the API version. Never reorder or
 * remove fields (older FALs reference by offset). */

#include <furi.h>
#include <gui/scene_manager.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/variable_item_list.h>
#include <lib/subghz/subghz_setting.h>
#include <lib/flipper_application/flipper_application.h>
#include "../subghz_last_settings.h"

#define SUBGHZ_CONFIG_SCENE_PLUGIN_APP_ID      "subghz_config_scene"
#define SUBGHZ_CONFIG_SCENE_PLUGIN_API_VERSION 3U

/* Targets the resident loader maps to real scene ids (keeps FALs off the enum). */
typedef enum {
    SubGhzConfigNavFilePrefix,
    SubGhzConfigNavCustomFreq,
} SubGhzConfigNavTarget;

typedef struct {
    void* ctx; /* opaque SubGhz*; passed back to the host functions below */

    /* --- v1 core --- */
    SubGhzSetting* (*get_setting)(void* ctx);
    VariableItemList* (*get_item_list)(void* ctx);
    SubGhzLastSettings* (*get_last_settings)(void* ctx);
    void (*switch_to_item_list_view)(void* ctx);
    void (*save_all)(void* ctx);

    /* --- v2 additions --- */
    ViewDispatcher* (*get_view_dispatcher)(void* ctx);
    void (*set_scene_state)(void* ctx, uint32_t state);
    void (*open_scene)(void* ctx, SubGhzConfigNavTarget target); /* deferred, safe */

    /* modulation filter */
    bool (*mod_filter_is_enabled)(void* ctx, size_t index);
    void (*mod_filter_set_enabled)(void* ctx, size_t index, bool enabled);

    /* radio / device settings */
    void (*set_freq_offset_hw)(void* ctx, int32_t offset_hz);
    bool (*device_external_available)(void* ctx);
    uint8_t (*device_get_index)(void* ctx);                /* 0 = Internal, 1 = External */
    uint8_t (*device_set_index)(void* ctx, uint8_t index); /* returns applied index */
    uint8_t (*get_tx_power)(void* ctx);
    void (*set_tx_power)(void* ctx, uint8_t index);
    void (*set_debug_pin_state)(void* ctx, bool on);
    bool (*get_debug_pin_state)(void* ctx);
    void (*threshold_rssi_set)(void* ctx, float value);
    float (*threshold_rssi_get)(void* ctx);
    void (*reseat_radio_device)(void* ctx);

    /* furi_hal radio ops (routed so FALs need no furi_hal headers) */
    void (*set_ext_leds_and_amp)(void* ctx, bool on);
    void (*set_dangerous_frequency)(void* ctx, bool on);
    void (*set_rolling_counter_mult)(void* ctx, int32_t mult);
    int32_t (*get_rolling_counter_mult)(void* ctx);
    bool (*is_debug_mode)(void* ctx);

    /* --- v3 additions (receiver_config) --- */
    bool (*is_read_raw_mode)(void* ctx);      /* ReadRAW scene state == Set */
    void (*clear_read_raw_state)(void* ctx);  /* set ReadRAW scene state = NoSet (on exit) */
    void (*previous_scene)(void* ctx);
    void (*lock_keyboard)(void* ctx);

    bool (*hopper_get_running)(void* ctx);
    void (*hopper_set_running)(void* ctx, bool running);
    bool (*preset_hopper_get_running)(void* ctx);
    void (*preset_hopper_set_running)(void* ctx, bool running);
    void (*preset_hopper_pause)(void* ctx);
    bool (*speaker_get_enabled)(void* ctx);
    void (*speaker_set_enabled)(void* ctx, bool enabled);

    uint32_t (*get_ignore_filter)(void* ctx);
    void (*set_ignore_filter)(void* ctx, uint32_t value);
    uint32_t (*get_filter)(void* ctx);
    void (*set_filter)(void* ctx, uint32_t value);

    uint32_t (*get_preset_frequency)(void* ctx);           /* current preset frequency */
    uint8_t (*current_preset_index)(void* ctx);            /* setting index of current preset */
    void (*current_preset_name)(void* ctx, char* out, size_t size);

    uint16_t (*predefined_count)(void* ctx);               /* also refreshes subghz->predefined_count */
    void (*predefined_get_name)(void* ctx, uint16_t index, char* out, size_t size);

    void (*apply_frequency)(void* ctx, uint32_t frequency);     /* re-apply current preset @freq; no save */
    void (*apply_preset_at_frequency)(void* ctx, uint32_t frequency); /* preset@freq only; no last_settings/default update */
    void (*apply_preset)(void* ctx, uint8_t preset_index);      /* apply preset; no save */
    bool (*apply_predefined)(void* ctx, uint16_t index, char* name_out, size_t name_size); /* returns active */
    void (*reset_to_default)(void* ctx);                   /* full reset incl save + FA sync */
    void (*sync_preset_to_fa)(void* ctx, int preset_index);
} SubGhzConfigHostApi;

typedef struct {
    void (*set_host_api)(const SubGhzConfigHostApi* api);
    void (*on_enter)(void);
    bool (*on_event)(SceneManagerEvent event);
    void (*on_exit)(void);
} SubGhzConfigScenePlugin;
