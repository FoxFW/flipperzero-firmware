#include "saflok_app_i.h"
#include "saflok_field_helpers.h"
#include "saflok_app_helpers.h"

#include <furi.h>
#include <furi/core/log.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef TAG
#define TAG "SaflokEncryptHelper"
#endif

static const uint8_t c_aDecode[256] = {
    0xEA, 0x0D, 0xD9, 0x74, 0x4E, 0x28, 0xFD, 0xBA, 0x7B, 0x98, 0x87, 0x78, 0xDD, 0x8D, 0xB5, 0x1A,
	0x0E, 0x30, 0xF3, 0x2F, 0x6A, 0x3B, 0xAC, 0x09, 0xB9, 0x20, 0x6E, 0x5B, 0x2B, 0xB6, 0x21, 0xAA,
	0x17, 0x44, 0x5A, 0x54, 0x57, 0xBE, 0x0A, 0x52, 0x67, 0xC9, 0x50, 0x35, 0xF5, 0x41, 0xA0, 0x94,
	0x60, 0xFE, 0x24, 0xA2, 0x36, 0xEF, 0x1E, 0x6B, 0xF7, 0x9C, 0x69, 0xDA, 0x9B, 0x6F, 0xAD, 0xD8,
	0xFB, 0x97, 0x62, 0x5F, 0x1F, 0x38, 0xC2, 0xD7, 0x71, 0x31, 0xF0, 0x13, 0xEE, 0x0F, 0xA3, 0xA7,
	0x1C, 0xD5, 0x11, 0x4C, 0x45, 0x2C, 0x04, 0xDB, 0xA6, 0x2E, 0xF8, 0x64, 0x9A, 0xB8, 0x53, 0x66,
	0xDC, 0x7A, 0x5D, 0x03, 0x07, 0x80, 0x37, 0xFF, 0xFC, 0x06, 0xBC, 0x26, 0xC0, 0x95, 0x4A, 0xF1,
	0x51, 0x2D, 0x22, 0x18, 0x01, 0x79, 0x5E, 0x76, 0x1D, 0x7F, 0x14, 0xE3, 0x9E, 0x8A, 0xBB, 0x34,
	0xBF, 0xF4, 0xAB, 0x48, 0x63, 0x55, 0x3E, 0x56, 0x8C, 0xD1, 0x12, 0xED, 0xC3, 0x49, 0x8E, 0x92,
	0x9D, 0xCA, 0xB1, 0xE5, 0xCE, 0x4D, 0x3F, 0xFA, 0x73, 0x05, 0xE0, 0x4B, 0x93, 0xB2, 0xCB, 0x08,
	0xE1, 0x96, 0x19, 0x3D, 0x83, 0x39, 0x75, 0xEC, 0xD6, 0x3C, 0xD0, 0x70, 0x81, 0x16, 0x29, 0x15,
	0x6C, 0xC7, 0xE7, 0xE2, 0xF6, 0xB7, 0xE8, 0x25, 0x6D, 0x3A, 0xE6, 0xC8, 0x99, 0x46, 0xB0, 0x85,
	0x02, 0x61, 0x1B, 0x8B, 0xB3, 0x9F, 0x0B, 0x2A, 0xA8, 0x77, 0x10, 0xC1, 0x88, 0xCC, 0xA4, 0xDE,
	0x43, 0x58, 0x23, 0xB4, 0xA1, 0xA5, 0x5C, 0xAE, 0xA9, 0x7E, 0x42, 0x40, 0x90, 0xD2, 0xE9, 0x84,
	0xCF, 0xE4, 0xEB, 0x47, 0x4F, 0x82, 0xD4, 0xC5, 0x8F, 0xCD, 0xD3, 0x86, 0x00, 0x59, 0xDF, 0xF2,
    0x0C, 0x7C, 0xC6, 0xBD, 0xF9, 0x7D, 0xC4, 0x91, 0x27, 0x89, 0x32, 0x72, 0x33, 0x65, 0x68, 0xAF
};

static const uint8_t c_aEncode[256] = {
    0xEC, 0x74, 0xC0, 0x63, 0x56, 0x99, 0x69, 0x64, 0x9F, 0x17, 0x26, 0xC6, 0xF0, 0x01, 0x10, 0x4D,
	0xCA, 0x52, 0x8A, 0x4B, 0x7A, 0xAF, 0xAD, 0x20, 0x73, 0xA2, 0x0F, 0xC2, 0x50, 0x78, 0x36, 0x44,
	0x19, 0x1E, 0x72, 0xD2, 0x32, 0xB7, 0x6B, 0xF8, 0x05, 0xAE, 0xC7, 0x1C, 0x55, 0x71, 0x59, 0x13,
	0x11, 0x49, 0xFA, 0xFC, 0x7F, 0x2B, 0x34, 0x66, 0x45, 0xA5, 0xB9, 0x15, 0xA9, 0xA3, 0x86, 0x96,
	0xDB, 0x2D, 0xDA, 0xD0, 0x21, 0x54, 0xBD, 0xE3, 0x83, 0x8D, 0x6E, 0x9B, 0x53, 0x95, 0x04, 0xE4,
	0x2A, 0x70, 0x27, 0x5E, 0x23, 0x85, 0x87, 0x24, 0xD1, 0xED, 0x22, 0x1B, 0xD6, 0x62, 0x76, 0x43,
	0x30, 0xC1, 0x42, 0x84, 0x5B, 0xFD, 0x5F, 0x28, 0xFE, 0x3A, 0x14, 0x37, 0xB0, 0xB8, 0x1A, 0x3D,
	0xAB, 0x48, 0xFB, 0x98, 0x03, 0xA6, 0x77, 0xC9, 0x0B, 0x75, 0x61, 0x08, 0xF1, 0xF5, 0xD9, 0x79,
	0x65, 0xAC, 0xE5, 0xA4, 0xDF, 0xBF, 0xEB, 0x0A, 0xCC, 0xF9, 0x7D, 0xC3, 0x88, 0x0D, 0x8E, 0xE8,
	0xDC, 0xF7, 0x8F, 0x9C, 0x2F, 0x6D, 0xA1, 0x41, 0x09, 0xBC, 0x5C, 0x3C, 0x39, 0x90, 0x7C, 0xC5,
	0x2E, 0xD4, 0x33, 0x4E, 0xCE, 0xD5, 0x58, 0x4F, 0xC8, 0xD8, 0x1F, 0x82, 0x16, 0x3E, 0xD7, 0xFF,
	0xBE, 0x92, 0x9D, 0xC4, 0xD3, 0x0E, 0x1D, 0xB5, 0x5D, 0x18, 0x07, 0x7E, 0x6A, 0xF3, 0x25, 0x80,
	0x6C, 0xCB, 0x46, 0x8C, 0xF6, 0xE7, 0xF2, 0xB1, 0xBB, 0x29, 0x91, 0x9E, 0xCD, 0xE9, 0x94, 0xE0,
	0xAA, 0x89, 0xDD, 0xEA, 0xE6, 0x51, 0xA8, 0x47, 0x3F, 0x02, 0x3B, 0x57, 0x60, 0x0C, 0xCF, 0xEE,
	0x9A, 0xA0, 0xB3, 0x7B, 0xE1, 0x93, 0xBA, 0xB2, 0xB6, 0xDE, 0x00, 0xE2, 0xA7, 0x8B, 0x4C, 0x35,
    0x4A, 0x6F, 0xEF, 0x12, 0x81, 0x2C, 0xB4, 0x38, 0x5A, 0xF4, 0x97, 0x40, 0x68, 0x06, 0x31, 0x67
};

bool decrypt_data(uint8_t* encrypted, uint8_t* decrypted) {
    if(!encrypted || !decrypted) {
        return false;
    }

    for(int i = 0; i < SAFLOK_DATA_LENGTH; i++) {
        int num = c_aDecode[encrypted[i]] - (i + 1);
        if(num < 0) num += 256;
        decrypted[i] = num;
    }

    int b = decrypted[10];
    int b2 = b & 1;

    for(int num2 = SAFLOK_DATA_LENGTH; num2 > 0; num2--) {
        b = decrypted[num2 - 1];
        for(int num3 = 8; num3 > 0; num3--) {
            int num4 = num2 + num3;
            if(num4 > SAFLOK_DATA_LENGTH) num4 -= SAFLOK_DATA_LENGTH;
            int b3 = decrypted[num4 - 1];
            int b4 = (b3 & 0x80) >> 7;
            b3 = ((b3 << 1) & 0xFF) | b2;
            b2 = (b & 0x80) >> 7;
            b = ((b << 1) & 0xFF) | b4;
            decrypted[num4 - 1] = b3;
        }
        decrypted[num2 - 1] = b;
    }

    return true;
}

bool encrypt_data(uint8_t* decrypted, uint8_t* encrypted) {
    if(!decrypted || !encrypted) {
        return false;
    }

    uint8_t* encoded = malloc(SAFLOK_DATA_LENGTH);
    if(!encoded) {
        return false;
    }

    memcpy(encoded, decrypted, SAFLOK_DATA_LENGTH);
    int b = 0;

    for(int i = 0; i < SAFLOK_DATA_LENGTH; i++) {
        int b2 = encoded[i];
        int num2 = i;
        for(int j = 0; j < 8; j++) {
            num2 = (num2 + 1) % SAFLOK_DATA_LENGTH;
            int b3 = encoded[num2];
            int b4 = b2 & 1;
            b2 = (b2 >> 1) | (b << 7);
            b = b3 & 1;
            b3 = (b3 >> 1) | (b4 << 7);
            encoded[num2] = b3;
        }
        encoded[i] = b2;
    }

    encoded[10] |= b;

    memcpy(encrypted, encoded, SAFLOK_DATA_LENGTH);
    free(encoded);

    for(int i = 0; i < SAFLOK_DATA_LENGTH; i++) {
        int j = encrypted[i] + (i + 1);
        if(j > 255) j -= 256;
        encrypted[i] = c_aEncode[j];
    }

    return true;
}

bool saflok_decrypt(uint8_t* encrypted, uint8_t* decrypted_out, size_t out_len) {
    if(!encrypted || !decrypted_out || out_len < SAFLOK_DATA_LENGTH) {
        return false;
    }

    return decrypt_data((uint8_t*)encrypted, decrypted_out);
}

bool saflok_encrypt(uint8_t* decrypted, uint8_t* encrypted_out) {
    if(!decrypted || !encrypted_out) {
         FURI_LOG_E(TAG, "%s: Invalid parameters", __func__);
        return false;
    }

    return encrypt_data((uint8_t*)decrypted, encrypted_out);
}

static uint8_t saflok_calculate_checksum(uint8_t* decrypted) {
    if(!decrypted) {
        FURI_LOG_E(TAG, "saflok_calculate_checksum: NULL input pointer");
        return 0;
    }
    uint32_t sum = 0;
    for(int i = 0; i < SAFLOK_DATA_LENGTH - 1; i++) {
        sum += decrypted[i];
    }
    return (255 - (sum & 0xFF)) & 0xFF;
}

#define SAFLOK_DECRYPT_OR_RETURN(encrypted_ptr, decrypted_buf, error_retval) \
    do { \
        if(!(encrypted_ptr)) { \
            FURI_LOG_E(TAG, "%s: NULL input pointer", __func__); \
            return (error_retval); \
        } \
        if(!saflok_decrypt((encrypted_ptr), (decrypted_buf), sizeof(decrypted_buf))) { \
            FURI_LOG_E(TAG, "%s: Decryption failed", __func__); \
            return (error_retval); \
        } \
    } while(0)

uint8_t saflok_get_sub_function(uint8_t* encrypted) {
    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    SAFLOK_DECRYPT_OR_RETURN(encrypted, decrypted, 0);

    uint8_t sub_function = decrypted[0] & 0x07;

    if(!saflok_validate_field_value(sub_function, MIN_SUB_FUNCTION, MAX_SUB_FUNCTION)) {
        FURI_LOG_W(TAG, "%s: Invalid sub function value %d", __func__, sub_function);
        return 0;
    }
    return sub_function;
}

bool saflok_set_sub_function(SaflokApp* app, uint8_t sub_function) {
    NfcDevice* device = app->saflok_device;
    if(!device) {
        FURI_LOG_E(TAG, "Invalid device pointer");
        return false;
    }

    if(!saflok_validate_field_value(sub_function, MIN_SUB_FUNCTION, MAX_SUB_FUNCTION)) {
        FURI_LOG_W(TAG, "%s: Invalid sub function value %d", __func__, sub_function);
        return false;
    }

    SaflokMfClassicData* mf_data = (SaflokMfClassicData*)nfc_device_get_data(
        device,
        NfcProtocolMfClassic
    );

    if(!mf_data) {
        FURI_LOG_E(TAG, "Invalid MfClassic data in device");
        return false;
    }

    uint8_t encrypted[SAFLOK_DATA_LENGTH];

    memcpy(encrypted, mf_data->block[1].data, 16);

    encrypted[16] = mf_data->block[2].data[0];

    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    if(!saflok_decrypt(encrypted, decrypted, sizeof(decrypted))) {
        FURI_LOG_E(TAG, "Failed to decrypt data");
        return false;
    }

    decrypted[0] = (decrypted[0] & ~0x07) | sub_function;

    decrypted[SAFLOK_DATA_LENGTH - 1] = saflok_calculate_checksum(decrypted);

    saflok_unpack_sub_function(decrypted, app);
    saflok_pack_current_sub_function(app);

    if(!saflok_encrypt(decrypted, encrypted)) {
        FURI_LOG_E(TAG, "Failed to encrypt data");
        return false;
    }

    memcpy(mf_data->block[1].data, encrypted, 16);
    mf_data->block[2].data[0] = encrypted[16];

    mf_data->type = MfClassicType1k;

    nfc_device_set_data(device, NfcProtocolMfClassic, mf_data);

    return true;
}

uint8_t saflok_get_key_level(uint8_t* encrypted) {
    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    SAFLOK_DECRYPT_OR_RETURN(encrypted, decrypted, 0);

    uint8_t key_level = ((decrypted[0] & 0xF0) >> 4) + 1;

    if(!saflok_validate_field_value(key_level, MIN_KEY_LEVEL, MAX_KEY_LEVEL)) {
        FURI_LOG_W(TAG, "%s: Invalid key level value %d", __func__, key_level);
        return 0;
    }
    return key_level;
}

bool saflok_set_key_level(SaflokApp* app, uint8_t key_level) {
    NfcDevice* device = app->saflok_device;
    if(!device) {
        FURI_LOG_E(TAG, "Invalid device pointer");
        return false;
    }

    if(!saflok_validate_field_value(key_level, MIN_KEY_LEVEL, MAX_KEY_LEVEL)) {
        FURI_LOG_W(TAG, "%s: Invalid key level value %d", __func__, key_level);
        return false;
    }

    SaflokMfClassicData* mf_data = (SaflokMfClassicData*)nfc_device_get_data(
        device,
        NfcProtocolMfClassic
    );

    if(!mf_data) {
        FURI_LOG_E(TAG, "Invalid MfClassic data in device");
        return false;
    }

    uint8_t encrypted[SAFLOK_DATA_LENGTH];

    memcpy(encrypted, mf_data->block[1].data, 16);

    encrypted[16] = mf_data->block[2].data[0];

    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    if(!saflok_decrypt(encrypted, decrypted, sizeof(decrypted))) {
        FURI_LOG_E(TAG, "Failed to decrypt data");
        return false;
    }

    decrypted[0] = (decrypted[0] & ~0xF0) | (((key_level - 1) << 4) & 0xF0);

    decrypted[SAFLOK_DATA_LENGTH - 1] = saflok_calculate_checksum(decrypted);

    saflok_unpack_key_level(decrypted, app);
    saflok_pack_current_key_level(app);

    if(!saflok_encrypt(decrypted, encrypted)) {
        FURI_LOG_E(TAG, "Failed to encrypt data");
        return false;
    }

    memcpy(mf_data->block[1].data, encrypted, 16);
    mf_data->block[2].data[0] = encrypted[16];

    mf_data->type = MfClassicType1k;

    nfc_device_set_data(device, NfcProtocolMfClassic, mf_data);

    return true;
}

bool saflok_get_led_warning(uint8_t* encrypted) {
    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    SAFLOK_DECRYPT_OR_RETURN(encrypted, decrypted, false);

    return ((decrypted[0] & 0x08) >> 3) != 0;
}

bool saflok_set_led_warning(SaflokApp* app, bool warning) {
    NfcDevice* device = app->saflok_device;
    if(!device) {
        FURI_LOG_E(TAG, "Invalid device pointer");
        return false;
    }

    SaflokMfClassicData* mf_data = (SaflokMfClassicData*)nfc_device_get_data(
        device,
        NfcProtocolMfClassic
    );

    if(!mf_data) {
        FURI_LOG_E(TAG, "Invalid MfClassic data in device");
        return false;
    }

    uint8_t encrypted[SAFLOK_DATA_LENGTH];

    memcpy(encrypted, mf_data->block[1].data, 16);

    encrypted[16] = mf_data->block[2].data[0];

    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    if(!saflok_decrypt(encrypted, decrypted, sizeof(decrypted))) {
        FURI_LOG_E(TAG, "Failed to decrypt data");
        return false;
    }

    decrypted[0] = (decrypted[0] & ~0x08) | (warning ? 0x08 : 0x00);

    decrypted[SAFLOK_DATA_LENGTH - 1] = saflok_calculate_checksum(decrypted);

    saflok_unpack_led_warning(decrypted, app);
    saflok_pack_current_led_warning(app);

    if(!saflok_encrypt(decrypted, encrypted)) {
        FURI_LOG_E(TAG, "Failed to encrypt data");
        return false;
    }

    memcpy(mf_data->block[1].data, encrypted, 16);
    mf_data->block[2].data[0] = encrypted[16];

    mf_data->type = MfClassicType1k;

    nfc_device_set_data(device, NfcProtocolMfClassic, mf_data);

    return true;
}

uint8_t saflok_get_key_id(uint8_t* encrypted) {
    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    SAFLOK_DECRYPT_OR_RETURN(encrypted, decrypted, 0);

    return decrypted[1];
}

bool saflok_set_key_id(SaflokApp* app, uint8_t key_id) {
    NfcDevice* device = app->saflok_device;
    if(!device) {
        FURI_LOG_E(TAG, "Invalid device pointer");
        return false;
    }

    SaflokMfClassicData* mf_data = (SaflokMfClassicData*)nfc_device_get_data(
        device,
        NfcProtocolMfClassic
    );

    if(!mf_data) {
        FURI_LOG_E(TAG, "Invalid MfClassic data in device");
        return false;
    }

    uint8_t encrypted[SAFLOK_DATA_LENGTH];

    memcpy(encrypted, mf_data->block[1].data, 16);

    encrypted[16] = mf_data->block[2].data[0];

    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    if(!saflok_decrypt(encrypted, decrypted, sizeof(decrypted))) {
        FURI_LOG_E(TAG, "Failed to decrypt data");
        return false;
    }

    decrypted[1] = key_id;

    decrypted[SAFLOK_DATA_LENGTH - 1] = saflok_calculate_checksum(decrypted);

    saflok_unpack_key_id(decrypted, app);
    saflok_pack_current_key_id(app);

    if(!saflok_encrypt(decrypted, encrypted)) {
        FURI_LOG_E(TAG, "Failed to encrypt data");
        return false;
    }

    memcpy(mf_data->block[1].data, encrypted, 16);
    mf_data->block[2].data[0] = encrypted[16];

    mf_data->type = MfClassicType1k;

    nfc_device_set_data(device, NfcProtocolMfClassic, mf_data);

    return true;
}

uint16_t saflok_get_key_record(uint8_t* encrypted) {
    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    SAFLOK_DECRYPT_OR_RETURN(encrypted, decrypted, 0);

    uint16_t key_record = ((decrypted[2] & 0x7F) << 8) | decrypted[3];

    if(!saflok_validate_field_value(key_record, MIN_KEY_RECORD, MAX_KEY_RECORD)) {
        FURI_LOG_W(TAG, "%s: Invalid key record value %d", __func__, key_record);
        return 0;
    }
    return key_record;
}

bool saflok_set_key_record(SaflokApp* app, uint16_t key_record) {
    NfcDevice* device = app->saflok_device;
    if(!device) {
        FURI_LOG_E(TAG, "Invalid device pointer");
        return false;
    }

    if(!saflok_validate_field_value(key_record, MIN_KEY_RECORD, MAX_KEY_RECORD)) {
        FURI_LOG_W(TAG, "%s: Invalid key record value %d", __func__, key_record);
        return false;
    }

    SaflokMfClassicData* mf_data = (SaflokMfClassicData*)nfc_device_get_data(
        device,
        NfcProtocolMfClassic
    );

    if(!mf_data) {
        FURI_LOG_E(TAG, "Invalid MfClassic data in device");
        return false;
    }

    uint8_t encrypted[SAFLOK_DATA_LENGTH];

    memcpy(encrypted, mf_data->block[1].data, 16);

    encrypted[16] = mf_data->block[2].data[0];

    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    if(!saflok_decrypt(encrypted, decrypted, sizeof(decrypted))) {
        FURI_LOG_E(TAG, "Failed to decrypt data");
        return false;
    }

    decrypted[2] = (decrypted[2] & 0x80) | ((key_record >> 8) & 0x7F);
    decrypted[3] = key_record & 0xFF;

    decrypted[SAFLOK_DATA_LENGTH - 1] = saflok_calculate_checksum(decrypted);

    saflok_unpack_key_record(decrypted, app);
    saflok_pack_current_key_record(app);

    if(!saflok_encrypt(decrypted, encrypted)) {
        FURI_LOG_E(TAG, "Failed to encrypt data");
        return false;
    }

    memcpy(mf_data->block[1].data, encrypted, 16);
    mf_data->block[2].data[0] = encrypted[16];

    mf_data->type = MfClassicType1k;

    nfc_device_set_data(device, NfcProtocolMfClassic, mf_data);

    return true;
}

bool saflok_get_opening_key(uint8_t* encrypted) {
    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    SAFLOK_DECRYPT_OR_RETURN(encrypted, decrypted, false);

    return ((decrypted[2] & 0x80) >> 7) != 0;
}

bool saflok_set_opening_key(SaflokApp* app, bool opening_key) {
    NfcDevice* device = app->saflok_device;
    if(!device) {
        FURI_LOG_E(TAG, "Invalid device pointer");
        return false;
    }

    SaflokMfClassicData* mf_data = (SaflokMfClassicData*)nfc_device_get_data(
        device,
        NfcProtocolMfClassic
    );

    if(!mf_data) {
        FURI_LOG_E(TAG, "Invalid MfClassic data in device");
        return false;
    }

    uint8_t encrypted[SAFLOK_DATA_LENGTH];

    memcpy(encrypted, mf_data->block[1].data, 16);

    encrypted[16] = mf_data->block[2].data[0];

    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    if(!saflok_decrypt(encrypted, decrypted, sizeof(decrypted))) {
        FURI_LOG_E(TAG, "Failed to decrypt data");
        return false;
    }

    decrypted[2] = (decrypted[2] & 0x7F) | (opening_key ? 0x80 : 0x00);

    decrypted[SAFLOK_DATA_LENGTH - 1] = saflok_calculate_checksum(decrypted);

    saflok_unpack_opening_key(decrypted, app);
    saflok_pack_current_opening_key(app);

    if(!saflok_encrypt(decrypted, encrypted)) {
        FURI_LOG_E(TAG, "Failed to encrypt data");
        return false;
    }

    memcpy(mf_data->block[1].data, encrypted, 16);
    mf_data->block[2].data[0] = encrypted[16];

    mf_data->type = MfClassicType1k;

    nfc_device_set_data(device, NfcProtocolMfClassic, mf_data);

    return true;
}

uint16_t saflok_get_pass_levels_bitmask(uint8_t* encrypted) {
    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    SAFLOK_DECRYPT_OR_RETURN(encrypted, decrypted, 0);

    uint16_t mask = 0;
    mask |= ((decrypted[5] & 0xF0) >> 4);
    mask |= (decrypted[4] << 4);


    return mask;
}

bool saflok_set_pass_levels(SaflokApp* app, uint16_t pass_levels_mask) {
    NfcDevice* device = app->saflok_device;
    if(!device) {
        FURI_LOG_E(TAG, "Invalid device pointer");
        return false;
    }

    if(pass_levels_mask > 0xFFF) {
        FURI_LOG_W(TAG, "%s: Invalid pass levels mask %04X", __func__, pass_levels_mask);
        return false;
    }

    SaflokMfClassicData* mf_data = (SaflokMfClassicData*)nfc_device_get_data(
        device,
        NfcProtocolMfClassic
    );

    if(!mf_data) {
        FURI_LOG_E(TAG, "Invalid MfClassic data in device");
        return false;
    }

    uint8_t encrypted[SAFLOK_DATA_LENGTH];

    memcpy(encrypted, mf_data->block[1].data, 16);

    encrypted[16] = mf_data->block[2].data[0];

    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    if(!saflok_decrypt(encrypted, decrypted, sizeof(decrypted))) {
        FURI_LOG_E(TAG, "Failed to decrypt data");
        return false;
    }

    decrypted[5] = (decrypted[5] & 0x0F) | ((pass_levels_mask & 0x000F) << 4);

    decrypted[4] = (pass_levels_mask & 0x0FF0) >> 4;

    decrypted[SAFLOK_DATA_LENGTH - 1] = saflok_calculate_checksum(decrypted);

    saflok_unpack_pass_levels(decrypted, app);
    saflok_pack_current_pass_levels(app);

    if(!saflok_encrypt(decrypted, encrypted)) {
        FURI_LOG_E(TAG, "Failed to encrypt data");
        return false;
    }

    memcpy(mf_data->block[1].data, encrypted, 16);
    mf_data->block[2].data[0] = encrypted[16];

    mf_data->type = MfClassicType1k;

    nfc_device_set_data(device, NfcProtocolMfClassic, mf_data);

    return true;
}

uint16_t saflok_get_sequence_combo(uint8_t* encrypted) {
    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    SAFLOK_DECRYPT_OR_RETURN(encrypted, decrypted, 0);

    uint16_t sequence = ((decrypted[5] & 0x0F) << 8) | decrypted[6];

    if(!saflok_validate_field_value(sequence, 0, MAX_SEQUENCE_COMBO)) {
         FURI_LOG_W(TAG, "%s: Invalid sequence combo value %d", __func__, sequence);
         return 0;
    }
    return sequence;
}

bool saflok_set_sequence_combo(SaflokApp* app, uint16_t sequence) {
    NfcDevice* device = app->saflok_device;
    if(!device) {
        FURI_LOG_E(TAG, "Invalid device pointer");
        return false;
    }

    if(!saflok_validate_field_value(sequence, 0, MAX_SEQUENCE_COMBO)) {
        FURI_LOG_W(TAG, "%s: Invalid sequence combo value %d", __func__, sequence);
        return false;
    }

    SaflokMfClassicData* mf_data = (SaflokMfClassicData*)nfc_device_get_data(
        device,
        NfcProtocolMfClassic
    );

    if(!mf_data) {
        FURI_LOG_E(TAG, "Invalid MfClassic data in device");
        return false;
    }

    uint8_t encrypted[SAFLOK_DATA_LENGTH];

    memcpy(encrypted, mf_data->block[1].data, 16);

    encrypted[16] = mf_data->block[2].data[0];

    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    if(!saflok_decrypt(encrypted, decrypted, sizeof(decrypted))) {
        FURI_LOG_E(TAG, "Failed to decrypt data");
        return false;
    }

    decrypted[5] = (decrypted[5] & 0xF0) | ((sequence >> 8) & 0x0F);
    decrypted[6] = sequence & 0xFF;

    decrypted[SAFLOK_DATA_LENGTH - 1] = saflok_calculate_checksum(decrypted);

    saflok_unpack_sequence_combo(decrypted, app);
    saflok_pack_current_sequence_combo(app);

    if(!saflok_encrypt(decrypted, encrypted)) {
        FURI_LOG_E(TAG, "Failed to encrypt data");
        return false;
    }

    memcpy(mf_data->block[1].data, encrypted, 16);
    mf_data->block[2].data[0] = encrypted[16];

    mf_data->type = MfClassicType1k;

    nfc_device_set_data(device, NfcProtocolMfClassic, mf_data);

    return true;
}

bool saflok_get_override_deadbolt(uint8_t* encrypted) {
    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    SAFLOK_DECRYPT_OR_RETURN(encrypted, decrypted, false);

    return ((decrypted[7] & 0x80) >> 7) != 0;
}

bool saflok_set_override_deadbolt(SaflokApp* app, bool override) {
    NfcDevice* device = app->saflok_device;
    if(!device) {
        FURI_LOG_E(TAG, "Invalid device pointer");
        return false;
    }

    SaflokMfClassicData* mf_data = (SaflokMfClassicData*)nfc_device_get_data(
        device,
        NfcProtocolMfClassic
    );

    if(!mf_data) {
        FURI_LOG_E(TAG, "Invalid MfClassic data in device");
        return false;
    }

    uint8_t encrypted[SAFLOK_DATA_LENGTH];

    memcpy(encrypted, mf_data->block[1].data, 16);

    encrypted[16] = mf_data->block[2].data[0];

    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    if(!saflok_decrypt(encrypted, decrypted, sizeof(decrypted))) {
        FURI_LOG_E(TAG, "Failed to decrypt data");
        return false;
    }

    decrypted[7] = (decrypted[7] & 0x7F) | (override ? 0x80 : 0x00);

    decrypted[SAFLOK_DATA_LENGTH - 1] = saflok_calculate_checksum(decrypted);

    saflok_unpack_override_deadbolt(decrypted, app);
    saflok_pack_current_override_deadbolt(app);

    if(!saflok_encrypt(decrypted, encrypted)) {
        FURI_LOG_E(TAG, "Failed to encrypt data");
        return false;
    }

    memcpy(mf_data->block[1].data, encrypted, 16);
    mf_data->block[2].data[0] = encrypted[16];

    mf_data->type = MfClassicType1k;

    nfc_device_set_data(device, NfcProtocolMfClassic, mf_data);

    return true;
}

uint8_t saflok_get_restrictions(uint8_t* encrypted) {
    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    SAFLOK_DECRYPT_OR_RETURN(encrypted, decrypted, 0);

    uint8_t restrictions = decrypted[7] & 0x7F;

    if(!saflok_validate_field_value(restrictions, MIN_RESTRICTIONS, MAX_RESTRICTIONS)) {
         FURI_LOG_W(TAG, "%s: Invalid restrictions value %d", __func__, restrictions);
         return 0;
    }
    return restrictions;
}

bool saflok_set_restrictions(SaflokApp* app, uint8_t restrictions) {
    NfcDevice* device = app->saflok_device;
    if(!device) {
        FURI_LOG_E(TAG, "Invalid device pointer");
        return false;
    }

    if(!saflok_validate_field_value(restrictions, MIN_RESTRICTIONS, MAX_RESTRICTIONS)) {
        FURI_LOG_W(TAG, "%s: Invalid restrictions value %d", __func__, restrictions);
        return false;
    }

    SaflokMfClassicData* mf_data = (SaflokMfClassicData*)nfc_device_get_data(
        device,
        NfcProtocolMfClassic
    );

    if(!mf_data) {
        FURI_LOG_E(TAG, "Invalid MfClassic data in device");
        return false;
    }

    uint8_t encrypted[SAFLOK_DATA_LENGTH];

    memcpy(encrypted, mf_data->block[1].data, 16);

    encrypted[16] = mf_data->block[2].data[0];

    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    if(!saflok_decrypt(encrypted, decrypted, sizeof(decrypted))) {
        FURI_LOG_E(TAG, "Failed to decrypt data");
        return false;
    }

    decrypted[7] = (decrypted[7] & 0x80) | (restrictions & 0x7F);

    decrypted[SAFLOK_DATA_LENGTH - 1] = saflok_calculate_checksum(decrypted);

    saflok_unpack_restrictions(decrypted, app);
    saflok_pack_current_restrictions(app);

    if(!saflok_encrypt(decrypted, encrypted)) {
        FURI_LOG_E(TAG, "Failed to encrypt data");
        return false;
    }

    memcpy(mf_data->block[1].data, encrypted, 16);
    mf_data->block[2].data[0] = encrypted[16];

    mf_data->type = MfClassicType1k;

    nfc_device_set_data(device, NfcProtocolMfClassic, mf_data);

    return true;
}

static bool _saflok_get_creation_date_details(uint8_t* decrypted, SaflokDateTime* out_date) {
    uint8_t year_low = (decrypted[11] & 0xF0) >> 4;
    uint8_t year_high = decrypted[14] & 0xF0;
    out_date->year = (year_low + SAFLOK_YEAR_OFFSET) | year_high;
    out_date->month = decrypted[11] & 0x0F;
    out_date->day = (decrypted[12] >> 3) & 0x1F;
    out_date->hour = ((decrypted[12] & 0x07) << 2) | ((decrypted[13] >> 6));
    out_date->minute = decrypted[13] & 0x3F;

    return is_valid_date(out_date->year, out_date->month, out_date->day, out_date->hour, out_date->minute);
}


SaflokDateTime saflok_get_date(uint8_t* encrypted, SaflokDateType dateType) {
    SaflokDateTime result = {0};
    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    SAFLOK_DECRYPT_OR_RETURN(encrypted, decrypted, result);

    SaflokDateTime creation_date;
    if(!_saflok_get_creation_date_details(decrypted, &creation_date)) {
        FURI_LOG_W(TAG, "%s: Invalid creation date stored on card", __func__);
        return result;
    }

    if(dateType == SAFLOK_DATE_CREATION) {
        result = creation_date;
    } else {
        uint16_t interval_year = decrypted[8] >> 4;
        uint8_t interval_month = decrypted[8] & 0x0F;
        uint8_t interval_day = (decrypted[9] >> 3) & 0x1F;

        result.hour = ((decrypted[9] & 0x07) << 2) | (decrypted[10] >> 6);
        result.minute = decrypted[10] & 0x3F;

        result.year = creation_date.year + interval_year;
        result.month = creation_date.month + interval_month;
        result.day = creation_date.day + interval_day;

        uint8_t temp_day = result.day;
        uint8_t temp_month = result.month;
        uint16_t temp_year = result.year;
        saflok_normalize_date(&temp_day, &temp_month, &temp_year);

        if(is_valid_date(temp_year, temp_month, temp_day, result.hour, result.minute)) {
             result.day = temp_day;
             result.month = temp_month;
             result.year = temp_year;
        } else {
             FURI_LOG_W(TAG, "%s: Calculated expiration date is invalid after normalization", __func__);
             result = (SaflokDateTime){0};
        }
    }

    return result;
}

bool saflok_set_date(SaflokApp* app, SaflokDateTime date, SaflokDateType dateType) {
    NfcDevice* device = app->saflok_device;
    if(!device) {
        FURI_LOG_E(TAG, "Invalid device pointer");
        return false;
    }

    if(!is_valid_date(date.year, date.month, date.day, date.hour, date.minute)) {
        FURI_LOG_W(TAG, "%s: Invalid date provided", __func__);
        return false;
    }

    SaflokMfClassicData* mf_data = (SaflokMfClassicData*)nfc_device_get_data(
        device,
        NfcProtocolMfClassic
    );

    if(!mf_data) {
        FURI_LOG_E(TAG, "Invalid MfClassic data in device");
        return false;
    }

    uint8_t encrypted[SAFLOK_DATA_LENGTH];

    memcpy(encrypted, mf_data->block[1].data, 16);

    encrypted[16] = mf_data->block[2].data[0];

    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    if(!saflok_decrypt(encrypted, decrypted, sizeof(decrypted))) {
        FURI_LOG_E(TAG, "Failed to decrypt data");
        return false;
    }

    if(dateType == SAFLOK_DATE_CREATION) {
        uint16_t adjusted_year = date.year - SAFLOK_YEAR_OFFSET;
        if(adjusted_year > 0xFF) {
            FURI_LOG_W(TAG, "%s: Creation year out of range", __func__);
            return false;
        }
        decrypted[11] = ((adjusted_year & 0x0F) << 4) | (date.month & 0x0F);
        decrypted[12] = ((date.day & 0x1F) << 3) | ((date.hour >> 2) & 0x07);
        decrypted[13] = ((date.hour & 0x03) << 6) | (date.minute & 0x3F);
        decrypted[14] = (decrypted[14] & 0x0F) | (adjusted_year & 0xF0);
    } else {
        SaflokDateTime creation_date;
        if(!_saflok_get_creation_date_details(decrypted, &creation_date)) {
            FURI_LOG_E(TAG, "%s: Failed to get valid creation date from card data", __func__);
            return false;
        }

        uint16_t year_interval = date.year - creation_date.year;
        int16_t month_interval = date.month - creation_date.month;
        int16_t day_interval = date.day - creation_date.day;

        while(day_interval < 0) {
            month_interval--;
            uint8_t prev_month = (creation_date.month == 1) ? 12 : creation_date.month - 1;
            uint16_t prev_year = (creation_date.month == 1) ? creation_date.year - 1 : creation_date.year;
            day_interval += days_in_month(prev_month, prev_year);
        }

        while(month_interval < 0) {
            year_interval--;
            month_interval += 12;
        }

        if(year_interval > 0x0F || month_interval < 0 || month_interval > 0x0F ||
           day_interval < 0 || day_interval > 0x1F) {
            FURI_LOG_W(TAG, "%s: Calculated intervals out of range (Y:%u M:%d D:%d)",
                      __func__, year_interval, month_interval, day_interval);
            return false;
        }

        decrypted[8] = ((year_interval & 0x0F) << 4) | (month_interval & 0x0F);
        decrypted[9] = ((day_interval & 0x1F) << 3) | ((date.hour >> 2) & 0x07);
        decrypted[10] = ((date.hour & 0x03) << 6) | (date.minute & 0x3F);
    }

    decrypted[SAFLOK_DATA_LENGTH - 1] = saflok_calculate_checksum(decrypted);

    if(dateType == SAFLOK_DATE_CREATION) {
        saflok_unpack_creation_date(decrypted, app);
        saflok_pack_current_creation_date(app);
    } else {
        saflok_unpack_expiration_date(decrypted, app);
        saflok_pack_current_expiration_date(app);
    }

    if(!saflok_encrypt(decrypted, encrypted)) {
        FURI_LOG_E(TAG, "Failed to encrypt data");
        return false;
    }

    memcpy(mf_data->block[1].data, encrypted, 16);
    mf_data->block[2].data[0] = encrypted[16];

    mf_data->type = MfClassicType1k;

    nfc_device_set_data(device, NfcProtocolMfClassic, mf_data);

    return true;
}

SaflokDateTime saflok_get_expiration_date(uint8_t* encrypted) {
    return saflok_get_date(encrypted, SAFLOK_DATE_EXPIRATION);
}

bool saflok_set_expiration_date(SaflokApp* app, SaflokDateTime date) {
    return saflok_set_date(app, date, SAFLOK_DATE_EXPIRATION);
}

SaflokDateTime saflok_get_creation_date(uint8_t* encrypted) {
    return saflok_get_date(encrypted, SAFLOK_DATE_CREATION);
}

bool saflok_set_creation_date(SaflokApp* app, SaflokDateTime date) {
    return saflok_set_date(app, date, SAFLOK_DATE_CREATION);
}

uint16_t saflok_get_property_number(uint8_t* encrypted) {
    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    SAFLOK_DECRYPT_OR_RETURN(encrypted, decrypted, 0);

    uint16_t property_num = ((decrypted[14] & 0x0F) << 8) | decrypted[15];

    if(!saflok_validate_field_value(property_num, MIN_PROPERTY_NUMBER, MAX_PROPERTY_NUMBER)) {
        FURI_LOG_W(TAG, "%s: Invalid property number value %d", __func__, property_num);
        return 0;
    }
    return property_num;
}

bool saflok_set_property_number(SaflokApp* app, uint16_t property_num) {
    NfcDevice* device = app->saflok_device;
    if(!device) {
        FURI_LOG_E(TAG, "Invalid device pointer");
        return false;
    }

    if(!saflok_validate_field_value(property_num, MIN_PROPERTY_NUMBER, MAX_PROPERTY_NUMBER)) {
        FURI_LOG_W(TAG, "%s: Invalid property number value %d", __func__, property_num);
        return false;
    }

    SaflokMfClassicData* mf_data = (SaflokMfClassicData*)nfc_device_get_data(
        device,
        NfcProtocolMfClassic
    );

    if(!mf_data) {
        FURI_LOG_E(TAG, "Invalid MfClassic data in device");
        return false;
    }

    uint8_t encrypted[SAFLOK_DATA_LENGTH];

    memcpy(encrypted, mf_data->block[1].data, 16);

    encrypted[16] = mf_data->block[2].data[0];

    uint8_t decrypted[SAFLOK_DATA_LENGTH];
    if(!saflok_decrypt(encrypted, decrypted, sizeof(decrypted))) {
        FURI_LOG_E(TAG, "Failed to decrypt data");
        return false;
    }

    decrypted[14] = (decrypted[14] & 0xF0) | ((property_num >> 8) & 0x0F);
    decrypted[15] = property_num & 0xFF;

    decrypted[SAFLOK_DATA_LENGTH - 1] = saflok_calculate_checksum(decrypted);

    saflok_unpack_property_number(decrypted, app);
    saflok_pack_current_property_number(app);

    if(!saflok_encrypt(decrypted, encrypted)) {
        FURI_LOG_E(TAG, "Failed to encrypt data");
        return false;
    }

    memcpy(mf_data->block[1].data, encrypted, 16);
    mf_data->block[2].data[0] = encrypted[16];

    mf_data->type = MfClassicType1k;

    nfc_device_set_data(device, NfcProtocolMfClassic, mf_data);

    return true;
}
