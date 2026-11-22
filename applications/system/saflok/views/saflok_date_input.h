#ifndef SAFLOK_DATE_INPUT_H
#define SAFLOK_DATE_INPUT_H

#include "scenes/saflok_scene.h"
#include "saflok_types.h"

#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/submenu.h>

typedef struct DateInput {
    View* view;
    FuriEventLoopTimer* timer;
} DateInput;

typedef struct DateInputModel {
    DateTime date;
    bool editing;
    uint8_t row;
    uint8_t column;
} DateInputModel;

typedef enum {
    DateInputCustomEventNone,
} DateInputCustomEvent;

typedef void (*DateInputCallback)(uint8_t channel_id, uint32_t freq, uint8_t duty, void* context);

View* saflok_date_input_get_view(DateInput* instance);

void saflok_date_input_set(DateInput* instance, const DateTime* datetime, bool enabled);

bool saflok_date_input_get(DateInput* instance, DateTime* datetime);

DateInput* saflok_date_input_alloc();

void saflok_date_input_free(DateInput* instance);

void saflok_date_input_reset(DateInput* instance);

bool date_input_init_from_date(DateInputModel* model, SaflokDateTime* date);

bool date_input_extract_to_date(DateInputModel* model, SaflokDateTime* date);

bool date_has_changed(SaflokDateTime* original, SaflokDateTime* new_date);

#endif
