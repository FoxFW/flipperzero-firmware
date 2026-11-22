#ifndef SAFLOK_DIALOGS_H
#define SAFLOK_DIALOGS_H

#include "../saflok_app_i.h"

bool saflok_dialog_shadow_prompt(SaflokApp* app);

DialogMessageButton saflok_dialog_edit_mode_prompt(SaflokApp* app);

bool saflok_dialog_risky_operation(SaflokApp* app);

SaveResponse saflok_dialog_unsaved_changes(SaflokApp* app);

void saflok_dialog_error(SaflokApp* app, const char* message);

void saflok_dialog_success(SaflokApp* app, const char* message);

DialogMessageButton show_dialog(
    SaflokApp* app,
    const char* header,
    const char* text,
    const char* left_button,
    const char* center_button,
    const char* right_button);

#endif
