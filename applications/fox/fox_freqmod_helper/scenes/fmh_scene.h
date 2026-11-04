#pragma once

#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) FmhScene##id,
typedef enum {
#include "fmh_scene_config.h"
    FmhSceneNum,
} FmhScene;
#undef ADD_SCENE

extern const SceneManagerHandlers fmh_scene_handlers;

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "fmh_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event);
#include "fmh_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void* context);
#include "fmh_scene_config.h"
#undef ADD_SCENE
