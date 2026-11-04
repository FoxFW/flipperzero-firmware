#include "keeloq_tool_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const keeloq_tool_scene_on_enter_handlers[])(void*) = {
#include "keeloq_tool_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const keeloq_tool_scene_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#include "keeloq_tool_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const keeloq_tool_scene_on_exit_handlers[])(void* context) = {
#include "keeloq_tool_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers keeloq_tool_scene_handlers = {
    .on_enter_handlers = keeloq_tool_scene_on_enter_handlers,
    .on_event_handlers = keeloq_tool_scene_on_event_handlers,
    .on_exit_handlers = keeloq_tool_scene_on_exit_handlers,
    .scene_num = KeeloqToolSceneNum,
};
