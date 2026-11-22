#ifndef SAFLOK_SCENE_FIELD_SELECT_H
#define SAFLOK_SCENE_FIELD_SELECT_H
void saflok_scene_field_select_submenu_callback(void* context, uint32_t index);
void saflok_scene_field_select_on_enter(void* context);
bool saflok_scene_field_select_on_event(void* context, SceneManagerEvent event);
void saflok_scene_field_select_on_exit(void* context);
#endif
