#pragma once

/* Resident side of config-externalization. Scene handlers become thin shims
 * over these three calls; this loader is the ONLY code that dereferences the
 * SubGhz struct on behalf of config FALs. scene_id is this scene's SubGhzScene*
 * enum, used to back set_scene_state. 2026-10-31. */

#include <gui/scene_manager.h>

void subghz_config_scene_enter(void* context, const char* fal_base, uint32_t scene_id);
bool subghz_config_scene_event(void* context, SceneManagerEvent event);
void subghz_config_scene_exit(void* context);

/* Free the cached config FAL (call on subghz app teardown). */
void subghz_config_scene_loader_flush(void);
