#pragma once

#include <gui/view.h>

typedef struct FmhBoxList FmhBoxList;

typedef void (*FmhBoxListCallback)(void* context, uint32_t index);

typedef struct {
    const char* title;
    const char* subtitle;
} FmhBoxListOption;

FmhBoxList* fmh_box_list_alloc(void);
void fmh_box_list_free(FmhBoxList* instance);
View* fmh_box_list_get_view(FmhBoxList* instance);
void fmh_box_list_set_callback(FmhBoxList* instance, FmhBoxListCallback callback, void* context);
void fmh_box_list_set_header(FmhBoxList* instance, const char* header);
void fmh_box_list_set_options(
    FmhBoxList* instance,
    const FmhBoxListOption* options,
    uint8_t count);
void fmh_box_list_set_selected(FmhBoxList* instance, uint8_t index);
void fmh_box_list_resume_scroll(FmhBoxList* instance);
void fmh_box_list_pause_scroll(FmhBoxList* instance);
