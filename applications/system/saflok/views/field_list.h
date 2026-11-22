#ifndef FIELD_LIST_H
#define FIELD_LIST_H

#include <gui/view.h>
#include <gui/canvas.h>
#include <gui/elements.h>
#include <gui/modules/submenu.h>

void abbreviate_days(const char* input, FuriString* output);
char* get_field_value(SaflokData* data, uint8_t index, char* buffer, size_t buffer_size);
void draw_aligned_field(Canvas* canvas, int y_pos, const char* label, const char* value, bool is_weekday, bool is_pass_levels, SaflokData* data);
void field_list_draw_callback(Canvas* canvas, void* model);
FieldList* field_list_alloc();
void field_list_free(FieldList* field_list);
View* field_list_get_view(FieldList* field_list);
void field_list_set_callback(FieldList* field_list, FieldListEnterCallback callback, void* context);
void field_list_set_saflok_data(FieldList* field_list, SaflokData* data);
void field_list_set_selected_item(FieldList* field_list, uint8_t index);
uint8_t field_list_get_selected_item(FieldList* field_list);
void field_list_reset(FieldList* field_list);

#endif
