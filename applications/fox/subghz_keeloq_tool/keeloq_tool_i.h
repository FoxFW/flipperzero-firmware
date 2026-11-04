#pragma once
#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/popup.h>
#include <gui/modules/text_input.h>
#include <gui/modules/byte_input.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>

#include "keeloq_tool_custom_event.h"
#include "helpers/keeloq_keys.h"
#include "views/keeloq_decrypt_view.h"
#include "scenes/keeloq_tool_scene.h"

typedef enum {
    KeeloqToolViewSubmenu,
    KeeloqToolViewPopup,
    KeeloqToolViewTextInput,
    KeeloqToolViewByteInput,
    KeeloqToolViewKeeloqDecrypt,
} KeeloqToolViewId;

typedef struct KeeloqTool KeeloqTool;

struct KeeloqTool {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    Submenu* submenu;
    Popup* popup;
    TextInput* text_input;
    ByteInput* byte_input;
    DialogsApp* dialogs;

    SubGhzKeeloqKeysManager* keeloq_keys_manager; /* lazy-allocated */
    SubGhzViewKeeloqDecrypt* keeloq_decrypt;

    /* lifted verbatim from subghz_i.h */
    struct {
        uint8_t key_bytes[8];
        char name[65];
        uint16_t type;
        bool is_new;
        size_t edit_index;
        uint8_t edit_step;
    } keeloq_edit;

    struct {
        uint32_t fix;
        uint32_t hop1;
        uint32_t hop2;
        uint32_t serial;
        bool sig1_loaded;
        bool sig2_loaded;
        FuriString* sig1_path;
        FuriString* sig2_path;
        uint8_t learn_type;
    } keeloq_bf2;

    FuriString* signal_path; /* current signal (.sub) read by bf2/decrypt */
    FuriString* file_path;   /* decrypt annotation save-back target */
    FuriString* error_str;

    char return_marker[24];
    bool from_subghz;
};
