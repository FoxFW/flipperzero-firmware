#include "int_backup.h"

#include <toolbox/tar/tar_archive.h>

#include <bt/bt_settings_filename.h>
#include <bt/bt_service/bt_keys_filename.h>
#include <desktop/desktop_settings_filename.h>
#include <notification/notification_settings_filename.h>

#define INT_BACKUP_DEFAULT_LOCATION EXT_PATH(INT_BACKUP_DEFAULT_FILENAME)

/* The "fox" service (formerly "dolphin") that used to own this filename
 * constant has been removed entirely. The string value below MUST stay
 * exactly ".dolphin.state" unchanged, since backup_name_converter() below
 * uses it to recognize this settings file inside old backups. */
#define FOX_STATE_FILE_NAME ".dolphin.state"

static void backup_name_converter(FuriString* filename) {
    if(furi_string_empty(filename) || (furi_string_get_char(filename, 0) == '.')) {
        return;
    }

    const char* const names[] = {
        BT_SETTINGS_FILE_NAME,
        BT_KEYS_STORAGE_FILE_NAME,
        DESKTOP_SETTINGS_FILE_NAME,
        NOTIFICATION_SETTINGS_FILE_NAME,
        FOX_STATE_FILE_NAME,
    };

    for(size_t i = 0; i < COUNT_OF(names); i++) {
        if(furi_string_equal(filename, &names[i][1])) {
            furi_string_set(filename, names[i]);
            return;
        }
    }
}

bool int_backup_create(Storage* storage, const char* destination) {
    const char* final_destination =
        destination && strlen(destination) ? destination : INT_BACKUP_DEFAULT_LOCATION;
    return storage_int_backup(storage, final_destination) == FSE_OK;
}

bool int_backup_exists(Storage* storage, const char* source) {
    const char* final_source = source && strlen(source) ? source : INT_BACKUP_DEFAULT_LOCATION;
    return storage_common_stat(storage, final_source, NULL) == FSE_OK;
}

bool int_backup_unpack(Storage* storage, const char* source) {
    const char* final_source = source && strlen(source) ? source : INT_BACKUP_DEFAULT_LOCATION;
    return storage_int_restore(storage, final_source, backup_name_converter) == FSE_OK;
}
