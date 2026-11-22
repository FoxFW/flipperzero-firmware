#pragma once
#include <furi.h>
#include <gui/canvas.h>
#include <gui/modules/file_browser.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORD_DIALOGS "dialogs"

typedef struct DialogsApp DialogsApp;

typedef struct {
    const char* extension;
    const char* base_path;
    bool skip_assets;
    bool hide_dot_files;
    const Icon* icon;
    bool hide_ext;
    FileBrowserLoadItemCallback item_loader_callback;
    void* item_loader_context;
    bool sort_by_name;
} DialogsFileBrowserOptions;

void dialog_file_browser_set_basic_options(
    DialogsFileBrowserOptions* options,
    const char* extension,
    const Icon* icon);

bool dialog_file_browser_show(
    DialogsApp* context,
    FuriString* result_path,
    FuriString* path,
    const DialogsFileBrowserOptions* options);

typedef enum {
    DialogMessageButtonBack,
    DialogMessageButtonLeft,
    DialogMessageButtonCenter,
    DialogMessageButtonRight,
} DialogMessageButton;

typedef struct DialogMessage DialogMessage;

DialogMessage* dialog_message_alloc(void);

void dialog_message_free(DialogMessage* message);

void dialog_message_set_text(
    DialogMessage* message,
    const char* text,
    uint8_t x,
    uint8_t y,
    Align horizontal,
    Align vertical);

void dialog_message_set_header(
    DialogMessage* message,
    const char* text,
    uint8_t x,
    uint8_t y,
    Align horizontal,
    Align vertical);

void dialog_message_set_icon(DialogMessage* message, const Icon* icon, uint8_t x, uint8_t y);

void dialog_message_set_buttons(
    DialogMessage* message,
    const char* left,
    const char* center,
    const char* right);

DialogMessageButton dialog_message_show(DialogsApp* context, const DialogMessage* message);

void dialog_message_show_storage_error(DialogsApp* context, const char* error_text);

#ifdef __cplusplus
}
#endif
