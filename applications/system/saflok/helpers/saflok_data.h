#ifndef SAFLOK_DATA_H
#define SAFLOK_DATA_H

#include "saflok_app_i.h"

SaflokData* saflok_data_alloc(SaflokApp* app_context);
bool saflok_data_reset(SaflokApp* app);

void saflok_data_free(SaflokData* data);

#endif
