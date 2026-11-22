#include "../pocsag_pager_app_i.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const pocsag_pager_scene_on_enter_handlers[])(void*) = {
#include "pocsag_pager_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const pocsag_pager_scene_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#include "pocsag_pager_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const pocsag_pager_scene_on_exit_handlers[])(void* context) = {
#include "pocsag_pager_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers pocsag_pager_scene_handlers = {
    .on_enter_handlers = pocsag_pager_scene_on_enter_handlers,
    .on_event_handlers = pocsag_pager_scene_on_event_handlers,
    .on_exit_handlers = pocsag_pager_scene_on_exit_handlers,
    .scene_num = POCSAGPagerSceneNum,
};
