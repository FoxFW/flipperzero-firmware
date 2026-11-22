#include "saflok_app_i.h"
#include "saflok_field_helpers.h"
#include "saflok_encryption_helpers.h"
#include "../views/field_list.h"
#include <furi.h>
#include <furi/core/log.h>
#include <stdint.h>
#include <stddef.h>

bool saflok_validate_checksum(SaflokData* data) {
    uint8_t checksum = 0;
    for (int i = 0; i < 16; i++) {
        checksum += data->packed_data[i];
    }
    checksum = 255 - (checksum & 0xFF);
    FURI_LOG_D("Saflok", "Validating checksum:");
    FURI_LOG_D("Saflok", "                    checksum = %d", checksum);
    FURI_LOG_D("Saflok", "                    saflok_data->security.checksum = %d", data->security.checksum);
    FURI_LOG_D("Saflok", "                    saflok_data->packed_data[16] = %d", data->packed_data[16]);
    return (checksum == data->packed_data[16]) && (data->packed_data[16] == data->security.checksum);
}

void saflok_update_checksum(SaflokApp* app) {
    uint8_t checksum = 0;
    for (int i = 0; i < 16; i++) {
        checksum += app->saflok_data->packed_data[i];
    }
    checksum = 255 - (checksum & 0xFF);
    app->saflok_data->packed_data[16] = checksum;
    app->saflok_data->security.checksum = checksum;
    app->changed->checksum = false;

}

uint32_t calculate_mf_classic_checksum(const MfClassicData* mf_data) {
    uint32_t checksum = 0;

    checksum += mf_data->type;

    for(size_t i = 0; i < mf_data->iso14443_3a_data->uid_len; i++) {
        checksum += mf_data->iso14443_3a_data->uid[i];
    }
    checksum += mf_data->iso14443_3a_data->atqa[0];
    checksum += mf_data->iso14443_3a_data->atqa[1];
    checksum += mf_data->iso14443_3a_data->sak;

    for(size_t i = 0; i < 8; i++) {
        checksum += mf_data->block_read_mask[i];
    }

    checksum += (uint32_t)(mf_data->key_a_mask & 0xFFFFFFFF);
    checksum += (uint32_t)(mf_data->key_a_mask >> 32);
    checksum += (uint32_t)(mf_data->key_b_mask & 0xFFFFFFFF);
    checksum += (uint32_t)(mf_data->key_b_mask >> 32);

    size_t total_blocks;
    switch(mf_data->type) {
        case MfClassicTypeMini:
            total_blocks = 20;
            break;
        case MfClassicType1k:
            total_blocks = 64;
            break;
        case MfClassicType4k:
            total_blocks = 256;
            break;
        default:
            total_blocks = 0;
            break;
    }

    for(size_t block = 0; block < total_blocks; block++) {
        for(size_t byte = 0; byte < 16; byte++) {
            checksum += mf_data->block[block].data[byte];
        }
    }

    return checksum;
}

bool saflok_unpack_data(uint8_t* packed_data, SaflokApp* app) {
    if(!packed_data || !app) return false;

    if(!saflok_unpack_sub_function(packed_data, app)           ||
            !saflok_unpack_key_level(packed_data, app)         ||
            !saflok_unpack_led_warning(packed_data, app)       ||
            !saflok_unpack_key_id(packed_data, app)            ||
            !saflok_unpack_key_record(packed_data, app)        ||
            !saflok_unpack_opening_key(packed_data, app)       ||
            !saflok_unpack_sequence_combo(packed_data, app)    ||
            !saflok_unpack_pass_levels(packed_data, app)       ||
            !saflok_unpack_override_deadbolt(packed_data, app) ||
            !saflok_unpack_restrictions(packed_data, app)      ||
            !saflok_unpack_creation_date(packed_data, app)     ||
            !saflok_unpack_expiration_date(packed_data, app)   ||
            !saflok_unpack_property_number(packed_data, app)   ||
            !saflok_unpack_checksum(packed_data, app)) {
        return false;
    }

    return true;
}

void format_field_value(SaflokApp* app, SaflokData* data, uint8_t index, char* buffer, size_t buffer_size) {
    switch(index) {
        case 0:
            snprintf(buffer, buffer_size, "%s", data->access_control.key_level.name);
            break;
        case 1:
            snprintf(buffer, buffer_size, "%s", data->access_control.sub_function.name);
            break;
        case 2:
            if(app->display_settings->key_id_hex_format) {
                snprintf(buffer, buffer_size, "%02X", data->identification.key_id);
            } else {
                snprintf(buffer, buffer_size, "%02d", data->identification.key_id);
            }
            break;
        case 3:
            snprintf(buffer, buffer_size, "%s", data->access_control.led_warning ? "Yes" : "No");
            break;
        case 4:
            if(app->display_settings->key_record_hex_format) {
                snprintf(buffer, buffer_size, "%04X", data->identification.key_record);
            } else {
                snprintf(buffer, buffer_size, "%d", data->identification.key_record);
            }
            break;
        case 5:
            snprintf(buffer, buffer_size, "PASS_LEVELS_PLACEHOLDER");
            break;
        case 6:
            snprintf(buffer, buffer_size, "%s", data->access_control.opening_key ? "Yes" : "No");
            break;
        case 7:
            if(app->display_settings->sequence_combo_hex_format) {
                snprintf(buffer, buffer_size, "%04X", data->security.sequence_combination);
            } else {
                snprintf(buffer, buffer_size, "%d", data->security.sequence_combination);
            }
            break;
        case 8:
            snprintf(buffer, buffer_size, "%s", data->access_control.override_deadbolt ? "Yes" : "No");
            break;
        case 9: {
            FuriString* abbrev = furi_string_alloc();
            abbreviate_days(
                furi_string_get_cstr(data->time_control.restrictions.restricted_weekday_string),
                abbrev);
            snprintf(buffer, buffer_size, "%s", furi_string_get_cstr(abbrev));
            furi_string_free(abbrev);
            break;
        }
        case 10:
            snprintf(
                buffer,
                buffer_size,
                "%04d-%02d-%02d",
                data->time_control.creation_date.year,
                data->time_control.creation_date.month,
                data->time_control.creation_date.day);
            break;
        case 11:
            snprintf(
                buffer,
                buffer_size,
                "%04d-%02d-%02d",
                data->time_control.expiration_date.year,
                data->time_control.expiration_date.month,
                data->time_control.expiration_date.day);
            break;
        case 12:
            if(app->display_settings->property_number_hex_format) {
                snprintf(buffer, buffer_size, "%04X", data->identification.property_number);
            } else {
                snprintf(buffer, buffer_size, "%04d", data->identification.property_number);
            }
            break;
        case 13:
            snprintf(buffer, buffer_size, "%s", saflok_validate_checksum(data) ? "Yes" : "No");
            break;
        default:
            snprintf(buffer, buffer_size, " ");
            break;
    }
}

void saflok_update_nfc_device(SaflokApp* app) {
    SaflokMfClassicData* device_data = (SaflokMfClassicData*)nfc_device_get_data(app->saflok_device, NfcProtocolMfClassic);

    if(has_unsaved_changes(app)) {
        save_changes(app);
    }

    uint8_t encrypted[SAFLOK_DATA_LENGTH];
    saflok_encrypt(app->saflok_data->packed_data, encrypted);
    memcpy(device_data->block[1].data, encrypted, 16);
    device_data->block[2].data[0] = encrypted[16];

    device_data->iso14443_3a_data->atqa[0] = 0x04;
    device_data->iso14443_3a_data->atqa[1] = 0x00;
    device_data->iso14443_3a_data->sak     = 0x08;
    device_data->type = MfClassicType1k;

    for(int i = 0; i < MF_CLASSIC_READ_MASK_SIZE; i++) {
        device_data->block_read_mask[i] = 0xFFFFFFFF;
    }

    nfc_device_set_data(app->saflok_device, NfcProtocolMfClassic, device_data);
}
