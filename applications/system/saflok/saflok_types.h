#ifndef SAFLOK_TYPES_H
#define SAFLOK_TYPES_H

#include <stdint.h>
#include <gui/view.h>
#include <furi/core/timer.h>
#include <furi/core/string.h>

#define SAFLOK_DATA_LENGTH 17

#define MIN_VALID_YEAR      1980
#define MAX_VALID_YEAR      2099
#define MIN_SUB_FUNCTION    0
#define MAX_SUB_FUNCTION    7
#define MIN_KEY_LEVEL       1
#define MAX_KEY_LEVEL       16
#define MIN_PROPERTY_NUMBER 1
#define MAX_PROPERTY_NUMBER 4095
#define MIN_SEQUENCE_COMBO  1
#define MAX_SEQUENCE_COMBO  0x0FFF
#define MIN_KEY_RECORD      1
#define MAX_KEY_RECORD      0x7FFF
#define MIN_KEY_ID          0
#define MAX_KEY_ID          255
#define MIN_PASS_LEVEL      0
#define MAX_PASS_LEVEL      0xFFFF
#define MIN_RESTRICTIONS    0
#define MAX_RESTRICTIONS    127

typedef void (*NamingPromptCallback)(void* context, int choice);
typedef void (*FieldListEnterCallback)(void* context, uint8_t index);
typedef void (*PopupCompletionCallback)(void* context);

typedef struct SaflokApp SaflokApp;

typedef struct SaflokDay {
    const char* full_name;
    const char* abbreviation;
    bool enabled;
} SaflokDay;

typedef struct SaflokKeyLevel {
    const char* name;
    uint8_t value;
} SaflokKeyLevel;

typedef struct SaflokSubFunction {
    char* name;
    uint8_t value;
} SaflokSubFunction;

typedef struct SaflokDateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
} SaflokDateTime;

typedef struct DisplaySettings {
    bool key_id_hex_format;
    bool key_record_hex_format;
    bool sequence_combo_hex_format;
    bool property_number_hex_format;
    bool show_advanced_fields;
    uint8_t logging_level;
    bool debug_mode;
} DisplaySettings;

typedef enum DayIndex {
    MONDAY,
    TUESDAY,
    WEDNESDAY,
    THURSDAY,
    FRIDAY,
    SATURDAY,
    SUNDAY,
    ALL,
    WEEKDAYS,
    WEEKENDS,
    NONE,
    DAY_COUNT
} DayIndex;

typedef struct Restrictions {
    union {
        SaflokDay day_array[DAY_COUNT];

        struct {
            SaflokDay monday;
            SaflokDay tuesday;
            SaflokDay wednesday;
            SaflokDay thursday;
            SaflokDay friday;
            SaflokDay saturday;
            SaflokDay sunday;
            SaflokDay all;
            SaflokDay week_days;
            SaflokDay week_ends;
            SaflokDay none;
        };
    };

    FuriString* restricted_weekday_string;
} Restrictions;

typedef struct SaflokData {
    uint8_t packed_data[SAFLOK_DATA_LENGTH];
    SaflokApp* app_context;

    struct {
        SaflokKeyLevel key_level;
        uint8_t pass_levels[12];
        SaflokSubFunction sub_function;
        bool led_warning;
        bool opening_key;
        bool override_deadbolt;
    } access_control;

    struct {
        uint8_t key_id;
        uint16_t key_record;
        uint16_t property_number;
    } identification;

    struct {
        uint16_t sequence_combination;
        uint8_t checksum;
    } security;

    struct {
        SaflokDateTime creation_date;
        SaflokDateTime expiration_date;
        Restrictions restrictions;
    } time_control;
} SaflokData;

typedef enum {
    SAFLOK_SUCCESS = 0,
    SAFLOK_ERROR_NULL_POINTER,
    SAFLOK_ERROR_INVALID_DATA,
    SAFLOK_ERROR_MALLOC_FAILED,
    SAFLOK_ERROR_DECRYPT_FAILED,
    SAFLOK_ERROR_ENCRYPT_FAILED,
    SAFLOK_ERROR_INVALID_CHECKSUM,
    SAFLOK_ERROR_INVALID_DATE,
    SAFLOK_ERROR_INVALID_FIELD,
    SAFLOK_ERROR_INVALID_LENGTH,
    SAFLOK_ERROR_INVALID_FORMAT,
    SAFLOK_ERROR_FILE_ACCESS,
    SAFLOK_ERROR_NFC_READ,
    SAFLOK_ERROR_NFC_WRITE,
} SaflokError;

typedef enum {
    SAFLOK_DATE_CREATION,
    SAFLOK_DATE_EXPIRATION
} SaflokDateType;

bool is_valid_date(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute);
int days_in_month(uint8_t month, uint16_t year);
void format_date_string(SaflokDateTime date, char* buffer, size_t buffer_size);

extern SaflokError saflok_last_error;
const char* saflok_error_to_string(SaflokError error);

#endif
