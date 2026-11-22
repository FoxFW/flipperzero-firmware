#pragma once

#include <gui/view.h>
#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char* display_text;
    int32_t int_value;
    bool enabled;
} OptionValue;

typedef struct OptionValueModel {
    FuriString* display_text;
    void* value;
    size_t value_size;
    bool enabled;
    void* value_context;
} OptionValueModel;

typedef struct OptionEntry OptionEntry;

typedef void (*OptionEntryChangeCallback)(OptionEntry* entry);

typedef struct OptionEntry {
    FuriString* label;
    uint8_t index;
    bool spacer;
    bool locked;
    FuriString* locked_message;

    OptionValue* values;
    uint8_t values_count;
    uint8_t current_value;

    OptionEntryChangeCallback change_callback;
    void* context;
} OptionEntry;

typedef void (*OptionListEnterCallback)(void* context, uint8_t position);

typedef struct OptionList {
    View* view;
    OptionListEnterCallback callback;
    void* context;

    FuriTimer* scroll_timer;
    FuriTimer* locked_timer;
} OptionList;

OptionValueModel* option_value_model_alloc(
    const char* display_text,
    void* value,
    size_t value_size,
    bool enabled,
    void* value_context);

OptionValueModel* option_value_model_alloc_int(
    const char* display_text,
    int32_t int_value,
    bool enabled);

OptionValueModel* option_value_model_alloc_string(
    const char* display_text,
    const char* string_value,
    bool enabled);

void option_value_model_free(OptionValueModel* model);

const char* option_value_model_get_display_text(const OptionValueModel* model);

int32_t option_value_model_get_int_value(const OptionValueModel* model);

const char* option_value_model_get_string_value(const OptionValueModel* model);

bool option_value_model_is_enabled(const OptionValueModel* model);

void option_value_model_set_enabled(OptionValueModel* model, bool enabled);

OptionEntry* option_entry_alloc(
    const char* label,
    uint8_t index,
    OptionEntryChangeCallback change_callback,
    void* context);

void option_entry_free(OptionEntry* entry);

void option_entry_add_value_model(OptionEntry* entry, OptionValueModel* model);

void option_entry_add_int_value(
    OptionEntry* entry,
    const char* display_text,
    int32_t int_value,
    bool enabled);

void option_entry_add_string_value(
    OptionEntry* entry,
    const char* display_text,
    const char* string_value,
    bool enabled);

bool option_entry_set_current_index(OptionEntry* entry, uint8_t index);

uint8_t option_entry_get_current_index(const OptionEntry* entry);

OptionValueModel* option_entry_get_current_value_model(const OptionEntry* entry);

const char* option_entry_get_current_display_text(const OptionEntry* entry);

int32_t option_entry_get_current_int_value(const OptionEntry* entry);

const char* option_entry_get_current_string_value(const OptionEntry* entry);

bool option_entry_next_value(OptionEntry* entry);

bool option_entry_prev_value(OptionEntry* entry);

void option_entry_set_locked(OptionEntry* entry, bool locked, const char* locked_message);

void option_entry_set_label(OptionEntry* entry, const char* label);

void* option_entry_get_context(const OptionEntry* entry);

OptionList* option_list_alloc(void);

void option_list_free(OptionList* list);

void option_list_clear_entries(OptionList* list);

void option_list_reset(OptionList* list);

View* option_list_get_view(OptionList* list);

void option_list_add_entry(OptionList* list, OptionEntry* entry);

OptionEntry* option_list_add_entry_create(
    OptionList* list,
    uint8_t index,
    const char* label,
    OptionEntryChangeCallback change_callback,
    void* context);

OptionEntry* option_list_add_spacer(OptionList* list, bool divider, void* context);

OptionEntry* option_list_get(OptionList* list, uint8_t position);

OptionEntry* option_list_get_entry_by_index(OptionList* list, uint8_t index);

void option_list_set_selected_item(OptionList* list, uint8_t index);

uint8_t option_list_get_selected_item_index(OptionList* list);

void option_list_set_header(OptionList* list, const char* header);

void option_list_set_subheader(OptionList* list, const char* subheader);

void option_list_set_enter_callback(
    OptionList* list,
    OptionListEnterCallback callback,
    void* context);

OptionEntry* option_list_add(
    OptionList* list,
    uint8_t index,
    const char* label,
    uint8_t values_count,
    OptionEntryChangeCallback change_callback,
    void* context);

void option_entry_set_current_value_index(OptionEntry* entry, uint8_t current_value_index);

void option_entry_set_values_count(OptionEntry* entry, uint8_t values_count);

void option_entry_set_item_label(OptionEntry* entry, const char* label);

void option_entry_set_current_value_text(OptionEntry* entry, const char* current_value_text);

uint8_t option_entry_get_current_value_index(OptionEntry* entry);

#ifdef __cplusplus
}
#endif
