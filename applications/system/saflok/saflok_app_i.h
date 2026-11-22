#pragma once

#include "saflok_app.h"
#include "saflok_types.h"

#include <furi.h>
#include <furi_hal.h>

#include <gui/gui.h>
#include <gui/view.h>
#include <assets_icons.h>
#include "saflok_icons.h"
#include <gui/modules/variable_item_list.h>
/* saflok vendors variable_item_list (views/variable_item_list.c) with an added
 * header row; the stock module header lacks this decl. */
void variable_item_list_set_header(VariableItemList* variable_item_list, const char* header);
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <cli/cli.h>
#include <notification/notification_messages.h>

#include <gui/modules/submenu.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/popup.h>
#include <gui/modules/loading.h>
#include <gui/modules/text_input.h>
#include <gui/modules/byte_input.h>
#include <gui/modules/text_box.h>
#include <gui/modules/widget.h>
#include "views/dict_attack.h"
#include "views/detect_reader.h"
#include "views/dict_attack.h"
#include "views/hex_input.h"
#include "views/saflok_date_input.h"
#include "views/number_input.h"
#include "views/option_list.h"

#include "scenes/saflok_scene.h"
#include "helpers/saflok_detected_protocols.h"
#include "helpers/saflok_custom_event.h"
#include "helpers/mf_user_dict.h"
#include "helpers/mfkey32_logger.h"
#include "helpers/mf_classic_key_cache.h"

#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <toolbox/path.h>

#include "rpc/rpc_app.h"

#include <m-array.h>

#include <lib/nfc/nfc.h>
#include <lib/nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <lib/nfc/protocols/iso14443_3a/iso14443_3a_listener.h>

#include <nfc/nfc_poller.h>
#include <nfc/nfc_scanner.h>
#include <nfc/nfc_listener.h>

#include <nfc/nfc_device.h>
#include <nfc/helpers/nfc_data_generator.h>
#include <toolbox/keys_dict.h>

#include <gui/modules/validators.h>
#include <toolbox/path.h>
#include <toolbox/name_generator.h>

#define BASIC_ACCESS_BYTE_NUM          17
#define MF_CLASSIC_1K_TOTAL_BLOCKS_NUM 64
#define SAFLOK_NAME_SIZE               22
#define SAFLOK_TEXT_STORE_SIZE         128
#define SAFLOK_BYTE_INPUT_STORE_SIZE   10
#define SAFLOK_LOG_SIZE_MAX            (1024)
#define SAFLOK_YEAR_OFFSET             1980
#define SAFLOK_APP_FOLDER              EXT_PATH("saflok")
#define NFC_APP_FOLDER                 EXT_PATH("nfc")
#define SAFLOK_APP_EXTENSION           ".nfc"
#define SAFLOK_APP_SHADOW_EXTENSION    ".shd"
#define SAFLOK_APP_FILENAME_PREFIX     "SAFLOK"

#define SAFLOK_APP_MFKEY32_LOGS_FILE_NAME ".mfkey32.log"
#define SAFLOK_APP_MFKEY32_LOGS_FILE_PATH (SAFLOK_APP_FOLDER "/" SAFLOK_APP_MFKEY32_LOGS_FILE_NAME)

#define SAFLOK_APP_MF_CLASSIC_DICT_USER_PATH (SAFLOK_APP_FOLDER "/assets/mf_classic_dict_user.nfc")
#define SAFLOK_APP_MF_CLASSIC_DICT_USER_NESTED_PATH \
    (SAFLOK_APP_FOLDER "/assets/mf_classic_dict_user_nested.nfc")
#define SAFLOK_APP_MF_CLASSIC_DICT_SYSTEM_PATH (SAFLOK_APP_FOLDER "/assets/mf_classic_dict.nfc")
#define SAFLOK_APP_MF_CLASSIC_DICT_SYSTEM_NESTED_PATH \
    (SAFLOK_APP_FOLDER "/assets/mf_classic_dict_nested.nfc")

typedef struct AlignedTextBox AlignedTextBox;
typedef struct FieldList      FieldList;

typedef enum {
    SaflokRpcStateIdle,
    SaflokRpcStateEmulating,
} SaflokRpcState;

typedef struct {
    KeysDict* dict;
    uint8_t sectors_total;
    uint8_t sectors_read;
    uint8_t current_sector;
    uint8_t keys_found;
    size_t dict_keys_total;
    size_t dict_keys_current;
    bool is_key_attack;
    uint8_t key_attack_current_sector;
    bool is_card_present;
    MfClassicNestedPhase nested_phase;
    MfClassicPrngType prng_type;
    MfClassicBackdoor backdoor;
    uint16_t nested_target_key;
    uint16_t msb_count;
    bool enhanced_dict;
} SaflokMfClassicDictAttackContext;

typedef struct SaflokMfClassicData {
    Iso14443_3aData* iso14443_3a_data;
    MfClassicType type;
    uint32_t block_read_mask[MF_CLASSIC_READ_MASK_SIZE];
    uint64_t key_a_mask;
    uint64_t key_b_mask;
    MfClassicBlock block[MF_CLASSIC_TOTAL_BLOCKS_MAX];
} SaflokMfClassicData;

typedef struct FieldList {
    View* view;
} FieldList;

typedef struct SaflokChanges {
    bool key_level;
    bool sub_function;
    bool key_id;
    bool led_warning;
    bool key_record;
    bool pass_levels;
    bool opening_key;
    bool sequence_combo;
    bool override_deadbolt;
    bool restrictions;
    bool creation_date;
    bool expiration_date;
    bool property_number;
    bool checksum;
} SaflokChanges;

struct SaflokApp {
    DialogsApp*   dialogs;
    Storage*      storage;
    SceneManager* scene_manager;
    Gui*          gui;

    ViewDispatcher*  view_dispatcher;
    NotificationApp* notifications;
    DisplaySettings* display_settings;

    char text_store[SAFLOK_TEXT_STORE_SIZE + 1];
    FuriString* text_box_store;
    uint8_t byte_input_store[SAFLOK_BYTE_INPUT_STORE_SIZE];

    SaflokDetectedProtocols* detected_protocols;

    RpcAppSystem* rpc_ctx;
    SaflokRpcState rpc_state;

    Submenu*          submenu;
    Submenu*          topmenu;
    Submenu*          start_menu;
    Submenu*          preset_key_select_menu;
    DialogEx*         dialog_ex;
    Popup*            popup;
    Loading*          loading;
    TextInput*        text_input;
    ByteInput*        byte_input;
    HexInput*         hex_input;
    DateInput*        date_input;
    NumberInput*      number_input;
    TextBox*          text_box;
    AlignedTextBox*   aligned_text_box;
    FieldList*        field_list;
    Widget*           widget;
    DetectReader*     detect_reader;
    DictAttack*       dict_attack;

    VariableItemList*  key_level_list;
    VariableItemList*  sub_function_list;
    OptionList*        display_settings_list;
    OptionList*        pass_levels_list;

    OptionList* restrictions_list;
    void* restrictions_context;

    Nfc* nfc;
    NfcPoller* poller;
    NfcScanner* scanner;
    NfcListener* listener;

    SaflokMfClassicDictAttackContext saflok_dict_context;
    Mfkey32Logger* mfkey32_logger;
    MfUserDict* mf_user_dict;
    MfClassicKeyCache* mfc_key_cache;

    NfcDevice*  saflok_device;
    SaflokData* saflok_data;
    FuriString* file_path;
    FuriString* file_name;
    FuriTimer*  timer;

    bool     fav_timeout;
    bool     popup_visible;
    bool     edit_original;
    bool     is_exit_flow;
    bool     is_writing;
    bool     nfc_mode;
    uint32_t original_file_checksum;
    uint32_t previous_field_index;
    char*    text_input_buffer;
    uint8_t* edit_buffer;

    SaflokChanges* changed;
    SaflokMfClassicData* mf_classic_data;

    struct {
        uint8_t saved_restrictions[DAY_COUNT];
    } temp_storage;
};

typedef enum {
    SaflokViewAlignedTextBox,
    SaflokViewByteInput,
    SaflokViewDateInput,
    SaflokViewDetectReader,
    SaflokViewDialogEx,
    SaflokViewDictAttack,
    SaflokViewFieldList,
    SaflokViewHexInput,
    SaflokViewKeyLevel,
    SaflokViewSubFunction,
    SaflokViewLoading,
    SaflokViewMenu,
    SaflokViewNumberInput,
    SaflokViewDisplaySettingsList,
    SaflokViewPassLevelsList,
    SaflokViewRestrictionsList,
    SaflokViewPopup,
    SaflokViewTextBox,
    SaflokViewTextInput,
    SaflokViewStartMenu,
    SaflokViewTopMenu,
    SaflokViewPresetKeySelect,
    SaflokViewWidget,
} SaflokView;

typedef enum {
    PromptResponseSuccess,
    PromptResponseFailure,
    PromptResponseNotMfClassic,
    PromptResponseExitedFile,
    PromptResponseExitedShadow,
} PromptResponse;

typedef enum {
    SaveResponseSave,
    SaveResponseDiscard,
    SaveResponseCancel,
} SaveResponse;

int32_t saflok_task(void* p);

void saflok_text_store_set(SaflokApp* saflok, const char* text, ...);

void saflok_text_store_clear(SaflokApp* saflok);

void saflok_blink_read_start(SaflokApp* saflok);

void saflok_blink_emulate_start(SaflokApp* saflok);

void saflok_blink_detect_start(SaflokApp* saflok);

void saflok_blink_stop(SaflokApp* saflok);

void saflok_show_loading_popup(void* context, bool show);

bool saflok_has_shadow_file(SaflokApp* app);

bool saflok_save_shadow_file(SaflokApp* app);

bool saflok_delete_shadow_file(SaflokApp* app);

bool saflok_save(SaflokApp* app);

bool saflok_delete(SaflokApp* app);

bool saflok_load_from_file_select(SaflokApp* app);

bool saflok_load_file(SaflokApp* app, FuriString* path, bool show_dialog);

bool saflok_save_file(SaflokApp* app, FuriString* path);

void saflok_make_app_folder(SaflokApp* app);

void saflok_append_filename_string_when_present(SaflokApp* app, FuriString* string);

void save_changes(SaflokApp* app);

bool has_unsaved_changes(SaflokApp* app);

