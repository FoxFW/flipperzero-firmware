#include "../subghz_i.h"
#include "../helpers/subghz_config_scene_loader.h"

/* FoxFW2.0 config-externalization Stage 2 (2026-10-31): body now lives in
 * config_ext/subghz_config_modulation.c, loaded on demand as
 * subghz_config_modulation.fal. Thin shims over the resident loader. */

void subghz_scene_modulation_list_on_enter(void* context) {
    subghz_config_scene_enter(context, "subghz_config_modulation", SubGhzSceneModulationList);
}

bool subghz_scene_modulation_list_on_event(void* context, SceneManagerEvent event) {
    return subghz_config_scene_event(context, event);
}

void subghz_scene_modulation_list_on_exit(void* context) {
    subghz_config_scene_exit(context);
}
