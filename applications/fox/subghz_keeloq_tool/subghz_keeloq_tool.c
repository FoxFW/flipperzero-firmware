#include "keeloq_tool_i.h"
#include <furi.h>
#include <loader/loader.h>
#include <string.h>

#define KEELOQ_TOOL_FOCUS_FILE EXT_PATH("subghz/.focus_menu")

static bool keeloq_tool_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    KeeloqTool* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool keeloq_tool_back_event_callback(void* context) {
    furi_assert(context);
    KeeloqTool* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static KeeloqTool* keeloq_tool_alloc(void) {
    KeeloqTool* app = malloc(sizeof(KeeloqTool));
    memset(app, 0, sizeof(KeeloqTool));

    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&keeloq_tool_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, keeloq_tool_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, keeloq_tool_back_event_callback);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, KeeloqToolViewSubmenu, submenu_get_view(app->submenu));
    app->popup = popup_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, KeeloqToolViewPopup, popup_get_view(app->popup));
    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, KeeloqToolViewTextInput, text_input_get_view(app->text_input));
    app->byte_input = byte_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, KeeloqToolViewByteInput, byte_input_get_view(app->byte_input));
    app->keeloq_decrypt = subghz_view_keeloq_decrypt_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        KeeloqToolViewKeeloqDecrypt,
        subghz_view_keeloq_decrypt_get_view(app->keeloq_decrypt));

    app->keeloq_keys_manager = NULL;
    app->keeloq_bf2.sig1_path = furi_string_alloc();
    app->keeloq_bf2.sig2_path = furi_string_alloc();
    app->signal_path = furi_string_alloc();
    app->file_path = furi_string_alloc();
    app->error_str = furi_string_alloc();

    view_dispatcher_attach_to_gui(
        app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    return app;
}

static void keeloq_tool_free(KeeloqTool* app) {
    view_dispatcher_remove_view(app->view_dispatcher, KeeloqToolViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, KeeloqToolViewPopup);
    view_dispatcher_remove_view(app->view_dispatcher, KeeloqToolViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, KeeloqToolViewByteInput);
    view_dispatcher_remove_view(app->view_dispatcher, KeeloqToolViewKeeloqDecrypt);

    submenu_free(app->submenu);
    popup_free(app->popup);
    text_input_free(app->text_input);
    byte_input_free(app->byte_input);
    subghz_view_keeloq_decrypt_free(app->keeloq_decrypt);
    if(app->keeloq_keys_manager) subghz_keeloq_keys_free(app->keeloq_keys_manager);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_string_free(app->keeloq_bf2.sig1_path);
    furi_string_free(app->keeloq_bf2.sig2_path);
    furi_string_free(app->signal_path);
    furi_string_free(app->file_path);
    furi_string_free(app->error_str);

    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t subghz_keeloq_tool_app(void* p) {
    KeeloqTool* app = keeloq_tool_alloc();

    /* arg: "core:<section>" or "core:<section>|<signal_path>"; no prefix => standalone.
       section in {keys, bf2}. A '|path' means a signal-coupled launch from subGHz. */
    char section[16] = {0};
    const char* arg = (p && ((const char*)p)[0]) ? (const char*)p : NULL;
    if(arg) {
        if(strncmp(arg, "core:", 5) == 0) {
            app->from_subghz = true;
            arg += 5;
        } else if(strncmp(arg, "gmenu:", 6) == 0) {
            app->from_subghz = true;
            arg += 6;
        }
        const char* bar = strchr(arg, '|');
        if(bar) {
            size_t sl = (size_t)(bar - arg);
            if(sl >= sizeof(section)) sl = sizeof(section) - 1;
            memcpy(section, arg, sl);
            section[sl] = '\0';
            furi_string_set(app->keeloq_bf2.sig1_path, bar + 1);
            furi_string_set(app->signal_path, bar + 1);
            furi_string_set(app->file_path, bar + 1);
            app->keeloq_bf2.sig1_loaded = true;
        } else {
            strncpy(section, arg, sizeof(section) - 1);
        }
        strncpy(app->return_marker, section, sizeof(app->return_marker) - 1);
    }

    if(app->from_subghz && strcmp(section, "menu:keeloq_bf") == 0) {
        scene_manager_set_scene_state(
            app->scene_manager, KeeloqToolSceneBf2, app->keeloq_bf2.sig1_loaded ? 1 : 0);
        scene_manager_next_scene(app->scene_manager, KeeloqToolSceneBf2);
    } else if(app->from_subghz && strcmp(section, "menu:keeloq_keys") == 0) {
        scene_manager_next_scene(app->scene_manager, KeeloqToolSceneKeys);
    } else {
        scene_manager_next_scene(app->scene_manager, KeeloqToolSceneStart);
    }

    view_dispatcher_run(app->view_dispatcher);

    if(app->from_subghz) {
        if(!furi_string_empty(app->signal_path)) {
            /* Signal-coupled launch (from a signal's info/saved menu): reopen that
             * signal so BACK returns to its menu, not the subGHz start grid. */
            FuriString* marker = furi_string_alloc_printf(
                "inforeturn:%s", furi_string_get_cstr(app->signal_path));
            File* f = storage_file_alloc(app->storage);
            if(storage_file_open(
                   f, EXT_PATH("subghz/.focus_file"), FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
                storage_file_write(f, furi_string_get_cstr(marker), furi_string_size(marker));
            }
            storage_file_close(f);
            storage_file_free(f);
            furi_string_free(marker);
        } else if(app->return_marker[0]) {
            File* f = storage_file_alloc(app->storage);
            if(storage_file_open(f, KEELOQ_TOOL_FOCUS_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
                storage_file_write(f, app->return_marker, strlen(app->return_marker));
            }
            storage_file_close(f);
            storage_file_free(f);
        }
        Loader* loader = furi_record_open(RECORD_LOADER);
        loader_enqueue_launch(loader, "subghz", NULL, LoaderDeferredLaunchFlagNone);
        furi_record_close(RECORD_LOADER);
    }

    keeloq_tool_free(app);
    return 0;
}
