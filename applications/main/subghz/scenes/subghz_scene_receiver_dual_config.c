#include "../subghz_i.h"
#include "../helpers/subghz_config_scene_loader.h"

/* FoxFW2.0 config-externalization Stage 1 (2026-10-31): this scene's body now
 * lives in config_ext/subghz_config_dual.c, loaded on demand as
 * subghz_config_dual.fal. These handlers are thin shims over the resident loader. */

void subghz_scene_receiver_dual_config_on_enter(void* context) {
    subghz_config_scene_enter(context, "subghz_config_dual", SubGhzSceneReceiverDualConfig);
}

bool subghz_scene_receiver_dual_config_on_event(void* context, SceneManagerEvent event) {
    return subghz_config_scene_event(context, event);
}

void subghz_scene_receiver_dual_config_on_exit(void* context) {
    subghz_config_scene_exit(context);
}
