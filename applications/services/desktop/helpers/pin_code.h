#pragma once

#include <stdint.h>
#include <stdbool.h>

#define DESKTOP_PIN_CODE_MIN_LEN (1)
#define DESKTOP_PIN_CODE_MAX_LEN (10)
#define FOX_RECOVERY_FILE_PATH "/ext/System/Fox.data"

#define DESKTOP_PIN_DATA_LEN     ((DESKTOP_PIN_CODE_MAX_LEN * 2) + 1)

#define FOX_SETTINGS_EXT_PATH    "/ext/System/Fox.data"
#define FOX_SETTINGS_INT_PATH    "/int/Fox.data"
#define FOX_SETTINGS_MAGIC       0x464F5853u
#define FOX_SETTINGS_VERSION     1u
#define FOX_SETTINGS_XOR_KEY     0xAD

#define FOX_SETTINGS_OVERRIDE    0xF0u

#define FOX_LOCKOUT_FLAG_PATH    "/int/Fox.lock"
#define FOX_FORMAT_FLAG_PATH     "/int/Fox.fmt"
#define FOX_PIN_FILE_MAGIC       (0xFE)

#define FOX_ESCROW_MAX_USED_TOKENS (8)
#define FOX_TOKEN_SIZE             (16)

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t  override_flag;
    uint8_t  _pad;

    uint8_t  device_name[16];

    uint8_t  pin_hash[21];
    uint8_t  pin_length;

    uint8_t  fail_count;
    uint8_t  fail_limit;
    uint8_t  wipe_method;
    uint8_t  locked_out;

    uint32_t recovery_attempts_prime;
    uint32_t recovery_verify_key;
    uint8_t  recovery_token[16];

    uint8_t  used_tokens[8][16];

    uint8_t  build_hash[65];
    uint8_t  wizard_complete;

    uint32_t auto_lock_delay_ms;
    uint8_t  usb_inhibit_auto_lock;
    uint8_t  pin_exceed_action;

    uint8_t  reserved[32];

    uint32_t checksum;
} __attribute__((packed)) FoxSettingsData;

typedef struct {
    char    data[DESKTOP_PIN_DATA_LEN];
    uint8_t length;
} DesktopPinCode;

typedef struct {
    uint8_t active_fail_count;
    uint32_t secure_session_nonce;
    uint8_t hardware_uid_hash[16];
    uint8_t wipe_limit;
    uint8_t wipe_method;
    char used_tokens[FOX_ESCROW_MAX_USED_TOKENS][FOX_TOKEN_SIZE];
    uint8_t recorded_tokens_count;
} FoxEscrowData;

typedef struct {
    uint8_t device_name[16];
    uint32_t attempts_x_prime;
    uint32_t verification_key;
    char recovery_token[FOX_TOKEN_SIZE];
} FoxRecoveryData;

#ifdef __cplusplus
extern "C" {
#endif

bool desktop_pin_code_is_set(void);
void desktop_pin_code_set(const DesktopPinCode* pin_code);
void desktop_pin_code_reset(void);
bool desktop_pin_code_check(const DesktopPinCode* pin_code);
bool desktop_pin_code_is_equal(const DesktopPinCode* pin_code1, const DesktopPinCode* pin_code2);
void desktop_pin_lock_error_notify(void);
uint32_t desktop_pin_lock_get_fail_timeout(void);
void desktop_pin_code_load_from_storage(void);

bool fox_escrow_load_and_verify(FoxEscrowData* escrow_out);
bool fox_escrow_save_state(const FoxEscrowData* escrow_in);
void fox_escrow_trigger_wiper_screen(void);
void fox_escrow_execute_wipe(void);

bool fox_recovery_check_and_reset(void);
bool fox_recovery_generate_file(uint8_t current_attempts);
bool fox_recovery_validate_and_register_token(const char* incoming_token);

void fox_settings_write(void);
bool fox_settings_read(void);
bool fox_settings_import_override(void);
void fox_settings_update_wizard(bool complete, const char* build_hash);
void fox_settings_update_desktop_settings(uint32_t auto_lock_ms, uint8_t usb_inhibit, uint8_t exceed_action);

void fox_settings_sync_int_to_sd(void);
void fox_settings_sync_sd_to_int(void);

/* Notification-text mode (stored in FoxSettingsData.reserved[0]). */
#define FOX_NOTIFY_TEXT_ALL      0u
#define FOX_NOTIFY_TEXT_CRITICAL 1u
#define FOX_NOTIFY_TEXT_NONE     2u
uint8_t fox_settings_get_notification_text(void);
void    fox_settings_set_notification_text(uint8_t mode);

#ifdef __cplusplus
}
#endif
