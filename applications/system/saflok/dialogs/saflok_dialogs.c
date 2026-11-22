#include "../saflok_app_i.h"
#include "saflok_dialogs.h"

DialogMessageButton show_dialog(
    SaflokApp* app,
    const char* header,
    const char* text,
    const char* left_button,
    const char* center_button,
    const char* right_button) {
    
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, header, 63, 3, AlignCenter, AlignTop);
    dialog_message_set_text(message, text, 63, 31, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, left_button, center_button, right_button);

    DialogMessageButton result = dialog_message_show(app->dialogs, message);
    dialog_message_free(message);

    return result;
}

bool saflok_dialog_shadow_prompt(SaflokApp* app) {
    DialogMessageButton result = show_dialog(
        app,
        "File has modifications",
        "Would you like to edit a copy\nof the tag (recommended)\nor the original?",
        "Original",
        NULL,
        "Copy");

    return result == DialogMessageButtonRight;
}

DialogMessageButton saflok_dialog_edit_mode_prompt(SaflokApp* app) {
    return show_dialog(
        app,
        "Edit Mode",
        "Would you like to edit a copy\nof the tag (recommended)\nor the original?",
        "Original",
        NULL,
        "Copy");
}

bool saflok_dialog_risky_operation(SaflokApp* app) {
    DialogMessageButton result = show_dialog(
        app,
        "Risky operation",
        "Altering this data carries the\nrisk of corrupting the tag,\nrendering it unusable.\n\nAre you sure you want to\ncontinue?",
        "Back",
        "Continue",
        NULL);

    return result == DialogMessageButtonCenter;
}

SaveResponse saflok_dialog_unsaved_changes(SaflokApp* app) {
    DialogMessageButton result = show_dialog(
        app,
        "Unsaved changes",
        "Would you like to save?\nDiscarding your\nchanges is permanent.",
        "Discard",
        "Save",
        "Cancel");

    if(result == DialogMessageButtonLeft) return SaveResponseDiscard;
    if(result == DialogMessageButtonCenter) return SaveResponseSave;
    return SaveResponseCancel;
}

void saflok_dialog_error(SaflokApp* app, const char* message) {
    show_dialog(app, "Error", message, NULL, "Ok", NULL);
}

void saflok_dialog_success(SaflokApp* app, const char* message) {
    show_dialog(app, "Success", message, NULL, "Ok", NULL);
}
