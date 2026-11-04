#pragma once
#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) KeeloqToolScene##id,
typedef enum {
#include "keeloq_tool_scene_config.h"
    KeeloqToolSceneNum,
} KeeloqToolScene;
#undef ADD_SCENE

extern const SceneManagerHandlers keeloq_tool_scene_handlers;

#define ADD_SCENE(prefix, name, id)                       \
    void prefix##_scene_##name##_on_enter(void*);         \
    bool prefix##_scene_##name##_on_event(                \
        void* context, SceneManagerEvent event);          \
    void prefix##_scene_##name##_on_exit(void* context);
#include "keeloq_tool_scene_config.h"
#undef ADD_SCENE
