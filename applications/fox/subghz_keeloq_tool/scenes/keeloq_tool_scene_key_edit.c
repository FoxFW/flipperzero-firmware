#include "../keeloq_tool_i.h"
#include <string.h>

#define KL_EDIT_EV_KEY_DONE  200u
#define KL_EDIT_EV_NAME_DONE 201u
#define KL_EDIT_EV_TYPE_BASE 210u

static void kl_edit_byte_input_cb(void* context) {
    KeeloqTool* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, KL_EDIT_EV_KEY_DONE);
}

static void kl_edit_text_input_cb(void* context) {
    KeeloqTool* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, KL_EDIT_EV_NAME_DONE);
}

static void kl_edit_type_submenu_cb(void* context, uint32_t index) {
    KeeloqTool* app = context;
    view_dispatcher_send_custom_event(
        app->view_dispatcher, (uint32_t)(KL_EDIT_EV_TYPE_BASE + 1 + index));
}

static const char* const kl_type_names[8] = {
    "1-Simple",
    "2-Normal",
    "3-Secure",
    "4-Magic XOR",
    "5-FAAC SLH",
    "6-Magic Ser1",
    "7-Magic Ser2",
    "8-Magic Ser3",
};

static void kl_edit_show_step(KeeloqTool* app) {
    switch(app->keeloq_edit.edit_step) {
    case 0:
        byte_input_set_header_text(app->byte_input, "Enter 64-bit key (hex)");
        byte_input_set_result_callback(
            app->byte_input,
            kl_edit_byte_input_cb,
            NULL,
            app,
            app->keeloq_edit.key_bytes,
            sizeof(app->keeloq_edit.key_bytes));
        view_dispatcher_switch_to_view(app->view_dispatcher, KeeloqToolViewByteInput);
        break;
    case 1:
        text_input_set_header_text(app->text_input, "Manufacturer name");
        text_input_set_result_callback(
            app->text_input,
            kl_edit_text_input_cb,
            app,
            app->keeloq_edit.name,
            sizeof(app->keeloq_edit.name),
            false);
        view_dispatcher_switch_to_view(app->view_dispatcher, KeeloqToolViewTextInput);
        break;
    case 2:
        submenu_reset(app->submenu);
        submenu_set_header(app->submenu, "Learning type");
        for(size_t i = 0; i < 8; i++) {
            submenu_add_item(
                app->submenu,
                kl_type_names[i],
                (uint32_t)i,
                kl_edit_type_submenu_cb,
                app);
        }
        if(app->keeloq_edit.type >= 1 && app->keeloq_edit.type <= 8) {
            submenu_set_selected_item(app->submenu, (uint32_t)(app->keeloq_edit.type - 1));
        }
        view_dispatcher_switch_to_view(app->view_dispatcher, KeeloqToolViewSubmenu);
        break;
    default:
        break;
    }
}

void keeloq_tool_scene_key_edit_on_enter(void* context) {
    KeeloqTool* app = context;

    kl_edit_show_step(app);
}

bool keeloq_tool_scene_key_edit_on_event(void* context, SceneManagerEvent event) {
    KeeloqTool* app = context;

    if(event.type != SceneManagerEventTypeCustom) {
        return false;
    }

    if(event.event == KL_EDIT_EV_KEY_DONE) {
        app->keeloq_edit.edit_step = 1;
        kl_edit_show_step(app);
        return true;
    }

    if(event.event == KL_EDIT_EV_NAME_DONE) {
        app->keeloq_edit.edit_step = 2;
        kl_edit_show_step(app);
        return true;
    }

    if(event.event > KL_EDIT_EV_TYPE_BASE && event.event <= KL_EDIT_EV_TYPE_BASE + 8) {
        app->keeloq_edit.type = (uint16_t)(event.event - KL_EDIT_EV_TYPE_BASE);

        uint64_t kval = 0;
        for(int b = 0; b < 8; b++) {
            kval = (kval << 8) | (uint64_t)app->keeloq_edit.key_bytes[b];
        }

        if(app->keeloq_edit.is_new) {
            subghz_keeloq_keys_add(
                app->keeloq_keys_manager,
                kval,
                app->keeloq_edit.type,
                app->keeloq_edit.name);
        } else {
            subghz_keeloq_keys_set(
                app->keeloq_keys_manager,
                app->keeloq_edit.edit_index,
                kval,
                app->keeloq_edit.type,
                app->keeloq_edit.name);
        }

        if(!subghz_keeloq_keys_save(app->keeloq_keys_manager)) {
            furi_string_set(app->error_str, "Cannot save\nkeystore file.");
            scene_manager_next_scene(app->scene_manager, KeeloqToolSceneError);
        } else {
            scene_manager_previous_scene(app->scene_manager);
        }
        return true;
    }

    return false;
}

void keeloq_tool_scene_key_edit_on_exit(void* context) {
    KeeloqTool* app = context;
    byte_input_set_result_callback(app->byte_input, NULL, NULL, NULL, NULL, 0);
    byte_input_set_header_text(app->byte_input, "");
    text_input_reset(app->text_input);
    submenu_reset(app->submenu);
}
