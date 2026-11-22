#include "saflok_app_i.h"
#include "saflok_encryption_helpers.h"
#include "saflok_app_helpers.h"
#include <furi.h>
#include <furi/core/log.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

const SaflokDay weekdays[DAY_COUNT] = {
    [MONDAY] =    {"Monday",    "M"},
    [TUESDAY] =   {"Tuesday",   "Tu"},
    [WEDNESDAY] = {"Wednesday", "W"},
    [THURSDAY] =  {"Thursday",  "Th"},
    [FRIDAY] =    {"Friday",    "F"},
    [SATURDAY] =  {"Saturday",  "Sa"},
    [SUNDAY] =    {"Sunday",    "Su"},
    [ALL] =       {"All",       "All"},
    [WEEKDAYS] =  {"Weekdays",  "M-F"},
    [WEEKENDS] =  {"Weekends",  "Wkend"},
    [NONE] =      {"None",      "None"}
};

const SaflokSubFunction sub_functions[] = {
    {"Normal",      0},
    {"Resequence",  1},
    {"Pass",        2},
    {"Lockout",     3},
    {"Update",      4},
    {"Emergency",   5},
    {"Programming", 6}
};

const char* field_labels[] = {
    "Key Level:",
    "Sub Function:",
    "Key ID:",
    "LED Warning:",
    "Key Record:",
    "Pass Levels:",
    "Opening Key:",
    "Seq. & Combination:",
    "Override Deadbolt:",
    "Restrictions:",
    "Creation:",
    "Expiration:",
    "Property number:",
    "Checksum Valid:"};

const SaflokKeyLevel key_levels[] = {
    {"Guest Key",     1},
    {"Connectors",    2},
    {"Suite",         3},
    {"Limited Use",   4},
    {"Failsafe",      5},
    {"Inhibit",       6},
    {"Common Master", 7},
    {"Housekeeping",  8},
    {"Floor Key",     9},
    {"Section Key",  10},
    {"Rooms Master", 11},
    {"Grand Master", 12},
    {"Emergency",    13},
    {"Lockout",      14},
    {"SPK",          15},
    {"PPK",          16},
};

typedef enum {
    GuestKey = 1,
    Connectors,
    Suite,
    LimitedUse,
    Failsafe,
    Inhibit,
    CommonMaster,
    Housekeeping,
    FloorKey,
    SectionKey,
    RoomsMaster,
    GrandMaster,
    Emergency,
    Lockout,
    SPK,
    PPK
} KeyLevelEnum;

typedef enum {
    None = 0b00000000,
    Monday = 0b01000000,
    Tuesday = 0b00100000,
    Wednesday = 0b00010000,
    Thursday = 0b00001000,
    Friday = 0b00000100,
    Saturday = 0b00000010,
    Sunday = 0b00000001,
    All = 0b01111111,
    Weekdays = 0b01111100,
    Weekends = 0b00000011
} RestrictionsEnum;

bool saflok_validate_field_value(uint32_t value, uint32_t min, uint32_t max) {
    if(value < min || value > max) {
        return false;
    }
    return true;
}

int days_in_month(uint8_t month, uint16_t year) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if(month == 2) {
        if((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) {
            return 29;
        }
    }
    return days[month - 1];
}

bool is_valid_date(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute) {
    if(year < MIN_VALID_YEAR || year > MAX_VALID_YEAR) return false;
    if(month < 1 || month > 12) return false;
    if(hour > 23) return false;
    if(minute > 59) return false;

    uint8_t max_days = days_in_month(month, year);

    return day >= 1 && day <= max_days;
}

void saflok_normalize_date(uint8_t *day, uint8_t *month, uint16_t *year) {
    FURI_LOG_D("Saflok", "Normalizing date: %d/%d/%d", *day, *month, *year);
    while (*month > 12) {
        *month -= 12;
        (*year)++;
    }

    while (*day > days_in_month(*month, *year)) {
        *day -= days_in_month(*month, *year);
        (*month)++;
        if (*month > 12) {
            *month -= 12;
            (*year)++;
        }
    }
    FURI_LOG_D("Saflok", "Normalized date: %d/%d/%d", *day, *month, *year);
}

uint8_t saflok_packed_sub_function(SaflokApp* app) {
    return app->saflok_data->packed_data[0] & 0x07;
}

bool saflok_pack_sub_function(uint8_t sub_function, SaflokApp* app) {
    app->saflok_data->packed_data[0] = (app->saflok_data->packed_data[0] & ~0x07) | (sub_function & 0x07);
    saflok_update_checksum(app);
    return true;
}

bool saflok_pack_current_sub_function(SaflokApp* app) {
    return saflok_pack_sub_function(app->saflok_data->access_control.sub_function.value, app);
}

bool saflok_unpack_sub_function(uint8_t* decoded_data, SaflokApp* app) {
    FURI_LOG_D("Unpack", "Attempting sub_function unpack");
    app->saflok_data->access_control.sub_function = sub_functions[decoded_data[0] & 0x07];
    return true;
}

uint8_t saflok_packed_key_level(SaflokApp* app) {
    return (app->saflok_data->packed_data[0] & 0xF0) >> 4;
}

bool saflok_pack_key_level(uint8_t key_level, SaflokApp* app) {
    app->saflok_data->packed_data[0] = (app->saflok_data->packed_data[0] & ~0xF0) | (((key_level - 1) & 0x0F) << 4);
    saflok_update_checksum(app);
    return true;
}

bool saflok_pack_current_key_level(SaflokApp* app) {
    return saflok_pack_key_level(app->saflok_data->access_control.key_level.value, app);
}

bool saflok_unpack_key_level(uint8_t* decoded_data, SaflokApp* app) {
    FURI_LOG_D("Unpack", "Attempting key_level unpack");
    app->saflok_data->access_control.key_level = key_levels[((decoded_data[0] & 0xF0) >> 4)];
    return true;
}

bool saflok_packed_led_warning(SaflokApp* app) {
    uint8_t led_warning = app->saflok_data->packed_data[0] & 0x08;
    return led_warning != 0;
}

bool saflok_pack_led_warning(bool warning, SaflokApp* app) {
    app->saflok_data->packed_data[0] = (app->saflok_data->packed_data[0] & ~0x08) | (warning ? 0x08 : 0x00);
    saflok_update_checksum(app);
    return true;
}

bool saflok_pack_current_led_warning(SaflokApp* app) {
    return saflok_pack_led_warning(app->saflok_data->access_control.led_warning, app);
}

bool saflok_unpack_led_warning(uint8_t* decoded_data, SaflokApp* app) {
    FURI_LOG_D("Unpack", "Attempting led_warning unpack");
    app->saflok_data->access_control.led_warning = ((decoded_data[0] & 0x08) >> 3) != 0;
    return true;
}

uint8_t saflok_packed_key_id(SaflokApp* app) {
    return app->saflok_data->packed_data[1];
}

bool saflok_pack_key_id(uint8_t key_id, SaflokApp* app) {
    app->saflok_data->packed_data[1] = key_id;
    saflok_update_checksum(app);
    return true;
}

bool saflok_pack_current_key_id(SaflokApp* app) {
    return saflok_pack_key_id(app->saflok_data->identification.key_id, app);
}

bool saflok_unpack_key_id(uint8_t* decoded_data, SaflokApp* app) {
    FURI_LOG_D("Unpack", "Attempting key_id unpack");
    app->saflok_data->identification.key_id = decoded_data[1];
    return true;
}

uint16_t saflok_packed_key_record(SaflokApp* app) {
    uint16_t key_record = ((app->saflok_data->packed_data[2] & 0x7F) << 8) | app->saflok_data->packed_data[3];
    return key_record;
}

bool saflok_pack_key_record(uint16_t key_record, SaflokApp* app) {
    app->saflok_data->packed_data[2] = (app->saflok_data->packed_data[2] & 0x80) | ((key_record >> 8) & 0x7F);
    app->saflok_data->packed_data[3] = key_record & 0xFF;
    saflok_update_checksum(app);
    return true;
}

bool saflok_pack_current_key_record(SaflokApp* app) {
    return saflok_pack_key_record(app->saflok_data->identification.key_record, app);
}

bool saflok_unpack_key_record(uint8_t* decoded_data, SaflokApp* app) {
    FURI_LOG_D("Unpack", "Attempting key_record unpack");
    app->saflok_data->identification.key_record = ((decoded_data[2] & 0x7F) << 8) | decoded_data[3];
    return true;
}

bool saflok_packed_opening_key(SaflokApp* app) {
    uint8_t opening_key = app->saflok_data->packed_data[2] & 0x80;
    return opening_key != 0;
}

bool saflok_pack_opening_key(bool opening_key, SaflokApp* app) {
    app->saflok_data->packed_data[2] = (app->saflok_data->packed_data[2] & 0x7F) | (opening_key ? 0x80 : 0x00);
    saflok_update_checksum(app);
    return true;
}

bool saflok_pack_current_opening_key(SaflokApp* app) {
    return saflok_pack_opening_key(app->saflok_data->access_control.opening_key, app);
}

bool saflok_unpack_opening_key(uint8_t* decoded_data, SaflokApp* app) {
    FURI_LOG_D("Unpack", "Attempting opening_key unpack");
    app->saflok_data->access_control.opening_key = ((decoded_data[2] & 0x80) >> 7) != 0;
    return true;
}

uint8_t* saflok_packed_pass_levels(SaflokApp* app) {
    static uint8_t pass_levels[12];

    memset(pass_levels, 0, 12);

    for(int i = 0; i < 12; i++) {
        if(app->saflok_data->packed_data[4] & (1 << i)) {
            pass_levels[i] = i + 5;
        } else if(app->saflok_data->packed_data[5] & (1 << (i + 4))) {
            pass_levels[i] = i + 1;
        }
    }

    return pass_levels;
}

bool saflok_pack_pass_levels(uint8_t* pass_levels, SaflokApp* app) {
    app->saflok_data->packed_data[4] = 0;
    app->saflok_data->packed_data[5] &= 0x0F;

    for(int i = 0; i < 12 && pass_levels[i] != 0; i++) {
        uint8_t level = pass_levels[i];

        if(level >= 1 && level <= 4) {
            app->saflok_data->packed_data[5] |= (1 << (level - 1 + 4));
        } else if(level >= 5 && level <= 12) {
            app->saflok_data->packed_data[4] |= (1 << (level - 5));
        }
    }

    saflok_update_checksum(app);
    app->changed->pass_levels = false;

    return true;
}

bool saflok_pack_current_pass_levels(SaflokApp* app) {
    return saflok_pack_pass_levels(app->saflok_data->access_control.pass_levels, app);
}

bool saflok_unpack_pass_levels(uint8_t* keydata, SaflokApp* app) {
    FURI_LOG_D("Unpack", "Attempting pass_levels unpack");
    memset(app->saflok_data->access_control.pass_levels, 0, sizeof(app->saflok_data->access_control.pass_levels));

    uint8_t byte5 = keydata[5];
    uint8_t byte4 = keydata[4];

    int index = 0;

    for (int i = 4; i <= 7; i++) {
        if ((byte5 >> i) & 1) {
            app->saflok_data->access_control.pass_levels[index++] = i - 3;
        }
    }

    for (int i = 0; i <= 7; i++) {
        if ((byte4 >> i) & 1) {
            app->saflok_data->access_control.pass_levels[index++] = i + 5;
        }
    }

    return true;
}

uint16_t saflok_packed_sequence_combo(SaflokApp* app) {
    uint16_t sequence = ((app->saflok_data->packed_data[5] & 0x0F) << 8) | app->saflok_data->packed_data[6];
    return sequence;
}

bool saflok_pack_sequence_combo(uint16_t sequence, SaflokApp* app) {
    app->saflok_data->packed_data[5] = (app->saflok_data->packed_data[5] & 0xF0) | ((sequence >> 8) & 0x0F);
    app->saflok_data->packed_data[6] = sequence & 0xFF;

    saflok_update_checksum(app);
    app->changed->sequence_combo = false;
    return true;
}

bool saflok_pack_current_sequence_combo(SaflokApp* app) {
    return saflok_pack_sequence_combo(app->saflok_data->security.sequence_combination, app);
}

bool saflok_unpack_sequence_combo(uint8_t* decoded_data, SaflokApp* app) {
    FURI_LOG_D("Unpack", "Attempting sequence_combo unpack");
    app->saflok_data->security.sequence_combination = ((decoded_data[5] & 0x0F) << 8) | decoded_data[6];
    return true;
}

bool saflok_packed_override_deadbolt(SaflokApp* app) {
    uint8_t override_deadbolt = app->saflok_data->packed_data[7] & 0x80;
    return override_deadbolt != 0;
}

bool saflok_pack_override_deadbolt(bool override, SaflokApp* app) {
    app->saflok_data->packed_data[7] = (app->saflok_data->packed_data[7] & 0x7F) | (override ? 0x80 : 0x00);

    saflok_update_checksum(app);
    app->changed->override_deadbolt = false;
    return true;
}

bool saflok_pack_current_override_deadbolt(SaflokApp* app) {
    return saflok_pack_override_deadbolt(app->saflok_data->access_control.override_deadbolt, app);
}

bool saflok_unpack_override_deadbolt(uint8_t* decoded_data, SaflokApp* app) {
    FURI_LOG_D("Unpack", "Attempting override_deadbolt unpack");
    app->saflok_data->access_control.override_deadbolt = ((decoded_data[7] & 0x80) >> 7) != 0;
    return true;
}

Restrictions* saflok_packed_restrictions(SaflokApp* app) {
    static Restrictions* restrictions;

    uint8_t packed_bits = app->saflok_data->packed_data[7] & 0x7F;
    bool all_enabled = true;
    bool weekdays_enabled = true;
    bool weekends_enabled = true;
    bool none_enabled = true;

    for (int i = MONDAY; i <= SUNDAY; i++) {
        restrictions->day_array[i].full_name = weekdays[i].full_name;
        restrictions->day_array[i].abbreviation = weekdays[i].abbreviation;
        restrictions->day_array[i].enabled = ((packed_bits & (0b01000000 >> i)) != 0);
    }

    for (int i = MONDAY; i <= SUNDAY; i++) {
        if (!restrictions->day_array[i].enabled) {
            all_enabled = false;
            if (i < SATURDAY) {
                weekdays_enabled = false;
            } else {
                weekends_enabled = false;
                break;
            }
        } else {
            none_enabled = false;
        }
    }

    restrictions->day_array[ALL].full_name = weekdays[ALL].full_name;
    restrictions->day_array[ALL].abbreviation = weekdays[ALL].abbreviation;
    restrictions->day_array[ALL].enabled = all_enabled;

    restrictions->day_array[WEEKDAYS].full_name = weekdays[WEEKDAYS].full_name;
    restrictions->day_array[WEEKDAYS].abbreviation = weekdays[WEEKDAYS].abbreviation;
    restrictions->day_array[WEEKDAYS].enabled = weekdays_enabled;

    restrictions->day_array[WEEKENDS].full_name = weekdays[WEEKENDS].full_name;
    restrictions->day_array[WEEKENDS].abbreviation = weekdays[WEEKENDS].abbreviation;
    restrictions->day_array[WEEKENDS].enabled = weekends_enabled;

    restrictions->day_array[NONE].full_name = weekdays[NONE].full_name;
    restrictions->day_array[NONE].abbreviation = weekdays[NONE].abbreviation;
    restrictions->day_array[NONE].enabled = none_enabled;

    return restrictions;
}

bool saflok_pack_restrictions(Restrictions* restrictions, SaflokApp* app) {
    uint8_t packed_bits = 0;

    if(restrictions->day_array[NONE].enabled) {
        packed_bits = 0;
    } else if(restrictions->day_array[ALL].enabled) {
        packed_bits = 0b01111111;
    } else {
        bool all_weekdays = true;
        bool all_weekends = true;

        for(int i = MONDAY; i <= FRIDAY; i++) {
            if(!restrictions->day_array[i].enabled) {
                all_weekdays = false;
                break;
            }
        }

        for(int i = SATURDAY; i <= SUNDAY; i++) {
            if(!restrictions->day_array[i].enabled) {
                all_weekends = false;
                break;
            }
        }

        if(all_weekdays) {
            packed_bits |= 0b01111100;
        } else {
            if(restrictions->day_array[MONDAY].enabled) packed_bits |= 0b01000000;
            if(restrictions->day_array[TUESDAY].enabled) packed_bits |= 0b00100000;
            if(restrictions->day_array[WEDNESDAY].enabled) packed_bits |= 0b00010000;
            if(restrictions->day_array[THURSDAY].enabled) packed_bits |= 0b00001000;
            if(restrictions->day_array[FRIDAY].enabled) packed_bits |= 0b00000100;
        }

        if(all_weekends) {
            packed_bits |= 0b00000011;
        } else {
            if(restrictions->day_array[SATURDAY].enabled) packed_bits |= 0b00000010;
            if(restrictions->day_array[SUNDAY].enabled) packed_bits |= 0b00000001;
        }
    }

    app->saflok_data->packed_data[7] = (app->saflok_data->packed_data[7] & 0x80) | packed_bits;

    saflok_update_checksum(app);
    app->changed->restrictions = false;

    return true;
}

bool saflok_pack_current_restrictions(SaflokApp* app) {
    return saflok_pack_restrictions(&app->saflok_data->time_control.restrictions, app);
}

bool saflok_unpack_restrictions(uint8_t* decoded_data, SaflokApp* app) {
    FURI_LOG_D("Unpack", "Attempting restrictions unpack");
    uint8_t restrictions = decoded_data[7] & 0x7F;

    furi_string_reset(app->saflok_data->time_control.restrictions.restricted_weekday_string);

    int restricted_count = 0;

    for(int i = MONDAY; i <= SUNDAY; i++) {
        app->saflok_data->time_control.restrictions.day_array[i].enabled = restrictions & (0b01000000 >> i);

        if(restrictions & (0b01000000 >> i)) {
            if(restricted_count > 0) {
                furi_string_cat_printf(app->saflok_data->time_control.restrictions.restricted_weekday_string, ", ");
            }
            furi_string_cat_printf(app->saflok_data->time_control.restrictions.restricted_weekday_string, "%s", app->saflok_data->time_control.restrictions.day_array[i].full_name);
            restricted_count++;
        }
    }

    if(restricted_count == 0) {
        furi_string_set(app->saflok_data->time_control.restrictions.restricted_weekday_string, app->saflok_data->time_control.restrictions.day_array[NONE].full_name);
    }

    return true;
}

SaflokDateTime saflok_packed_creation_date(SaflokApp* app) {
    SaflokDateTime date;

    uint8_t year_low = (app->saflok_data->packed_data[11] & 0xF0) >> 4;
    uint8_t year_high = app->saflok_data->packed_data[14] & 0xF0;
    date.year = (year_low + SAFLOK_YEAR_OFFSET) | year_high;

    date.month = app->saflok_data->packed_data[11] & 0x0F;
    date.day = (app->saflok_data->packed_data[12] >> 3) & 0x1F;
    date.hour = ((app->saflok_data->packed_data[12] & 0x07) << 2) | (app->saflok_data->packed_data[13] >> 6);
    date.minute = app->saflok_data->packed_data[13] & 0x3F;

    return date;
}

bool saflok_pack_creation_date(SaflokDateTime date, SaflokApp* app) {
    uint16_t adjusted_year = date.year - SAFLOK_YEAR_OFFSET;
    if(adjusted_year > 0xFF) {
        return false;
    }

    app->saflok_data->packed_data[11] = ((adjusted_year & 0x0F) << 4) | (date.month & 0x0F);
    app->saflok_data->packed_data[12] = ((date.day & 0x1F) << 3) | ((date.hour >> 2) & 0x07);
    app->saflok_data->packed_data[13] = ((date.hour & 0x03) << 6) | (date.minute & 0x3F);
    app->saflok_data->packed_data[14] = (app->saflok_data->packed_data[14] & 0x0F) | (adjusted_year & 0xF0);

    saflok_update_checksum(app);
    app->changed->creation_date = false;
    return true;
}

bool saflok_pack_current_creation_date(SaflokApp* app) {
    return saflok_pack_creation_date(app->saflok_data->time_control.creation_date, app);
}

bool saflok_unpack_creation_date(uint8_t* decoded_data, SaflokApp* app) {
    FURI_LOG_D("Unpack", "Attempting creation_date unpack");
    uint8_t year_low = (decoded_data[11] & 0xF0) >> 4;
    uint8_t year_high = decoded_data[14] & 0xF0;
    app->saflok_data->time_control.creation_date.year = (year_low + SAFLOK_YEAR_OFFSET) | year_high;
    app->saflok_data->time_control.creation_date.month = decoded_data[11] & 0x0F;
    app->saflok_data->time_control.creation_date.day = (decoded_data[12] >> 3) & 0x1F;
    app->saflok_data->time_control.creation_date.hour = ((decoded_data[12] & 0x07) << 2) | (decoded_data[13] >> 6);
    app->saflok_data->time_control.creation_date.minute = decoded_data[13] & 0x3F;

    return true;
}

SaflokDateTime saflok_packed_expiration_date(SaflokApp* app) {
    SaflokDateTime date;

    SaflokDateTime creation = saflok_packed_creation_date(app);

    uint16_t interval_year = app->saflok_data->packed_data[8] >> 4;
    uint8_t interval_month = app->saflok_data->packed_data[8] & 0x0F;
    uint8_t interval_day = (app->saflok_data->packed_data[9] >> 3) & 0x1F;

    date.year = creation.year + interval_year;
    date.month = creation.month + interval_month;
    date.day = creation.day + interval_day;

    date.hour = ((app->saflok_data->packed_data[9] & 0x07) << 2) | (app->saflok_data->packed_data[10] >> 6);
    date.minute = app->saflok_data->packed_data[10] & 0x3F;

    uint8_t temp_day = date.day;
    uint8_t temp_month = date.month;
    uint16_t temp_year = date.year;

    saflok_normalize_date(&temp_day, &temp_month, &temp_year);

    date.day = temp_day;
    date.month = temp_month;
    date.year = temp_year;

    return date;
}

bool saflok_pack_expiration_date(SaflokDateTime date, SaflokApp* app) {
    saflok_unpack_creation_date(app->saflok_data->packed_data, app);
    SaflokDateTime creation = app->saflok_data->time_control.creation_date;

    uint16_t year_interval = date.year - creation.year;
    int16_t month_interval = date.month - creation.month;
    int16_t day_interval = date.day - creation.day;

    while(day_interval < 0) {
        month_interval--;
        day_interval += days_in_month(
            ((month_interval < 0) ? creation.month - 1 : creation.month),
            creation.year);
    }
    while(month_interval < 0) {
        year_interval--;
        month_interval += 12;
    }

    if(year_interval > 0x0F || month_interval > 0x0F || day_interval > 0x1F) {
        return false;
    }

    app->saflok_data->packed_data[8] = ((year_interval & 0x0F) << 4) | (month_interval & 0x0F);
    app->saflok_data->packed_data[9] = ((day_interval & 0x1F) << 3) | ((date.hour >> 2) & 0x07);
    app->saflok_data->packed_data[10] = ((date.hour & 0x03) << 6) | (date.minute & 0x3F);

    saflok_update_checksum(app);
    app->changed->expiration_date = false;

    return true;
}

bool saflok_pack_current_expiration_date(SaflokApp* app) {
    return saflok_pack_expiration_date(app->saflok_data->time_control.expiration_date, app);
}

bool saflok_unpack_expiration_date(uint8_t* decoded_data, SaflokApp* app) {
    FURI_LOG_D("Unpack", "Attempting expiration_date unpack");
    SaflokDateTime creation_date;
    saflok_unpack_creation_date(decoded_data, app);
    creation_date = app->saflok_data->time_control.creation_date;

    uint16_t interval_year = decoded_data[8] >> 4;
    uint8_t interval_month = decoded_data[8] & 0x0F;
    uint8_t interval_day = (decoded_data[9] >> 3) & 0x1F;

    app->saflok_data->time_control.expiration_date.year = creation_date.year + interval_year;
    app->saflok_data->time_control.expiration_date.month = creation_date.month + interval_month;
    app->saflok_data->time_control.expiration_date.day = creation_date.day + interval_day;

    app->saflok_data->time_control.expiration_date.hour = ((decoded_data[9] & 0x07) << 2) | (decoded_data[10] >> 6);
    app->saflok_data->time_control.expiration_date.minute = decoded_data[10] & 0x3F;

    saflok_normalize_date(
        &app->saflok_data->time_control.expiration_date.day,
        &app->saflok_data->time_control.expiration_date.month,
        &app->saflok_data->time_control.expiration_date.year
    );

    saflok_update_checksum(app);
    return true;
}

uint16_t saflok_packed_property_number(SaflokApp* app) {
    uint16_t property_number = ((app->saflok_data->packed_data[14] & 0x0F) << 8) | app->saflok_data->packed_data[15];
    return property_number;
}

bool saflok_pack_property_number(uint16_t property_num, SaflokApp* app) {
    app->saflok_data->packed_data[14] = (app->saflok_data->packed_data[14] & 0xF0) | ((property_num >> 8) & 0x0F);
    app->saflok_data->packed_data[15] = property_num & 0xFF;

    saflok_update_checksum(app);
    app->changed->property_number = false;

    return true;
}

bool saflok_pack_current_property_number(SaflokApp* app) {
    return saflok_pack_property_number(app->saflok_data->identification.property_number, app);
}

bool saflok_unpack_property_number(uint8_t* decoded_data, SaflokApp* app) {
    FURI_LOG_D("Unpack", "Attempting property_number unpack");
    app->saflok_data->identification.property_number = ((decoded_data[14] & 0x0F) << 8) | decoded_data[15];
    return true;
}

uint8_t saflok_packed_checksum(SaflokApp* app) {
    return app->saflok_data->packed_data[SAFLOK_DATA_LENGTH - 1];
}

bool saflok_pack_checksum(uint8_t checksum, SaflokApp* app) {
    app->saflok_data->packed_data[SAFLOK_DATA_LENGTH - 1] = checksum;

    saflok_update_checksum(app);
    app->changed->checksum = false;

    return true;
}

bool saflok_pack_current_checksum(SaflokApp* app) {
    return saflok_pack_checksum(app->saflok_data->security.checksum, app);
}

bool saflok_unpack_checksum(uint8_t* decoded_data, SaflokApp* app) {
    FURI_LOG_D("Unpack", "Attempting checksum unpack");
    app->saflok_data->security.checksum = decoded_data[SAFLOK_DATA_LENGTH - 1];
    return true;
}

bool saflok_create_battery_disconnect_key(SaflokApp* app) {
    saflok_set_key_level(app, 16);
    saflok_set_sub_function(app, 4);
    saflok_set_led_warning(app, true);
    saflok_set_key_id(app, 0x1D);
    saflok_set_key_record(app, 0x7C71);
    saflok_set_opening_key(app, true);
    saflok_set_pass_levels(app, 0x10);
    saflok_set_sequence_combo(app, 0x0000);
    saflok_set_override_deadbolt(app, true);
    saflok_set_restrictions(app, 0x7F);
    return true;
}

bool saflok_create_clock_date_key(SaflokApp* app) {
    saflok_set_key_level(app, 16);
    saflok_set_sub_function(app, 5);
    saflok_set_led_warning(app, false);
    saflok_set_key_id(app, 0x02);
    saflok_set_key_record(app, 0x0000);
    saflok_set_opening_key(app, false);
    saflok_set_pass_levels(app, 0);
    saflok_set_sequence_combo(app, 0x0000);
    saflok_set_override_deadbolt(app, false);
    saflok_set_restrictions(app, 0);
    return true;
}

bool saflok_create_clock_run_key(SaflokApp* app) {
    saflok_set_key_level(app, 16);
    saflok_set_sub_function(app, 5);
    saflok_set_led_warning(app, false);
    saflok_set_key_id(app, 0x09);
    saflok_set_key_record(app, 0x0000);
    saflok_set_opening_key(app, false);
    saflok_set_pass_levels(app, 0);
    saflok_set_sequence_combo(app, 0x0000);
    saflok_set_override_deadbolt(app, false);
    saflok_set_restrictions(app, 0);
    return true;
}

bool saflok_create_clock_time_key(SaflokApp* app) {
    saflok_set_key_level(app, 16);
    saflok_set_sub_function(app, 5);
    saflok_set_led_warning(app, false);
    saflok_set_key_id(app, 0x01);
    saflok_set_key_record(app, 0x0000);
    saflok_set_opening_key(app, false);
    saflok_set_pass_levels(app, 0);
    saflok_set_sequence_combo(app, 0x0000);
    saflok_set_override_deadbolt(app, false);
    saflok_set_restrictions(app, 0);
    return true;
}

bool saflok_create_deadbolt_switch_status_key(SaflokApp* app) {
    saflok_set_key_level(app, 16);
    saflok_set_sub_function(app, 5);
    saflok_set_led_warning(app, false);
    saflok_set_key_id(app, 0x06);
    saflok_set_key_record(app, 0x0000);
    saflok_set_opening_key(app, false);
    saflok_set_pass_levels(app, 0);
    saflok_set_sequence_combo(app, 0x0000);
    saflok_set_override_deadbolt(app, false);
    saflok_set_restrictions(app, 0);
    return true;
}

bool saflok_create_disable_latch_key(SaflokApp* app) {
    saflok_set_key_level(app, 16);
    saflok_set_sub_function(app, 4);
    saflok_set_led_warning(app, false);
    saflok_set_key_id(app, 0x04);
    saflok_set_key_record(app, 0x0000);
    saflok_set_opening_key(app, false);
    saflok_set_pass_levels(app, 0);
    saflok_set_sequence_combo(app, 0x0000);
    saflok_set_override_deadbolt(app, false);
    saflok_set_restrictions(app, 0);
    return true;
}

bool saflok_create_e2_changes_key(SaflokApp* app) {
    saflok_set_key_level(app, 16);
    saflok_set_sub_function(app, 2);
    saflok_set_led_warning(app, false);
    saflok_set_key_id(app, 0);
    saflok_set_key_record(app, 0x0000);
    saflok_set_opening_key(app, false);
    saflok_set_pass_levels(app, 0);
    saflok_set_sequence_combo(app, 0x0000);
    saflok_set_override_deadbolt(app, false);
    saflok_set_restrictions(app, 0);
    return true;
}

bool saflok_create_emergency_key(SaflokApp* app) {
    saflok_set_key_level(app, 13);
    saflok_set_sub_function(app, 0);
    saflok_set_key_id(app, 0x01);
    saflok_set_led_warning(app, true);
    saflok_set_key_record(app, 0x0001);
    saflok_set_pass_levels(app, 0);
    saflok_set_opening_key(app, true);
    saflok_set_sequence_combo(app, 0x0DD1);
    saflok_set_override_deadbolt(app, false);
    saflok_set_restrictions(app, 0);
    return true;
}

bool saflok_create_enable_latch_key(SaflokApp* app) {
    saflok_set_key_level(app, 16);
    saflok_set_sub_function(app, 4);
    saflok_set_led_warning(app, false);
    saflok_set_key_id(app, 0x03);
    saflok_set_key_record(app, 0x0000);
    saflok_set_opening_key(app, false);
    saflok_set_pass_levels(app, 0);
    saflok_set_sequence_combo(app, 0x0000);
    saflok_set_override_deadbolt(app, false);
    saflok_set_restrictions(app, 0);
    return true;
}

bool saflok_create_eprom_version_key(SaflokApp* app) {
    saflok_set_key_level(app, 16);
    saflok_set_sub_function(app, 5);
    saflok_set_led_warning(app, false);
    saflok_set_key_id(app, 0);
    saflok_set_key_record(app, 0x0000);
    saflok_set_opening_key(app, false);
    saflok_set_pass_levels(app, 0);
    saflok_set_sequence_combo(app, 0x0000);
    saflok_set_override_deadbolt(app, false);
    saflok_set_restrictions(app, 0);
    return true;
}

bool saflok_create_erase_lock_memory_key(SaflokApp* app) {
    saflok_set_key_level(app, 16);
    saflok_set_sub_function(app, 3);
    saflok_set_led_warning(app, false);
    saflok_set_key_id(app, 0);
    saflok_set_key_record(app, 0x0000);
    saflok_set_opening_key(app, false);
    saflok_set_pass_levels(app, 0);
    saflok_set_sequence_combo(app, 0x0000);
    saflok_set_override_deadbolt(app, false);
    saflok_set_restrictions(app, 0);
    return true;
}

bool saflok_create_interrogation_key(SaflokApp* app) {
    saflok_set_key_level(app, 16);
    saflok_set_sub_function(app, 7);
    saflok_set_led_warning(app, false);
    saflok_set_key_id(app, 0x48);
    saflok_set_key_record(app, 0x0000);
    saflok_set_opening_key(app, false);
    saflok_set_pass_levels(app, 0);
    saflok_set_sequence_combo(app, 0x0100);
    saflok_set_override_deadbolt(app, false);
    saflok_set_restrictions(app, 0);
    return true;
}

bool saflok_create_knob_switch_status_key(SaflokApp* app) {
    saflok_set_key_level(app, 16);
    saflok_set_sub_function(app, 5);
    saflok_set_led_warning(app, false);
    saflok_set_key_id(app, 0x05);
    saflok_set_key_record(app, 0x0000);
    saflok_set_opening_key(app, false);
    saflok_set_pass_levels(app, 0);
    saflok_set_sequence_combo(app, 0x0000);
    saflok_set_override_deadbolt(app, false);
    saflok_set_restrictions(app, 0);
    return true;
}

bool saflok_create_latch_switch_status_key(SaflokApp* app) {
    saflok_set_key_level(app, 16);
    saflok_set_sub_function(app, 5);
    saflok_set_led_warning(app, false);
    saflok_set_key_id(app, 0x03);
    saflok_set_key_record(app, 0x0000);
    saflok_set_opening_key(app, false);
    saflok_set_pass_levels(app, 0);
    saflok_set_sequence_combo(app, 0x0000);
    saflok_set_override_deadbolt(app, false);
    saflok_set_restrictions(app, 0);
    return true;
}

bool saflok_create_led_diagnostics_key(SaflokApp* app) {
    saflok_set_key_level(app, 16);
    saflok_set_sub_function(app, 1);
    saflok_set_led_warning(app, false);
    saflok_set_key_id(app, 0);
    saflok_set_key_record(app, 0x0000);
    saflok_set_opening_key(app, false);
    saflok_set_pass_levels(app, 0);
    saflok_set_sequence_combo(app, 0x0000);
    saflok_set_override_deadbolt(app, false);
    saflok_set_restrictions(app, 0);
    return true;
}

bool saflok_create_low_battery_status_key(SaflokApp* app) {
    saflok_set_key_level(app, 16);
    saflok_set_sub_function(app, 5);
    saflok_set_led_warning(app, false);
    saflok_set_key_id(app, 0x08);
    saflok_set_key_record(app, 0x0000);
    saflok_set_opening_key(app, false);
    saflok_set_pass_levels(app, 0);
    saflok_set_sequence_combo(app, 0x0000);
    saflok_set_override_deadbolt(app, false);
    saflok_set_restrictions(app, 0);
    return true;
}

bool saflok_create_primary_program_key(SaflokApp* app) {
    saflok_set_key_level(app, 16);
    saflok_set_sub_function(app, 0);
    saflok_set_led_warning(app, true);
    saflok_set_key_id(app, 0x1D);
    saflok_set_key_record(app, 0x7C71);
    saflok_set_opening_key(app, true);
    saflok_set_pass_levels(app, 0x10);
    saflok_set_sequence_combo(app, 0x0000);
    saflok_set_override_deadbolt(app, true);
    saflok_set_restrictions(app, 0x7F);
    return true;
}

bool saflok_create_resequence_key(SaflokApp* app) {
    saflok_set_key_level(app, 1);
    saflok_set_sub_function(app, 1);
    saflok_set_key_id(app, 0x01);
    saflok_set_led_warning(app, false);
    saflok_set_pass_levels(app, 0);
    saflok_set_opening_key(app, false);
    saflok_set_override_deadbolt(app, false);
    saflok_set_restrictions(app, 0);
    return true;
}

bool saflok_create_secondary_program_key(SaflokApp* app) {
    saflok_set_key_level(app, 15);
    saflok_set_sub_function(app, 0);
    saflok_set_led_warning(app, true);
    saflok_set_key_id(app, 0x04);
    saflok_set_key_record(app, 0x0AA4);
    saflok_set_opening_key(app, false);
    saflok_set_pass_levels(app, 0x001C);
    saflok_set_sequence_combo(app, 0x0000);
    saflok_set_override_deadbolt(app, true);
    saflok_set_restrictions(app, 0);
    return true;
}

void saflok_update_mf_classic_from_packed_data(SaflokApp* app) {
    uint8_t encrypted_data[SAFLOK_DATA_LENGTH] = {0};

    saflok_encrypt(app->saflok_data->packed_data, encrypted_data);

    memcpy(app->mf_classic_data->block[1].data, encrypted_data, 16);
    app->mf_classic_data->block[2].data[0] = encrypted_data[16];

    app->mf_classic_data->type = MfClassicType1k;

    nfc_device_set_data(app->saflok_device, NfcProtocolMfClassic, (MfClassicData*)app->mf_classic_data);
}
