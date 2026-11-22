#include "../saflok_app_i.h"
#include "../saflok_device.h"

void saflok_scene_file_prompt_on_enter(void* context) {
    SaflokApp* app = context;

    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, SAFLOK_APP_EXTENSION, &I_Nfc_10px);
    browser_options.base_path = SAFLOK_APP_FOLDER;
    browser_options.hide_dot_files = true;

    bool success = dialog_file_browser_show(
        app->dialogs, app->file_path, app->file_path, &browser_options);

    if(!success) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    popup_reset(app->popup);
    popup_set_header(app->popup, "", 64, 32, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewPopup);

    PromptResponse result = saflok_device_load_from_path(app, app->file_path);
    if(result == PromptResponseSuccess) {
        scene_manager_next_scene(app->scene_manager, SaflokSceneTopLevel);
    } else if(result == PromptResponseNotMfClassic) {
        scene_manager_next_scene(app->scene_manager, SaflokSceneInvalidFile);
    } else {
        scene_manager_previous_scene(app->scene_manager);
    }
}

bool saflok_scene_file_prompt_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void saflok_scene_file_prompt_on_exit(void* context) {
    SaflokApp* app = context;
    popup_reset(app->popup);
}
