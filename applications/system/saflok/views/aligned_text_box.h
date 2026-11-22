#pragma once

#include <gui/view.h>
#include "../saflok_app_i.h"

typedef struct AlignedTextBox AlignedTextBox;

AlignedTextBox* aligned_text_box_alloc();
void aligned_text_box_free(AlignedTextBox* aligned_text_box);
View* aligned_text_box_get_view(AlignedTextBox* aligned_text_box);
void aligned_text_box_reset(AlignedTextBox* aligned_text_box);

void aligned_text_box_set_saflok_data(AlignedTextBox* aligned_text_box, SaflokData* data);
