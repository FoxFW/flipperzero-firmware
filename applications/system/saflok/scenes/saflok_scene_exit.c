#include <furi.h>
#include <furi/core/string.h>
#include <gui/modules/submenu.h>
#include <gui/view_dispatcher.h>
#include "../saflok_app_i.h"

#define EXIT_OPTION_SAVE    0
#define EXIT_OPTION_DISCARD 1
#define EXIT_OPTION_CANCEL  2

static void saflok_scene_exit_callback(void* context, uint32_t index) {
    furi_assert(context);
    SaflokApp* app = context;

    switch(index) {
        case EXIT_OPTION_SAVE:
            app->is_exit_flow = true;

            scene_manager_next_scene(app->scene_manager, SaflokSceneWriteToTag);
            break;

        case EXIT_OPTION_DISCARD:
            view_dispatcher_stop(app->view_dispatcher);
            break;

        case EXIT_OPTION_CANCEL:
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, SaflokSceneTopLevel);
            break;

        default:
            break;
    }
}

void saflok_scene_exit_on_enter(void* context) {
    furi_assert(context);
    SaflokApp* app = context;

    Submenu* exit_menu = app->topmenu;
    submenu_reset(exit_menu);

    submenu_set_header(exit_menu, "Unsaved Changes");
    submenu_add_item(exit_menu, "Save Changes", EXIT_OPTION_SAVE, saflok_scene_exit_callback, app);
    submenu_add_item(exit_menu, "Discard Changes", EXIT_OPTION_DISCARD, saflok_scene_exit_callback, app);
    submenu_add_item(exit_menu, "Cancel", EXIT_OPTION_CANCEL, saflok_scene_exit_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, SaflokViewTopMenu);
}

bool saflok_scene_exit_on_event(void* context, SceneManagerEvent event) {
    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_previous_scene(context, SaflokSceneTopLevel);
        return true;
    }
    return false;
}

void saflok_scene_exit_on_exit(void* context) {
    furi_assert(context);
    SaflokApp* app = context;
    submenu_reset(app->topmenu);
}
