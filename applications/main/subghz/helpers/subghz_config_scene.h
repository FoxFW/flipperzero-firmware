#pragma once

#include <furi.h>
#include <gui/scene_manager.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/modules/popup.h>
#include <gui/modules/text_input.h>
#include <gui/modules/byte_input.h>
#include <gui/modules/submenu.h>
#include <lib/subghz/subghz_setting.h>
#include <lib/flipper_application/flipper_application.h>
#include "../subghz_last_settings.h"

#define SUBGHZ_CONFIG_SCENE_PLUGIN_APP_ID      "subghz_config_scene"
#define SUBGHZ_CONFIG_SCENE_PLUGIN_API_VERSION 8U

typedef enum {
    SubGhzConfigNavFilePrefix,
    SubGhzConfigNavCustomFreq,
} SubGhzConfigNavTarget;

typedef enum {
    SubGhzConfigViewWidget,
    SubGhzConfigViewPopup,
    SubGhzConfigViewTextInput,
    SubGhzConfigViewByteInput,
    SubGhzConfigViewSubmenu,
} SubGhzConfigView;

typedef enum {
    SubGhzConfigNavPrevious,
    SubGhzConfigNavNext,
    SubGhzConfigNavSearchPrev,
    SubGhzConfigNavStop,
    SubGhzConfigNavPreviousOrStop,
    SubGhzConfigNavSearchPrevOrStart,
    SubGhzConfigNavSearchPrevOrPrevious,
    SubGhzConfigNavSearchPrevOrStop,
} SubGhzConfigNavKind;

typedef enum {
    SubGhzConfigRxKeyStateIDLE,
    SubGhzConfigRxKeyStateBack,
    SubGhzConfigRxKeyStateExit,
    SubGhzConfigRxKeyStateRAWLoad,
} SubGhzConfigRxKeyState;

typedef enum {
    SubGhzConfigSceneNone,
    SubGhzConfigSceneStart,
    SubGhzConfigSceneReceiver,
    SubGhzConfigSceneSaved,
    SubGhzConfigSceneSaveName,
    SubGhzConfigSceneSaveSuccess,
    SubGhzConfigSceneShowError,
    SubGhzConfigSceneShowErrorSub,
    SubGhzConfigSceneTransmitter,
    SubGhzConfigSceneDecodeRAW,
    SubGhzConfigSceneReceiverInfo,
    SubGhzConfigSceneDeleteSuccess,
    SubGhzConfigSceneReadRAW,
    SubGhzConfigSceneDeleteRAW,
} SubGhzConfigSceneId;

typedef enum {
    SubGhzConfigIconSDQuestion,
    SubGhzConfigIconDebug,
} SubGhzConfigIcon;

typedef struct {
    void* ctx;

    SubGhzSetting* (*get_setting)(void* ctx);
    VariableItemList* (*get_item_list)(void* ctx);
    SubGhzLastSettings* (*get_last_settings)(void* ctx);
    void (*switch_to_item_list_view)(void* ctx);
    void (*save_all)(void* ctx);

    ViewDispatcher* (*get_view_dispatcher)(void* ctx);
    void (*set_scene_state)(void* ctx, uint32_t state);
    void (*open_scene)(void* ctx, SubGhzConfigNavTarget target);

    bool (*mod_filter_is_enabled)(void* ctx, size_t index);
    void (*mod_filter_set_enabled)(void* ctx, size_t index, bool enabled);

    void (*set_freq_offset_hw)(void* ctx, int32_t offset_hz);
    bool (*device_external_available)(void* ctx);
    uint8_t (*device_get_index)(void* ctx);
    uint8_t (*device_set_index)(void* ctx, uint8_t index);
    uint8_t (*get_tx_power)(void* ctx);
    void (*set_tx_power)(void* ctx, uint8_t index);
    void (*set_debug_pin_state)(void* ctx, bool on);
    bool (*get_debug_pin_state)(void* ctx);
    void (*threshold_rssi_set)(void* ctx, float value);
    float (*threshold_rssi_get)(void* ctx);
    void (*reseat_radio_device)(void* ctx);

    void (*set_ext_leds_and_amp)(void* ctx, bool on);
    void (*set_dangerous_frequency)(void* ctx, bool on);
    void (*set_rolling_counter_mult)(void* ctx, int32_t mult);
    int32_t (*get_rolling_counter_mult)(void* ctx);
    bool (*is_debug_mode)(void* ctx);

    bool (*is_read_raw_mode)(void* ctx);
    void (*clear_read_raw_state)(void* ctx);
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

    uint32_t (*get_preset_frequency)(void* ctx);
    uint8_t (*current_preset_index)(void* ctx);
    void (*current_preset_name)(void* ctx, char* out, size_t size);

    uint16_t (*predefined_count)(void* ctx);
    void (*predefined_get_name)(void* ctx, uint16_t index, char* out, size_t size);

    void (*apply_frequency)(void* ctx, uint32_t frequency);
    void (*apply_preset_at_frequency)(void* ctx, uint32_t frequency);
    void (*apply_preset)(void* ctx, uint8_t preset_index);
    bool (*apply_predefined)(void* ctx, uint16_t index, char* name_out, size_t name_size);
    void (*reset_to_default)(void* ctx);
    void (*sync_preset_to_fa)(void* ctx, int preset_index);

    Widget* (*get_widget)(void* ctx);
    Popup* (*get_popup)(void* ctx);
    TextInput* (*get_text_input)(void* ctx);
    ByteInput* (*get_byte_input)(void* ctx);
    Submenu* (*get_submenu)(void* ctx);
    void (*switch_to_view)(void* ctx, SubGhzConfigView view);
    void (*widget_add_fw_icon)(void* ctx, uint8_t x, uint8_t y, SubGhzConfigIcon icon);

    uint32_t (*get_scene_state)(void* ctx);
    void (*set_scene_state_of)(void* ctx, SubGhzConfigSceneId scene, uint32_t state);
    bool (*has_previous_scene)(void* ctx, SubGhzConfigSceneId scene);

    FuriString* (*get_error_str)(void* ctx);
    void (*set_error_str)(void* ctx, const char* text);
    void (*notify_sd_error)(void* ctx);
    void (*notify_reset_rgb)(void* ctx);
    FuriString* (*get_file_path)(void* ctx);
    char* (*get_file_name_tmp)(void* ctx);

    void (*nav)(void* ctx, SubGhzConfigNavKind kind, SubGhzConfigSceneId target);

    void (*notify_set_red)(void* ctx);
    void (*get_freq_mod)(void* ctx, FuriString* freq, FuriString* mod);
    void (*get_decoder_string)(void* ctx, FuriString* out);
    FuriString* (*get_file_path_tmp)(void* ctx);
    bool (*delete_file)(void* ctx);

    SubGhzConfigRxKeyState (*get_rx_key_state)(void* ctx);
    void (*set_rx_key_state)(void* ctx, SubGhzConfigRxKeyState state);
    void (*reset_preset_am650)(void* ctx);
    void (*receiver_release)(void* ctx);
    size_t (*protocol_count)(void* ctx);
    const char* (*protocol_name)(void* ctx, size_t index);
    bool (*protocol_filter_is_enabled)(void* ctx, size_t index);
    void (*protocol_filter_set_enabled)(void* ctx, size_t index, bool enabled);
    size_t (*protocol_filter_enabled_count)(void* ctx, size_t total);
    void (*protocol_filter_save)(void* ctx);

    bool (*freq_is_valid)(void* ctx, uint32_t hz);

    bool (*file_available)(void* ctx);
    bool (*raw_edit_available)(void* ctx);
    void (*launch_raw_edit)(void* ctx);
} SubGhzConfigHostApi;

typedef struct {
    void (*set_host_api)(const SubGhzConfigHostApi* api);
    void (*on_enter)(void);
    bool (*on_event)(SceneManagerEvent event);
    void (*on_exit)(void);
} SubGhzConfigScenePlugin;
