#include "saflok_app_i.h"
#include <nfc/protocols/mf_classic/mf_classic_poller_sync.h>
#include <bit_lib.h>

#include "helpers/saflok_field_helpers.h"
#include "helpers/saflok_app_helpers.h"
#include "helpers/saflok_encryption_helpers.h"

typedef struct MfClassicKeyPair {
    uint64_t a;
    uint64_t b;
} MfClassicKeyPair;

PromptResponse saflok_device_load_from_path(SaflokApp* app, FuriString* file_path) {
    furi_assert(app);
    furi_assert(app->saflok_device);
    furi_assert(file_path);

    PromptResponse result = PromptResponseSuccess;

    do {
        if(!nfc_device_load(app->saflok_device, furi_string_get_cstr(file_path))) {
            result = PromptResponseFailure;
            dialog_message_show_storage_error(app->dialogs, "Cannot load\nkey file");
            break;
        }

        if(nfc_device_get_protocol(app->saflok_device) != NfcProtocolMfClassic) {
            result = PromptResponseNotMfClassic;
            break;
        }

        app->mf_classic_data = (SaflokMfClassicData*)nfc_device_get_data(app->saflok_device, NfcProtocolMfClassic);
        app->mf_classic_data->type = MfClassicType1k;

        app->original_file_checksum = calculate_mf_classic_checksum((MfClassicData*)app->mf_classic_data);

		uint8_t encrypted_data[17];
		memcpy(encrypted_data, app->mf_classic_data->block[1].data, 16);
		encrypted_data[16] = app->mf_classic_data->block[2].data[0];

		decrypt_data(encrypted_data, app->saflok_data->packed_data);

        if(!saflok_unpack_data(app->saflok_data->packed_data, app)) {
            result = PromptResponseFailure;
            break;
        }

        app->changed = malloc(sizeof(SaflokChanges));
    } while(false);

    return result;
}

bool saflok_device_save_to_path(SaflokApp* app, const char* path) {
    furi_assert(app);
    furi_assert(path);

    app->saflok_device = nfc_device_alloc();
    bool success = false;

    do {
        nfc_device_set_data(app->saflok_device, NfcProtocolMfClassic, app->mf_classic_data);
        
        if(!nfc_device_save(app->saflok_device, path)) {
            dialog_message_show_storage_error(app->dialogs, "Cannot save\nkey file");
            break;
        }

        success = true;
    } while(false);

    return success;
}

void saflok_device_get_shadow_path(FuriString* file_path, FuriString* shadow_file_path) {
    furi_assert(file_path);
    furi_assert(shadow_file_path);

    furi_string_set_n(shadow_file_path, file_path, 0, furi_string_size(file_path) - 4);
    furi_string_cat_printf(shadow_file_path, "%s", SAFLOK_APP_SHADOW_EXTENSION);
}

bool saflok_device_has_shadow(SaflokApp* app, FuriString* file_path) {
    FuriString* shadow_file_path = furi_string_alloc();
    saflok_device_get_shadow_path(file_path, shadow_file_path);
    bool has_shadow = storage_common_exists(app->storage, furi_string_get_cstr(shadow_file_path));
    furi_string_free(shadow_file_path);
    return has_shadow;
}

void generate_saflok_key(const uint8_t* uid, uint8_t* key) {
    static const uint8_t magic_table[192] = {
        0x00, 0x00, 0xAA, 0x00, 0x00, 0x00, 0xF0, 0x57, 0xB3, 0x9E, 0xE3, 0xD8,
        0x00, 0x00, 0xAA, 0x00, 0x00, 0x00, 0x96, 0x9D, 0x95, 0x4A, 0xC1, 0x57,
        0x00, 0x00, 0xAA, 0x00, 0x00, 0x00, 0x8F, 0x43, 0x58, 0x0D, 0x2C, 0x9D,
        0x00, 0x00, 0xAA, 0x00, 0x00, 0x00, 0xFF, 0xCC, 0xE0, 0x05, 0x0C, 0x43,
        0x00, 0x00, 0xAA, 0x00, 0x00, 0x00, 0x34, 0x1B, 0x15, 0xA6, 0x90, 0xCC,
        0x00, 0x00, 0xAA, 0x00, 0x00, 0x00, 0x89, 0x58, 0x56, 0x12, 0xE7, 0x1B,
        0x00, 0x00, 0xAA, 0x00, 0x00, 0x00, 0xBB, 0x74, 0xB0, 0x95, 0x36, 0x58,
        0x00, 0x00, 0xAA, 0x00, 0x00, 0x00, 0xFB, 0x97, 0xF8, 0x4B, 0x5B, 0x74,
        0x00, 0x00, 0xAA, 0x00, 0x00, 0x00, 0xC9, 0xD1, 0x88, 0x35, 0x9F, 0x92,
        0x00, 0x00, 0xAA, 0x00, 0x00, 0x00, 0x8F, 0x92, 0xE9, 0x7F, 0x58, 0x97,
        0x00, 0x00, 0xAA, 0x00, 0x00, 0x00, 0x16, 0x6C, 0xA2, 0xB0, 0x9F, 0xD1,
        0x00, 0x00, 0xAA, 0x00, 0x00, 0x00, 0x27, 0xDD, 0x93, 0x10, 0x1C, 0x6C,
        0x00, 0x00, 0xAA, 0x00, 0x00, 0x00, 0xDA, 0x3E, 0x3F, 0xD6, 0x49, 0xDD,
        0x00, 0x00, 0xAA, 0x00, 0x00, 0x00, 0x58, 0xDD, 0xED, 0x07, 0x8E, 0x3E,
        0x00, 0x00, 0xAA, 0x00, 0x00, 0x00, 0x5C, 0xD0, 0x05, 0xCF, 0xD9, 0x07,
        0x00, 0x00, 0xAA, 0x00, 0x00, 0x00, 0x11, 0x8D, 0xD0, 0x01, 0x87, 0xD0};

    uint8_t magic_byte = (uid[3] >> 4) + (uid[2] >> 4) + (uid[0] & 0x0F);
    uint8_t magickal_index = (magic_byte & 0x0F) * 12 + 11;

    uint8_t temp_key[6] = {magic_byte, uid[0], uid[1], uid[2], uid[3], magic_byte};
    uint8_t carry_sum = 0;

    for(int i = 6 - 1; i >= 0; i--, magickal_index--) {
        uint16_t keysum = temp_key[i] + magic_table[magickal_index] + carry_sum;
        temp_key[i] = (keysum & 0xFF);
        carry_sum = keysum >> 8;
    }

    memcpy(key, temp_key, 6);
}

bool saflok_device_read(SaflokApp* app) {
    const char* TAG = "saflok_device_read";
    FURI_LOG_D(TAG, "Entering Saflok KDF");

    furi_assert(app->nfc);
    furi_assert(app->saflok_device);
    furi_assert(app->mf_classic_data);
    
    bool is_read = false;

    MfClassicKeyPair saflok_1k_keys[] = {
        {.a = 0x000000000000, .b = 0xffffffffffff},
        {.a = 0x2a2c13cc242a, .b = 0xffffffffffff},
        {.a = 0xffffffffffff, .b = 0xffffffffffff},
        {.a = 0xffffffffffff, .b = 0xffffffffffff},
        {.a = 0x000000000000, .b = 0xffffffffffff},
        {.a = 0x000000000000, .b = 0xffffffffffff},
        {.a = 0x000000000000, .b = 0xffffffffffff},
        {.a = 0x000000000000, .b = 0xffffffffffff},
        {.a = 0x000000000000, .b = 0xffffffffffff},
        {.a = 0x000000000000, .b = 0xffffffffffff},
        {.a = 0x000000000000, .b = 0xffffffffffff},
        {.a = 0x000000000000, .b = 0xffffffffffff},
        {.a = 0x000000000000, .b = 0xffffffffffff},
        {.a = 0x000000000000, .b = 0xffffffffffff},
        {.a = 0x000000000000, .b = 0xffffffffffff},
        {.a = 0x000000000000, .b = 0xffffffffffff},
    };

    nfc_device_copy_data(app->saflok_device, NfcProtocolMfClassic, (MfClassicData*)app->mf_classic_data);

    do {
        MfClassicType type = MfClassicType1k;
        MfClassicError error = mf_classic_poller_sync_detect_type(app->nfc, &type);
        if(error != MfClassicErrorNone) break;
        app->mf_classic_data->type = type;

        size_t uid_len;
        const uint8_t* uid = mf_classic_get_uid((MfClassicData*)app->mf_classic_data, &uid_len);
        FURI_LOG_D(TAG, "Saflok: UID identified: %02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
        if(uid_len != 4) break;

        uint8_t key[6];
        generate_saflok_key(uid, key);
        uint64_t num_key = bit_lib_bytes_to_num_be(key, 6);
        FURI_LOG_D(TAG, "Saflok: Key generated for UID: %012llX", num_key);

        for(size_t i = 0; i < mf_classic_get_total_sectors_num(app->mf_classic_data->type); i++) {
            if(saflok_1k_keys[i].a == 0x000000000000) {
                saflok_1k_keys[i].a = num_key;
            }
        }

        MfClassicDeviceKeys keys = {};
        for(size_t i = 0; i < mf_classic_get_total_sectors_num(app->mf_classic_data->type); i++) {
            bit_lib_num_to_bytes_be(saflok_1k_keys[i].a, sizeof(MfClassicKey), keys.key_a[i].data);
            FURI_BIT_SET(keys.key_a_mask, i);
            bit_lib_num_to_bytes_be(saflok_1k_keys[i].b, sizeof(MfClassicKey), keys.key_b[i].data);
            FURI_BIT_SET(keys.key_b_mask, i);
        }

        error = mf_classic_poller_sync_read(app->nfc, &keys, (MfClassicData*)app->mf_classic_data);
        if(error == MfClassicErrorNotPresent) {
            FURI_LOG_W(TAG, "Failed to read data");
            is_read = false;
            break;
        }

        nfc_device_set_data(app->saflok_device, NfcProtocolMfClassic, (MfClassicData*)app->mf_classic_data);

		uint8_t encrypted_data[17];
		memcpy(encrypted_data, app->mf_classic_data->block[1].data, 16);
		encrypted_data[16] = app->mf_classic_data->block[2].data[0];

		decrypt_data(encrypted_data, app->saflok_data->packed_data);

        if(!saflok_unpack_data(app->saflok_data->packed_data, app)) {
            FURI_LOG_W(TAG, "Failed to unpack data");
            is_read = false;
            break;
        }
        is_read = (error == MfClassicErrorNone);
    } while(false);

    return is_read;
}
