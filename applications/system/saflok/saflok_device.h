#ifndef SAFLOK_DEVICE_H
#define SAFLOK_DEVICE_H

#include <furi.h>
#include <core/string.h>

typedef struct SaflokApp SaflokApp;
PromptResponse saflok_device_load_from_path(SaflokApp* app, FuriString* file_path);
bool saflok_device_save_to_path(SaflokApp* app, const char* path);
void saflok_device_get_shadow_path(FuriString* file_path, FuriString* shadow_file_path);
bool saflok_device_has_shadow(SaflokApp* app, FuriString* file_path);

#endif
