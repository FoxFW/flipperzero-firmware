#include "../fox_freqmod_helper_i.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const fmh_scene_on_enter_handlers[])(void*) = {
#include "fmh_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const fmh_scene_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#include "fmh_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const fmh_scene_on_exit_handlers[])(void* context) = {
#include "fmh_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers fmh_scene_handlers = {
    .on_enter_handlers = fmh_scene_on_enter_handlers,
    .on_event_handlers = fmh_scene_on_event_handlers,
    .on_exit_handlers = fmh_scene_on_exit_handlers,
    .scene_num = FmhSceneNum,
};
