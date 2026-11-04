#pragma once

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VariableItemList VariableItemList;
typedef struct VariableItem VariableItem;
typedef void (*VariableItemChangeCallback)(VariableItem* item);
typedef void (*VariableItemListEnterCallback)(void* context, uint32_t index);

VariableItemList* variable_item_list_alloc(void);

void variable_item_list_free(VariableItemList* variable_item_list);

void variable_item_list_reset(VariableItemList* variable_item_list);

void variable_item_list_reserve(VariableItemList* variable_item_list, size_t count);

View* variable_item_list_get_view(VariableItemList* variable_item_list);

VariableItem* variable_item_list_add(
    VariableItemList* variable_item_list,
    const char* label,
    uint8_t values_count,
    VariableItemChangeCallback change_callback,
    void* context);

VariableItem* variable_item_list_get(VariableItemList* variable_item_list, uint8_t position);

void variable_item_list_set_enter_callback(
    VariableItemList* variable_item_list,
    VariableItemListEnterCallback callback,
    void* context);

void variable_item_list_set_selected_item(VariableItemList* variable_item_list, uint8_t index);

uint8_t variable_item_list_get_selected_item_index(VariableItemList* variable_item_list);

void variable_item_set_current_value_index(VariableItem* item, uint8_t current_value_index);

void variable_item_set_values_count(VariableItem* item, uint8_t values_count);

void variable_item_set_item_label(VariableItem* item, const char* label);

void variable_item_set_current_value_text(VariableItem* item, const char* current_value_text);

void variable_item_set_locked(VariableItem* item, bool locked, const char* locked_message);

void variable_item_set_centered(VariableItem* item, bool centered);

uint8_t variable_item_get_current_value_index(VariableItem* item);

void* variable_item_get_context(VariableItem* item);

#ifdef __cplusplus
}
#endif
