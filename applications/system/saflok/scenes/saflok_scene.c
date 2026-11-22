#include "saflok_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const saflok_on_enter_handlers[])(void*) = {
#include "saflok_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const saflok_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#include "saflok_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const saflok_on_exit_handlers[])(void* context) = {
#include "saflok_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers saflok_scene_handlers = {
    .on_enter_handlers = saflok_on_enter_handlers,
    .on_event_handlers = saflok_on_event_handlers,
    .on_exit_handlers = saflok_on_exit_handlers,
    .scene_num = SaflokSceneNum,
};
