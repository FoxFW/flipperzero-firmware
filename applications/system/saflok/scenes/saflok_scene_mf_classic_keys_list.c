#include "../saflok_app_i.h"

#undef TAG
#define TAG "SaflokSceneMfClassicKeysList"

#define SAFLOK_SCENE_MF_CLASSIC_KEYS_LIST_MAX (100)

void saflok_scene_mf_classic_keys_list_submenu_callback(void* context, uint32_t index) {
    SaflokApp* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void saflok_scene_mf_classic_keys_list_on_enter(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_enter");

    app->mf_user_dict = mf_user_dict_alloc(SAFLOK_SCENE_MF_CLASSIC_KEYS_LIST_MAX);

    submenu_set_header(app->submenu, "Select key to delete:");
    FuriString* temp_str = furi_string_alloc();
    for(size_t i = 0; i < mf_user_dict_get_keys_cnt(app->mf_user_dict); i++) {
        mf_user_dict_get_key_str(app->mf_user_dict, i, temp_str);
        submenu_add_item(
            app->submenu,
            furi_string_get_cstr(temp_str),
            i,
            saflok_scene_mf_classic_keys_list_submenu_callback,
            app);
    }
    furi_string_free(temp_str);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewMenu);
}

bool saflok_scene_mf_classic_keys_list_on_event(void* context, SceneManagerEvent event) {
    SaflokApp* app = context;
 
    FURI_LOG_D(TAG, "on_event");
    
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(
            app->scene_manager, SaflokSceneMfClassicKeysDelete, event.event);
        scene_manager_next_scene(app->scene_manager, SaflokSceneMfClassicKeysDelete);
    } else if(event.type == SceneManagerEventTypeBack) {
        mf_user_dict_free(app->mf_user_dict);
    }

    return consumed;
}

void saflok_scene_mf_classic_keys_list_on_exit(void* context) {
    SaflokApp* app = context;

    FURI_LOG_D(TAG, "on_exit");
    
    submenu_reset(app->submenu);
}
