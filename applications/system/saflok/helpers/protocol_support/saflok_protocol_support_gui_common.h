#pragma once

#include <gui/modules/widget.h>

#include "../../saflok_app.h"
#include "../../saflok_app_i.h"

enum {
    SubmenuIndexCommonSave,
    SubmenuIndexCommonEmulate,
    SubmenuIndexCommonEdit,
    SubmenuIndexCommonInfo,
    SubmenuIndexCommonRename,
    SubmenuIndexCommonDelete,
    SubmenuIndexCommonRestore,
    SubmenuIndexCommonMax,
};

void saflok_protocol_support_common_submenu_callback(void* context, uint32_t index);

void saflok_protocol_support_common_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context);

void saflok_protocol_support_common_byte_input_done_callback(void* context);

void saflok_protocol_support_common_text_input_done_callback(void* context);

void saflok_protocol_support_common_on_enter_empty(SaflokApp* app);

bool saflok_protocol_support_common_on_event_empty(SaflokApp* app, SceneManagerEvent event);
