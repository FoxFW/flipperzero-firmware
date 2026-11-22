#include "saflok_data.h"
#include "helpers/saflok_field_helpers.h"
#include <furi.h>

#define TAG "SaflokData"

SaflokData* saflok_data_alloc(SaflokApp* app_context) {
    SaflokData* data = malloc(sizeof(SaflokData));
    if(!data) {
        FURI_LOG_E(TAG, "Failed to allocate SaflokData");
        return NULL;
    }

    memset(data, 0, sizeof(SaflokData));

    data->app_context = app_context;

    data->access_control.key_level = key_levels[0];
    data->access_control.sub_function = sub_functions[0];
    data->access_control.led_warning = false;
    data->access_control.opening_key = false;
    data->access_control.override_deadbolt = false;

    data->identification.key_id = 0;
    data->identification.key_record = 0;
    data->identification.property_number = 0;

    data->security.sequence_combination = 0;
    data->security.checksum = 0;

    data->time_control.creation_date.year = 2020;
    data->time_control.creation_date.month = 1;
    data->time_control.creation_date.day = 1;
    data->time_control.creation_date.hour = 0;
    data->time_control.creation_date.minute = 0;

    data->time_control.expiration_date.year = 2020;
    data->time_control.expiration_date.month = 1;
    data->time_control.expiration_date.day = 1;
    data->time_control.expiration_date.hour = 0;
    data->time_control.expiration_date.minute = 0;

    for(int i = 0; i < DAY_COUNT; i++) {
        data->time_control.restrictions.day_array[i].full_name = weekdays[i].full_name;
        data->time_control.restrictions.day_array[i].abbreviation = weekdays[i].abbreviation;
        data->time_control.restrictions.day_array[i].enabled = false;
    }

    data->time_control.restrictions.restricted_weekday_string = furi_string_alloc();
    if(!data->time_control.restrictions.restricted_weekday_string) {
        FURI_LOG_E(TAG, "Failed to allocate restricted_weekday_string");
        free(data);
        return NULL;
    }

    furi_string_set(data->time_control.restrictions.restricted_weekday_string, "none");

    return data;
}

bool saflok_data_reset(SaflokApp* app) {
    if(!app) {
        FURI_LOG_E(TAG, "Invalid data pointer");
        return false;
    }

    if(app->saflok_data->time_control.restrictions.restricted_weekday_string) {
        furi_string_free(app->saflok_data->time_control.restrictions.restricted_weekday_string);
    }

    memset(app->saflok_data, 0, sizeof(SaflokData));

    app->saflok_data->app_context = app;

    app->saflok_data->access_control.key_level = key_levels[0];
    app->saflok_data->access_control.sub_function = sub_functions[0];
    app->saflok_data->access_control.led_warning = false;
    app->saflok_data->access_control.opening_key = false;
    app->saflok_data->access_control.override_deadbolt = false;

    app->saflok_data->identification.key_id = 0;
    app->saflok_data->identification.key_record = 0;
    app->saflok_data->identification.property_number = 0;

    app->saflok_data->security.sequence_combination = 0;
    app->saflok_data->security.checksum = 0;

    app->saflok_data->time_control.creation_date.year = 2020;
    app->saflok_data->time_control.creation_date.month = 1;
    app->saflok_data->time_control.creation_date.day = 1;
    app->saflok_data->time_control.creation_date.hour = 0;
    app->saflok_data->time_control.creation_date.minute = 0;

    app->saflok_data->time_control.expiration_date.year = 2020;
    app->saflok_data->time_control.expiration_date.month = 1;
    app->saflok_data->time_control.expiration_date.day = 1;
    app->saflok_data->time_control.expiration_date.hour = 0;
    app->saflok_data->time_control.expiration_date.minute = 0;

    for(int i = 0; i < DAY_COUNT; i++) {
        app->saflok_data->time_control.restrictions.day_array[i].full_name = weekdays[i].full_name;
        app->saflok_data->time_control.restrictions.day_array[i].abbreviation = weekdays[i].abbreviation;
        app->saflok_data->time_control.restrictions.day_array[i].enabled = false;
    }

    app->saflok_data->time_control.restrictions.restricted_weekday_string = furi_string_alloc();
    if(!app->saflok_data->time_control.restrictions.restricted_weekday_string) {
        FURI_LOG_E(TAG, "Failed to allocate restricted_weekday_string");
        return false;
    }

    furi_string_set(app->saflok_data->time_control.restrictions.restricted_weekday_string, "none");

    return true;
}

void saflok_data_free(SaflokData* data) {
    if(!data) {
        FURI_LOG_W(TAG, "Attempted to free NULL SaflokData");
        return;
    }

    if(data->time_control.restrictions.restricted_weekday_string) {
        furi_string_free(data->time_control.restrictions.restricted_weekday_string);
        data->time_control.restrictions.restricted_weekday_string = NULL;
    }

    free(data);
}
