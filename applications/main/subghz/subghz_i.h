#pragma once

#include "helpers/subghz_types.h"
#include "helpers/subghz_gen_info.h"
#include <lib/subghz/types.h>
#include "subghz.h"
#include "views/receiver.h"
#include "views/transmitter.h"
#include "views/subghz_signal_visualizer.h"
#include "subghz_protocol_filter.h"
#include "subghz_modulation_filter.h"
#include "views/subghz_view_start_grid.h"
#include "views/subghz_view_receiver_info_bar.h"
#include <gui/modules/loading.h>
#include <gui/view_holder.h>
#include "views/subghz_read_raw.h"
#include "views/subghz_psa_decrypt.h"
#include "views/subghz_seed_bf.h"
#include "views/subghz_fiat_v1_recover.h"

#include <gui/gui.h>
#include <gui/view_port.h>
#include <assets_icons.h>
#include <dialogs/dialogs.h>
#include <gui/scene_manager.h>
#include <notification/notification_messages.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/popup.h>
#include <gui/modules/text_input.h>
#include <gui/modules/byte_input.h>
#include <gui/modules/number_input.h>
#include <gui/modules/widget.h>

#include <subghz/scenes/subghz_scene.h>
#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/subghz_file_encoder_worker.h>
#include <lib/subghz/subghz_setting.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/transmitter.h>

#include "subghz_history.h"
#include "subghz_last_settings.h"

#include <gui/modules/variable_item_list.h>
#include <lib/toolbox/path.h>

#include "rpc/rpc_app.h"

#include "helpers/subghz_threshold_rssi.h"
#include "helpers/subghz_predefined.h"

#include "helpers/subghz_txrx.h"
#include "helpers/subghz_raw_match.h"

#define SUBGHZ_MAX_LEN_NAME      64
#define SUBGHZ_EXT_PRESET_NAME   true
#define SUBGHZ_RAW_THRESHOLD_MIN (-90.0f)
#define SUBGHZ_RAW_THRESHOLD_DEFAULT (-65.0f)
#define SUBGHZ_MEASURE_LOADING   false
#define SUBGHZ_FIAT_V1_KEYMETHOD_RETURN_SELECT 0xFFU

struct SubGhz {
    Gui* gui;
    NotificationApp* notifications;

    SubGhzTxRx* txrx;

    SceneManager* scene_manager;
    ViewDispatcher* view_dispatcher;

    ViewPort* blank_transition_viewport;
    ViewPort* loading_cover_viewport;
    const char* loading_cover_text;

    Submenu* submenu;
    Popup* popup;
    TextInput* text_input;
    ByteInput* byte_input;
    NumberInput* number_input;
    Widget* widget;
    DialogsApp* dialogs;
    FuriString* file_path;
    FuriString* raw_send_title;
    FuriString* decode_raw_source;
    bool loaded_file_has_raw;
    FuriString* file_path_tmp;

    bool        decoded_preview_active;
    FuriString* decoded_preview_orig_path;
    char file_name_tmp[SUBGHZ_MAX_LEN_NAME];
    SubGhzNotificationState state_notifications;

    SubGhzViewReceiver* subghz_receiver;
    SubGhzViewTransmitter* subghz_transmitter;
    VariableItemList* variable_item_list;

    SubGhzSignalVisualizer*       subghz_signal_visualizer;
    SubGhzProtocolFilter*         protocol_filter;
    SubGhzModulationFilter*        modulation_filter;
    SubGhzStartGrid*               start_grid;
    SubGhzReceiverInfoBar*        receiver_info_bar;

    Loading*                       startup_loading;
    ViewHolder*                    startup_holder;
    SubGhzReadRAW* subghz_read_raw;
    SubGhzViewPsaDecrypt* subghz_psa_decrypt;
    SubGhzViewSeedBf* subghz_seed_bf;
    SubGhzViewFiatV1Recover* subghz_fiat_v1_recover;
    bool raw_send_only;

    bool save_datetime_set;
    DateTime save_datetime;

    SubGhzLastSettings* last_settings;

    SubGhzProtocolFlag filter;
    SubGhzProtocolFlag ignore_filter;
    FuriString* error_str;
    SubGhzLock lock;

    GenInfo* gen_info;

    SubGhzFileEncoderWorker* decode_raw_file_worker_encoder;

    SubGhzThresholdRssi* threshold_rssi;
    uint16_t predefined_count;
    SubGhzRxKeyState rx_key_state;
    SubGhzHistory* history;
    SubGhzRawMatch* raw_match;
    uint8_t raw_match_hit;

    uint16_t idx_menu_chosen;
    SubGhzLoadTypeFile load_type_file;
    uint8_t tx_power;
    void* rpc_ctx;

    struct {
        uint8_t key_bytes[6];
    } fiat_v1_key_edit;
};

void subghz_blink_start(SubGhz* subghz);
void subghz_blink_stop(SubGhz* subghz);

bool subghz_tx_start(SubGhz* subghz, FlipperFormat* flipper_format);
void subghz_dialog_message_freq_error(SubGhz* subghz, bool only_rx);
void subghz_dialog_message_no_ext_module(SubGhz* subghz);

bool subghz_key_load(SubGhz* subghz, const char* file_path, bool show_dialog);
bool subghz_signal_needs_decrypt(FlipperFormat* flipper_format);

typedef enum {
    SubGhzRecoveryActionNone = 0,
    SubGhzRecoveryActionHitag,
    SubGhzRecoveryActionSeedBf,
    SubGhzRecoveryActionDecrypt,
    SubGhzRecoveryActionKeeloq,
} SubGhzRecoveryAction;

SubGhzRecoveryAction subghz_recovery_primary_action(FlipperFormat* flipper_format);

const char* subghz_recovery_action_label(SubGhzRecoveryAction action);
bool subghz_get_next_name_file(SubGhz* subghz, uint8_t max_len);
bool subghz_save_protocol_to_file(
    SubGhz* subghz,
    FlipperFormat* flipper_format,
    const char* dev_file_name);
bool subghz_save_append_raw_from_file(const char* dev_file_name, const char* src_raw);
void subghz_loading_cover_show(SubGhz* subghz, const char* text);
void subghz_loading_cover_hide(SubGhz* subghz);
void subghz_save_to_file(void* context);
bool subghz_rename_file(SubGhz* subghz);
bool subghz_file_available(SubGhz* subghz);
bool subghz_delete_file(SubGhz* subghz);
void subghz_file_name_clear(SubGhz* subghz);
bool subghz_path_is_file(FuriString* path);
SubGhzLoadTypeFile subghz_get_load_type_file(SubGhz* subghz);

void subghz_lock(SubGhz* subghz);
void subghz_unlock(SubGhz* subghz);
bool subghz_is_locked(SubGhz* subghz);

void subghz_rx_key_state_set(SubGhz* subghz, SubGhzRxKeyState state);
SubGhzRxKeyState subghz_rx_key_state_get(SubGhz* subghz);

extern const NotificationSequence subghz_sequence_rx;
extern const NotificationSequence subghz_sequence_rx_locked;
extern const NotificationSequence subghz_sequence_rx_nosound;
extern const NotificationSequence subghz_sequence_rx_locked_nosound;
void subghz_save_all(SubGhz* subghz);

void subghz_scene_receiver_add_decoded(
    SubGhz* subghz,
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    SubGhzRadioPreset* preset,
    SubGhzHistorySource source);
void subghz_scene_receiver_notify(SubGhz* subghz);
void subghz_scene_receiver_process_auto_save(SubGhz* subghz);

#define SUBGHZ_KEELOQ_TOOL_FAP_PATH "/ext/apps/Sub-GHz/subghz_keeloq_tool.fap"
void subghz_scene_start_launch_and_exit(
    SubGhz* subghz, const char* fap_path, const char* args, bool show_blank);
