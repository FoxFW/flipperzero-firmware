#include "pin_code.h"
#include <furi_hal_rtc.h>
#include <furi_hal_power.h>
#include <furi_hal_usb.h>
#include <furi.h>
#include <furi_hal_version.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FOX_RECOVERY_PRIME_MULTIPLIER 7
#define FOX_RECOVERY_VERIFICATION_KEY 0xABCD1234
#define FOX_ESCROW_PATH "/int/Fox.esc"
#define FOX_PIN_PATH    "/int/Fox.key"
#define XOR_KEY 0xAD

static const NotificationSequence sequence_pin_fail = {
    &message_display_backlight_on,
    &message_red_255,
    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,
    &message_red_0,
    &message_delay_250,
    &message_red_255,
    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,
    &message_red_0,
    NULL,
};

static char internal_secure_hash[DESKTOP_PIN_DATA_LEN] = {0};
static uint8_t internal_stored_length = 0;
static bool internal_is_provisioned = false;

void __attribute__((unused)) fox_touch_unused_sequences(void) {
    (void)sequence_pin_fail;
}

static void desktop_pin_code_save_to_storage(void) {

}

static bool fox_settings_read_from(Storage* storage, const char* path, FoxSettingsData* out);

void desktop_pin_code_load_from_storage(void) {

    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    FoxSettingsData s;
    bool ok = fox_settings_read_from(storage, FOX_SETTINGS_INT_PATH, &s);
    if(!ok) ok = fox_settings_read_from(storage, FOX_SETTINGS_EXT_PATH, &s);
    if(ok && s.pin_length > 0 && s.pin_length <= DESKTOP_PIN_DATA_LEN) {
        memset(internal_secure_hash, 0, sizeof(internal_secure_hash));
        memcpy(internal_secure_hash, s.pin_hash, s.pin_length);
        internal_stored_length = s.pin_length;
        internal_is_provisioned = true;
    }
    furi_record_close(RECORD_STORAGE);
}

bool desktop_pin_code_is_set(void) {
    return internal_is_provisioned;
}

void desktop_pin_code_set(const DesktopPinCode* pin_code) {
    furi_check(pin_code);
    memset(internal_secure_hash, 0, sizeof(internal_secure_hash));
    memcpy(internal_secure_hash, pin_code->data, pin_code->length);
    internal_stored_length = pin_code->length;
    internal_is_provisioned = true;

    desktop_pin_code_save_to_storage();

    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    storage_common_remove(storage, FOX_ESCROW_PATH);
    furi_record_close(RECORD_STORAGE);

    fox_recovery_generate_file(0);

    fox_settings_write();
}

void desktop_pin_code_reset(void) {
    memset(internal_secure_hash, 0, sizeof(internal_secure_hash));
    internal_stored_length = 0;
    internal_is_provisioned = false;
    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    storage_common_remove(storage, FOX_ESCROW_PATH);
    storage_common_remove(storage, FOX_PIN_PATH);
    furi_record_close(RECORD_STORAGE);
    fox_settings_write();
}

bool desktop_pin_code_check(const DesktopPinCode* pin_code) {
    furi_check(pin_code);
    if(!internal_is_provisioned) return true;
    if(pin_code->length != internal_stored_length) return false;
    return (memcmp(internal_secure_hash, pin_code->data, pin_code->length) == 0);
}

bool desktop_pin_code_is_equal(const DesktopPinCode* pin_code1, const DesktopPinCode* pin_code2) {
    furi_check(pin_code1);
    furi_check(pin_code2);
    if(pin_code1->length != pin_code2->length) return false;
    return (memcmp(pin_code1->data, pin_code2->data, pin_code1->length) == 0);
}

void desktop_pin_lock_error_notify(void) {
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notification, &sequence_pin_fail);
    furi_record_close(RECORD_NOTIFICATION);
}

uint32_t desktop_pin_lock_get_fail_timeout(void) {
    uint32_t fails = furi_hal_rtc_get_pin_fails();
    if(fails < 3) return 0;
    if(fails < 5) return 15 * 1000;
    if(fails < 7) return 30 * 1000;
    if(fails < 10) return 300 * 1000;
    return 1800 * 1000;
}

bool fox_recovery_generate_file(uint8_t current_attempts) {

    UNUSED(current_attempts);
    fox_settings_write();
    return true;
}

bool fox_recovery_check_and_reset(void) {

    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    FoxSettingsData s;
    bool reset_triggered = false;

    if(fox_settings_read_from(storage, FOX_RECOVERY_FILE_PATH, &s) &&
       s.recovery_verify_key == FOX_RECOVERY_VERIFICATION_KEY) {

        const char* dev_name = furi_hal_version_get_name_ptr();
        if(dev_name && strcmp((char*)s.device_name, dev_name) == 0 &&
           s.recovery_attempts_prime == 0) {

            char token[FOX_TOKEN_SIZE];
            memcpy(token, s.recovery_token, FOX_TOKEN_SIZE);
            token[FOX_TOKEN_SIZE - 1] = '\0';
            if(token[0] != '\0') {
                if(fox_recovery_validate_and_register_token(token)) {
                    reset_triggered = true;
                }
            }
        }
    }

    furi_record_close(RECORD_STORAGE);
    return reset_triggered;
}

bool fox_escrow_load_and_verify(FoxEscrowData* escrow_out) {
    furi_check(escrow_out);
    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool success = false;

    if(storage_file_open(file, FOX_ESCROW_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FoxEscrowData data;
        if(storage_file_read(file, &data, sizeof(FoxEscrowData)) == sizeof(FoxEscrowData)) {
            uint8_t* raw = (uint8_t*)&data;
            for(size_t i = 0; i < sizeof(FoxEscrowData); i++) raw[i] ^= XOR_KEY;
            uint8_t dev_id[16] = {0};
            const char* dev_name = furi_hal_version_get_name_ptr();
            if(dev_name) strncpy((char*)dev_id, dev_name, 15);
            if(memcmp(data.hardware_uid_hash, dev_id, 16) == 0) {
                *escrow_out = data;
                success = true;
            }
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return success;
}

bool fox_escrow_save_state(const FoxEscrowData* escrow_in) {
    furi_check(escrow_in);
    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool success = false;

    FoxEscrowData data = *escrow_in;
    memset(data.hardware_uid_hash, 0, sizeof(data.hardware_uid_hash));
    const char* dev_name = furi_hal_version_get_name_ptr();
    if(dev_name) strncpy((char*)data.hardware_uid_hash, dev_name, 15);
    uint8_t* raw = (uint8_t*)&data;
    for(size_t i = 0; i < sizeof(FoxEscrowData); i++) raw[i] ^= XOR_KEY;

    if(storage_file_open(file, FOX_ESCROW_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        if(storage_file_write(file, &data, sizeof(FoxEscrowData)) == sizeof(FoxEscrowData)) {
            success = true;
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return success;
}

void fox_escrow_trigger_wiper_screen(void) {
    FoxEscrowData escrow;
    memset(&escrow, 0, sizeof(FoxEscrowData));
    fox_escrow_load_and_verify(&escrow);
    escrow.active_fail_count = 0xFF;
    fox_escrow_save_state(&escrow);
}

static void fox_pin_write_lockout_flag(void) {
    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    File* f = storage_file_alloc(storage);
    if(storage_file_open(f, FOX_LOCKOUT_FLAG_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(f, "1", 1);
        storage_file_close(f);
    }
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
}

static void fox_clear_dir_recursive(Storage* storage, const char* path) {
    File* dir = storage_file_alloc(storage);
    if(!storage_dir_open(dir, path)) {
        storage_file_free(dir);
        return;
    }

    FileInfo* fi   = malloc(sizeof(FileInfo));
    char*     name = malloc(256);
    char*     full = malloc(768);

    if(fi && name && full) {
        while(storage_dir_read(dir, fi, name, 256)) {
            if(strnlen(name, 1) == 0) break;
            snprintf(full, 768, "%s/%s", path, name);
            if(fi->flags & FSF_DIRECTORY) {
                fox_clear_dir_recursive(storage, full);
                storage_common_remove(storage, full);
            } else if(strcmp(full, FOX_SETTINGS_EXT_PATH) != 0) {

                storage_common_remove(storage, full);
            }
        }
    }

    free(full);
    free(name);
    free(fi);
    storage_dir_close(dir);
    storage_file_free(dir);
}

static void fox_full_wipe_sd(Storage* storage) {

    File* root = storage_file_alloc(storage);
    if(!storage_dir_open(root, "/ext")) {
        storage_file_free(root);
        return;
    }
    FileInfo fi;
    char entry[256];
    char full[512];
    while(storage_dir_read(root, &fi, entry, sizeof(entry))) {
        if(strnlen(entry, 1) == 0) break;
        snprintf(full, sizeof(full), "/ext/%s", entry);
        if(fi.flags & FSF_DIRECTORY) {
            fox_clear_dir_recursive(storage, full);
            storage_common_remove(storage, full);
        } else {
            storage_common_remove(storage, full);
        }
    }
    storage_dir_close(root);
    storage_file_free(root);
}

void fox_escrow_execute_wipe(void) {

    fox_pin_write_lockout_flag();

    if(furi_hal_usb_get_config() != NULL) {
        furi_hal_usb_set_config(NULL, NULL);
    }

    FoxEscrowData escrow;
    memset(&escrow, 0, sizeof(FoxEscrowData));
    fox_escrow_load_and_verify(&escrow);

    if(escrow.wipe_method != 0) {
        Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
        fox_full_wipe_sd(storage);
        furi_record_close(RECORD_STORAGE);
    }

    furi_hal_power_reset();
}

bool fox_recovery_validate_and_register_token(const char* incoming_token) {
    if(incoming_token == NULL || strlen(incoming_token) == 0) {
        return false;
    }

    FoxEscrowData escrow;
    memset(&escrow, 0, sizeof(FoxEscrowData));

    if(!fox_escrow_load_and_verify(&escrow)) {
        escrow.recorded_tokens_count = 0;
    }

    for(uint8_t i = 0; i < escrow.recorded_tokens_count; i++) {
        if(strncmp(escrow.used_tokens[i], incoming_token, FOX_TOKEN_SIZE) == 0) {
            FURI_LOG_E("FoxSecurity", "Replay Protection Triggered: Token already consumed.");
            return false;
        }
    }

    if(escrow.recorded_tokens_count < FOX_ESCROW_MAX_USED_TOKENS) {
        strncpy(escrow.used_tokens[escrow.recorded_tokens_count], incoming_token, FOX_TOKEN_SIZE - 1);
        escrow.recorded_tokens_count++;
    } else {
        for(uint8_t i = 1; i < FOX_ESCROW_MAX_USED_TOKENS; i++) {
            memcpy(escrow.used_tokens[i - 1], escrow.used_tokens[i], FOX_TOKEN_SIZE);
        }
        strncpy(escrow.used_tokens[FOX_ESCROW_MAX_USED_TOKENS - 1], incoming_token, FOX_TOKEN_SIZE - 1);
    }

    return fox_escrow_save_state(&escrow);
}

static uint32_t fox_settings_checksum(const FoxSettingsData* s) {
    const uint8_t* b = (const uint8_t*)s;
    uint32_t sum = 0;
    for(size_t i = 0; i < offsetof(FoxSettingsData, checksum); i++) sum += b[i];
    return sum;
}

static void fox_settings_xor(uint8_t* buf, size_t len) {
    for(size_t i = 0; i < len; i++) buf[i] ^= FOX_SETTINGS_XOR_KEY;
}

static bool fox_settings_write_to(Storage* storage, const char* path, FoxSettingsData* s) {
    s->checksum = fox_settings_checksum(s);
    uint8_t buf[sizeof(FoxSettingsData)];
    memcpy(buf, s, sizeof(FoxSettingsData));
    fox_settings_xor(buf, sizeof(buf));

    storage_simply_mkdir(storage, "/ext/System");

    File* f = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(f, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = (storage_file_write(f, buf, sizeof(buf)) == sizeof(buf));
        storage_file_close(f);
    }
    storage_file_free(f);
    return ok;
}

static bool fox_settings_read_from(Storage* storage, const char* path, FoxSettingsData* out) {
    uint8_t buf[sizeof(FoxSettingsData)];
    File* f = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        if(storage_file_read(f, buf, sizeof(buf)) == sizeof(buf)) {
            fox_settings_xor(buf, sizeof(buf));
            FoxSettingsData tmp;
            memcpy(&tmp, buf, sizeof(tmp));
            if(tmp.magic == FOX_SETTINGS_MAGIC &&
               tmp.version == FOX_SETTINGS_VERSION &&
               fox_settings_checksum(&tmp) == tmp.checksum) {
                *out = tmp;
                ok = true;
            }
        }
        storage_file_close(f);
    }
    storage_file_free(f);
    return ok;
}

static void fox_settings_build_current(FoxSettingsData* s) {
    memset(s, 0, sizeof(FoxSettingsData));
    s->magic   = FOX_SETTINGS_MAGIC;
    s->version = FOX_SETTINGS_VERSION;

    const char* name = furi_hal_version_get_name_ptr();
    if(name) strncpy((char*)s->device_name, name, sizeof(s->device_name) - 1);

    s->pin_length = internal_stored_length;
    memcpy(s->pin_hash, internal_secure_hash, internal_stored_length);

    s->recovery_verify_key = FOX_RECOVERY_VERIFICATION_KEY;

    FoxEscrowData esc;
    memset(&esc, 0, sizeof(esc));
    if(fox_escrow_load_and_verify(&esc)) {
        s->fail_count   = esc.active_fail_count;
        s->fail_limit   = esc.wipe_limit;
        s->wipe_method  = esc.wipe_method;
        s->locked_out   = (esc.active_fail_count == 0xFF) ? 0xFF : 0;
        s->pin_exceed_action = esc.wipe_method;
        s->recovery_attempts_prime = (uint32_t)esc.active_fail_count * 7;

        memcpy(s->used_tokens, esc.used_tokens,
               sizeof(esc.used_tokens[0]) * FOX_ESCROW_MAX_USED_TOKENS);
    }
}

void fox_settings_write(void) {
    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    FoxSettingsData s;
    fox_settings_build_current(&s);
    fox_settings_write_to(storage, FOX_SETTINGS_EXT_PATH, &s);
    fox_settings_write_to(storage, FOX_SETTINGS_INT_PATH, &s);
    furi_record_close(RECORD_STORAGE);
}

bool fox_settings_read(void) {
    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    FoxSettingsData s;
    bool ok = fox_settings_read_from(storage, FOX_SETTINGS_EXT_PATH, &s);
    if(!ok) ok = fox_settings_read_from(storage, FOX_SETTINGS_INT_PATH, &s);
    furi_record_close(RECORD_STORAGE);

    return ok;
}

bool fox_settings_import_override(void) {
    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    FoxSettingsData s;
    bool imported = false;

    if(fox_settings_read_from(storage, FOX_SETTINGS_EXT_PATH, &s) &&
       s.override_flag == FOX_SETTINGS_OVERRIDE) {

        s.override_flag = 0;
        fox_settings_write_to(storage, FOX_SETTINGS_EXT_PATH, &s);
        fox_settings_write_to(storage, FOX_SETTINGS_INT_PATH, &s);

        memset(internal_secure_hash, 0, sizeof(internal_secure_hash));
        if(s.pin_length > 0 && s.pin_length <= DESKTOP_PIN_DATA_LEN - 1) {
            memcpy(internal_secure_hash, s.pin_hash, s.pin_length);
            internal_stored_length = s.pin_length;
            internal_is_provisioned = true;
            desktop_pin_code_save_to_storage();
        } else {

            internal_stored_length = 0;
            internal_is_provisioned = false;
            desktop_pin_code_save_to_storage();
        }

        imported = true;
    }

    furi_record_close(RECORD_STORAGE);
    return imported;
}

void fox_settings_update_wizard(bool complete, const char* build_hash) {
    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    FoxSettingsData s;

    if(!fox_settings_read_from(storage, FOX_SETTINGS_EXT_PATH, &s) &&
       !fox_settings_read_from(storage, FOX_SETTINGS_INT_PATH, &s)) {
        fox_settings_build_current(&s);
    }
    s.wizard_complete = complete ? 1 : 0;
    if(build_hash) strncpy((char*)s.build_hash, build_hash, sizeof(s.build_hash) - 1);
    fox_settings_write_to(storage, FOX_SETTINGS_EXT_PATH, &s);
    fox_settings_write_to(storage, FOX_SETTINGS_INT_PATH, &s);
    furi_record_close(RECORD_STORAGE);
}

void fox_settings_update_desktop_settings(uint32_t auto_lock_ms, uint8_t usb_inhibit, uint8_t exceed_action) {
    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    FoxSettingsData s;
    if(!fox_settings_read_from(storage, FOX_SETTINGS_EXT_PATH, &s) &&
       !fox_settings_read_from(storage, FOX_SETTINGS_INT_PATH, &s)) {
        fox_settings_build_current(&s);
    }
    s.auto_lock_delay_ms     = auto_lock_ms;
    s.usb_inhibit_auto_lock  = usb_inhibit;
    s.pin_exceed_action      = exceed_action;
    fox_settings_write_to(storage, FOX_SETTINGS_EXT_PATH, &s);
    fox_settings_write_to(storage, FOX_SETTINGS_INT_PATH, &s);
    furi_record_close(RECORD_STORAGE);
}

uint8_t fox_settings_get_notification_text(void) {
    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    FoxSettingsData s;
    uint8_t mode = FOX_NOTIFY_TEXT_ALL;
    if(fox_settings_read_from(storage, FOX_SETTINGS_EXT_PATH, &s) ||
       fox_settings_read_from(storage, FOX_SETTINGS_INT_PATH, &s)) {
        mode = s.reserved[0];
    }
    furi_record_close(RECORD_STORAGE);
    if(mode > FOX_NOTIFY_TEXT_NONE) mode = FOX_NOTIFY_TEXT_ALL; /* garbage -> default */
    return mode;
}

void fox_settings_set_notification_text(uint8_t mode) {
    Storage* storage = (Storage*)furi_record_open(RECORD_STORAGE);
    FoxSettingsData s;
    if(!fox_settings_read_from(storage, FOX_SETTINGS_EXT_PATH, &s) &&
       !fox_settings_read_from(storage, FOX_SETTINGS_INT_PATH, &s)) {
        fox_settings_build_current(&s);
    }
    s.reserved[0] = (mode > FOX_NOTIFY_TEXT_NONE) ? FOX_NOTIFY_TEXT_ALL : mode;
    fox_settings_write_to(storage, FOX_SETTINGS_EXT_PATH, &s);
    fox_settings_write_to(storage, FOX_SETTINGS_INT_PATH, &s);
    furi_record_close(RECORD_STORAGE);
}

#ifdef __cplusplus
}
#endif

void fox_settings_sync_int_to_sd(void) {
    Storage* st = (Storage*)furi_record_open(RECORD_STORAGE);

    if(!storage_file_exists(st, FOX_SETTINGS_EXT_PATH)) {
        storage_common_copy(st, FOX_SETTINGS_INT_PATH, FOX_SETTINGS_EXT_PATH);
    }
    furi_record_close(RECORD_STORAGE);
}

void fox_settings_sync_sd_to_int(void) {
    Storage* st = (Storage*)furi_record_open(RECORD_STORAGE);

    if(!storage_file_exists(st, FOX_SETTINGS_INT_PATH)) {
        storage_common_copy(st, FOX_SETTINGS_EXT_PATH, FOX_SETTINGS_INT_PATH);
    }
    furi_record_close(RECORD_STORAGE);
}
