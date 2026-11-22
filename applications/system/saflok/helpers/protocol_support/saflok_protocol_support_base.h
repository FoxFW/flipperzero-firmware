#pragma once

#include <core/string.h>

#include "../../saflok_app.h"
#include "../../saflok_app_i.h"

typedef void (*SaflokProtocolSupportOnEnter)(SaflokApp* app);

typedef bool (*SaflokProtocolSupportOnEvent)(SaflokApp* app, SceneManagerEvent event);

typedef struct {
    SaflokProtocolSupportOnEnter on_enter;
    SaflokProtocolSupportOnEvent on_event;
} SaflokProtocolSupportSceneBase;

typedef struct {
    const uint32_t features;

    SaflokProtocolSupportSceneBase scene_info;

    SaflokProtocolSupportSceneBase scene_more_info;

    SaflokProtocolSupportSceneBase scene_read;

    SaflokProtocolSupportSceneBase scene_read_menu;

    SaflokProtocolSupportSceneBase scene_read_success;

    SaflokProtocolSupportSceneBase scene_saved_menu;

    SaflokProtocolSupportSceneBase scene_save_name;

    SaflokProtocolSupportSceneBase scene_emulate;
} SaflokProtocolSupportBase;
