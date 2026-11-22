#include "../subghz_i.h"
#include "../helpers/subghz_config_scene_loader.h"

void subghz_scene_protocol_list_on_enter(void* context) {
    subghz_config_scene_enter(context, "subghz_config_protocollist", SubGhzSceneProtocolList);
}

bool subghz_scene_protocol_list_on_event(void* context, SceneManagerEvent event) {
    return subghz_config_scene_event(context, event);
}

void subghz_scene_protocol_list_on_exit(void* context) {
    subghz_config_scene_exit(context);
}
