#include "fmh_subghz_launch.h"

#include <furi.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include <loader/loader.h>

#define FMH_SETTINGS_PATH   "/ext/subghz/assets/last_subghz.settings"
#define FMH_FOCUS_MENU_PATH "/ext/subghz/.focus_menu"
#define FMH_GARAGE_FAP_PATH "/ext/apps/Sub-GHz/subghz_garage.fap"
#define FMH_SETTINGS_TYPE   "Flipper SubGhz Last Setting File"
#define FMH_SETTINGS_VER    3

static void fmh_write_settings(Storage* storage, uint32_t frequency, uint32_t preset) {
    bool hopping = false;
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool updated = false;

    if(flipper_format_file_open_existing(ff, FMH_SETTINGS_PATH)) {
        if(flipper_format_insert_or_update_uint32(ff, "Frequency", &frequency, 1) &&
           flipper_format_insert_or_update_uint32(ff, "Preset", &preset, 1) &&
           flipper_format_insert_or_update_bool(ff, "Hopping", &hopping, 1)) {
            updated = true;
        }
    }
    flipper_format_file_close(ff);
    flipper_format_free(ff);

    if(updated) return;

    FlipperFormat* fresh = flipper_format_file_alloc(storage);
    if(flipper_format_file_open_always(fresh, FMH_SETTINGS_PATH)) {
        do {
            if(!flipper_format_write_header_cstr(fresh, FMH_SETTINGS_TYPE, FMH_SETTINGS_VER)) break;
            if(!flipper_format_write_uint32(fresh, "Frequency", &frequency, 1)) break;
            if(!flipper_format_write_uint32(fresh, "Preset", &preset, 1)) break;
            if(!flipper_format_write_bool(fresh, "Hopping", &hopping, 1)) break;
        } while(0);
    }
    flipper_format_file_close(fresh);
    flipper_format_free(fresh);
}

static void fmh_write_focus_menu(Storage* storage) {
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, FMH_FOCUS_MENU_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, "read", 4);
    }
    storage_file_close(file);
    storage_file_free(file);
}

void fmh_launch_subghz(uint32_t frequency, uint8_t preset, char launch_mode) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    fmh_write_settings(storage, frequency, (uint32_t)preset);
    if(launch_mode != 'G') {
        fmh_write_focus_menu(storage);
    }
    furi_record_close(RECORD_STORAGE);

    Loader* loader = furi_record_open(RECORD_LOADER);
    if(launch_mode == 'G') {
        loader_enqueue_launch(loader, FMH_GARAGE_FAP_PATH, "read", LoaderDeferredLaunchFlagNone);
    } else {
        loader_enqueue_launch(loader, "subghz", NULL, LoaderDeferredLaunchFlagNone);
    }
    furi_record_close(RECORD_LOADER);
}
