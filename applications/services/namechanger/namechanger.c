#include "namechanger.h"
#include <furi_hal.h>
#include <furi_hal_version.h>
#include <cli/cli_vcp.h>
#include <bt/bt_service/bt.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#define TAG "NameChanger"

static bool namechanger_init() {
    Storage* storage = furi_record_open(RECORD_STORAGE);

    uint8_t timeout = 0;
    while(timeout < 11) {
        if(storage_sd_status(storage) == FSE_OK) break;
        furi_delay_ms(250);
        timeout++;

    }

    if(storage_file_exists(storage, EXT_PATH("dolphin/name.settings")) &&
       !storage_file_exists(storage, NAMECHANGER_PATH)) {
        storage_simply_mkdir(storage, EXT_PATH("System"));
        storage_common_rename(
            storage, EXT_PATH("dolphin/name.settings"), NAMECHANGER_PATH);
    }
    storage_simply_remove(storage, EXT_PATH("dolphin/name.settings"));
    storage_simply_remove(storage, EXT_PATH("dolphin"));
    storage_simply_remove(storage, EXT_PATH("apps_data/fox_setup/name.pending"));

    FuriString* str = furi_string_alloc();
    FlipperFormat* file = flipper_format_file_alloc(storage);

    bool res = false;

    do {
        uint32_t version;
        if(!flipper_format_file_open_existing(file, NAMECHANGER_PATH)) break;
        if(!flipper_format_read_header(file, str, &version)) break;
        if(furi_string_cmp_str(str, NAMECHANGER_HEADER)) break;
        if(version != NAMECHANGER_VERSION) break;

        if(!flipper_format_read_string(file, "Name", str)) break;

        size_t temp_string_size = furi_string_size(str);
        if(temp_string_size > (size_t)8) break;
        if(temp_string_size < (size_t)2) break;

        const char* name_ptr = furi_string_get_cstr(str);
        bool chars_check_failed = false;

        for(; *name_ptr; ++name_ptr) {
            const char c = *name_ptr;
            if((c < '0' || c > '9') && (c < 'A' || c > 'Z') && (c < 'a' || c > 'z')) {
                chars_check_failed = true;
                break;
            }
        }

        if(chars_check_failed) break;

        version_set_custom_name(NULL, strdup(furi_string_get_cstr(str)));
        furi_hal_version_set_name(version_get_custom_name(NULL));

        res = true;
    } while(false);

    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);
    furi_string_free(str);

    return res;
}

int32_t namechanger_on_system_start(void* p) {
    UNUSED(p);
    if(furi_hal_rtc_get_boot_mode() != FuriHalRtcBootModeNormal) {
        return 0;
    }

    uint8_t timeout = 0;
    while(!furi_record_exists(RECORD_CLI_VCP) || !furi_record_exists(RECORD_BT) ||
          !furi_record_exists(RECORD_STORAGE)) {
        timeout++;
        if(timeout > 250) {
            return 0;
        }
        furi_delay_ms(5);
    }

    if(namechanger_init()) {
        /* cli_vcp already brought USB up with the FACTORY name (storage wasn't mounted
         * yet), so the CDC serial is flip_<factory>. Rebuild it now with the custom name
         * while the boot gate still holds the pullup down, so the host's FIRST enumeration
         * is flip_<customname>. Without this, boot enumerates the factory-name serial (one
         * COM) and the first USB reseat rebuilds it as the custom-name serial (a different
         * COM) -- the COM drift seen only on renamed units. reinit respects the boot gate
         * (USB stays down until desktop releases it), so nothing enumerates early. */
        furi_hal_usb_reinit();

        Bt* bt = furi_record_open(RECORD_BT);
        if(!bt_profile_restore_default(bt)) {

        }
        furi_record_close(RECORD_BT);
        bt = NULL;
        furi_delay_ms(3);
    }

    return 0;
}
