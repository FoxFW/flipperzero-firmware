#pragma once

#include <gui/scene_manager.h>

void subghz_config_scene_enter(void* context, const char* fal_base, uint32_t scene_id);
bool subghz_config_scene_event(void* context, SceneManagerEvent event);
void subghz_config_scene_exit(void* context);

void subghz_config_scene_loader_flush(void);
