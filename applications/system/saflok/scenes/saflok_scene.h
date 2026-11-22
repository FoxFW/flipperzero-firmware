#pragma once

#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) SaflokScene##id,
typedef enum {
#include "saflok_scene_config.h"
    SaflokSceneNum,
} SaflokScene;
#undef ADD_SCENE

extern const SceneManagerHandlers saflok_scene_handlers;

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "saflok_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event);
#include "saflok_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void* context);
#include "saflok_scene_config.h"
#undef ADD_SCENE
