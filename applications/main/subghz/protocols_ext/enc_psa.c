/* enc_psa.fal : SD encoder for psa (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_psa_alloc enc_psa__subghz_protocol_encoder_psa_alloc
#define subghz_protocol_encoder_psa_deserialize enc_psa__subghz_protocol_encoder_psa_deserialize
#define subghz_protocol_encoder_psa_free enc_psa__subghz_protocol_encoder_psa_free
#define subghz_protocol_encoder_psa_stop enc_psa__subghz_protocol_encoder_psa_stop
#define subghz_protocol_encoder_psa_yield enc_psa__subghz_protocol_encoder_psa_yield
#define subghz_protocol_psa enc_psa__subghz_protocol_psa
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/psa.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/subghz/protocols/common.h>

#define PSA_TE_LONG_250 0xfa
#define PSA_TE_END_1000 1000
#define TEA_DELTA 0x9E3779B9U
#define TEA_ROUNDS 32
#define PSA_BF1_CONST_U4 0x0E0F5C41U
#define PSA_BF1_CONST_U5 0x0F5C4123U
#define PSA_BF1_START 0x23000000U

struct SubGhzProtocolEncoderPSA {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t key1_low;
    uint32_t key1_high;
    uint16_t validation_field;
    uint32_t key2_low;
    uint32_t counter;
    uint8_t button;
    uint8_t type;
    uint8_t seed;
    uint8_t mode;
    uint32_t serial;
    uint16_t crc;
    bool is_running;
    bool raw_replay;
};

static const SubGhzBlockConst subghz_protocol_psa_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 128,
};

static const uint32_t PSA_BF1_KEY_SCHEDULE[4] = {
    0x4A434915U,
    0xD6743C2BU,
    0x1F29D308U,
    0xE6B79A64U,
};

static uint8_t psa_btn_to_custom(uint8_t btn);
static uint8_t psa_custom_to_btn(uint8_t custom_btn, uint8_t fallback_btn);
static uint8_t psa_get_btn_code(void);
static void psa_setup_byte_buffer(uint8_t* buffer, uint32_t key1_low, uint32_t key1_high, uint32_t key2_low);
static void psa_calculate_checksum(uint8_t* buffer);
static void psa_second_stage_xor_encrypt(uint8_t* buffer);
__attribute__((optimize("O3"), always_inline)) static inline void
    psa_tea_encrypt(
        uint32_t* restrict v0,
        uint32_t* restrict v1,
        const uint32_t* restrict key);
static uint8_t psa_calculate_tea_crc(uint32_t v0, uint32_t v1);
static void psa_unpack_tea_result_to_buffer(uint8_t* buffer, uint32_t v0, uint32_t v1);
static void psa_build_buffer_mode23(SubGhzProtocolEncoderPSA* instance, uint8_t* buffer, uint8_t* preserve_buffer01);
static void psa_build_buffer_mode36(SubGhzProtocolEncoderPSA* instance, uint8_t* buffer, uint8_t* preserve_buffer01);
static void psa_encoder_build_upload(SubGhzProtocolEncoderPSA* instance);

static uint8_t psa_btn_to_custom(uint8_t btn) {
    switch(btn) {
    case 0x0:
        return SUBGHZ_CUSTOM_BTN_UP;
    case 0x1:
        return SUBGHZ_CUSTOM_BTN_DOWN;
    case 0x2:
        return SUBGHZ_CUSTOM_BTN_LEFT;
    default:
        return SUBGHZ_CUSTOM_BTN_OK;
    }
}

static uint8_t psa_custom_to_btn(uint8_t custom_btn, uint8_t fallback_btn) {
    switch(custom_btn) {
    case SUBGHZ_CUSTOM_BTN_UP:
        return 0x0;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        return 0x1;
    case SUBGHZ_CUSTOM_BTN_LEFT:
    case SUBGHZ_CUSTOM_BTN_RIGHT:
        return 0x2;
    default:
        return fallback_btn;
    }
}

static uint8_t psa_get_btn_code(void) {
    uint8_t custom_btn = subghz_custom_btn_get();
    uint8_t original_btn = psa_custom_to_btn(subghz_custom_btn_get_original(), 0x0);
    if(custom_btn == SUBGHZ_CUSTOM_BTN_OK) return original_btn;
    return psa_custom_to_btn(custom_btn, original_btn);
}

static void psa_setup_byte_buffer(uint8_t* buffer, uint32_t key1_low, uint32_t key1_high, uint32_t key2_low) {
    for(int i = 0; i < 8; i++) {
        int shift = i * 8;
        uint8_t byte_val;
        if(shift < 32) {
            byte_val = (uint8_t)((key1_low >> shift) & 0xFF);
        } else {
            byte_val = (uint8_t)((key1_high >> (shift - 32)) & 0xFF);
        }
        buffer[7 - i] = byte_val;
    }
    buffer[9] = (uint8_t)(key2_low & 0xFF);
    buffer[8] = (uint8_t)((key2_low >> 8) & 0xFF);
}

static void psa_calculate_checksum(uint8_t* buffer) {
    uint32_t checksum = 0;
    for(int i = 2; i < 8; i++) {
        checksum += (buffer[i] & 0xF) + ((buffer[i] >> 4) & 0xF);
    }
    buffer[11] = (uint8_t)((checksum * 0x10) & 0xFF);
}

static void psa_second_stage_xor_encrypt(uint8_t* buffer) {
    uint8_t E6 = buffer[8];
    uint8_t E7 = buffer[9];

    uint8_t P[6];
    P[0] = buffer[2];
    P[1] = buffer[3];
    P[2] = buffer[4];
    P[3] = buffer[5];
    P[4] = buffer[6];
    P[5] = buffer[7];

    uint8_t E5 = P[5] ^ E7 ^ E6;
    uint8_t E0 = P[2] ^ E5;
    uint8_t E2 = P[4] ^ E0;
    uint8_t E4 = P[3] ^ E2;
    uint8_t E3 = P[0] ^ E5;
    uint8_t E1 = P[1] ^ E3;

    buffer[2] = E0;
    buffer[3] = E1;
    buffer[4] = E2;
    buffer[5] = E3;
    buffer[6] = E4;
    buffer[7] = E5;
}

__attribute__((optimize("O3"), always_inline)) static inline void
    psa_tea_encrypt(
        uint32_t* restrict v0,
        uint32_t* restrict v1,
        const uint32_t* restrict key) {
    uint32_t a = *v0, b = *v1;
    uint32_t sum = 0;
    for(int i = 0; i < TEA_ROUNDS; i++) {
        uint32_t temp = key[sum & 3] + sum;
        sum += TEA_DELTA;
        a += (temp ^ (((b >> 5) ^ (b << 4)) + b));
        temp = key[(sum >> 11) & 3] + sum;
        b += (temp ^ (((a >> 5) ^ (a << 4)) + a));
    }
    *v0 = a;
    *v1 = b;
}

static uint8_t psa_calculate_tea_crc(uint32_t v0, uint32_t v1) {
    uint32_t crc = ((v0 >> 24) & 0xFF) + ((v0 >> 16) & 0xFF) +
                   ((v0 >> 8) & 0xFF) + (v0 & 0xFF);
    crc += ((v1 >> 24) & 0xFF) + ((v1 >> 16) & 0xFF) + ((v1 >> 8) & 0xFF);
    return (uint8_t)(crc & 0xFF);
}

static void psa_unpack_tea_result_to_buffer(uint8_t* buffer, uint32_t v0, uint32_t v1) {
    buffer[2] = (uint8_t)((v0 >> 24) & 0xFF);
    buffer[3] = (uint8_t)((v0 >> 16) & 0xFF);
    buffer[4] = (uint8_t)((v0 >> 8) & 0xFF);
    buffer[5] = (uint8_t)(v0 & 0xFF);
    buffer[6] = (uint8_t)((v1 >> 24) & 0xFF);
    buffer[7] = (uint8_t)((v1 >> 16) & 0xFF);
    buffer[8] = (uint8_t)((v1 >> 8) & 0xFF);
    buffer[9] = (uint8_t)(v1 & 0xFF);
}

static void psa_build_buffer_mode23(SubGhzProtocolEncoderPSA* instance, uint8_t* buffer, uint8_t* preserve_buffer01) {
    memset(buffer, 0, 48);

    buffer[2] = (uint8_t)((instance->serial >> 16) & 0xFF);
    buffer[3] = (uint8_t)((instance->serial >> 8) & 0xFF);
    buffer[4] = (uint8_t)(instance->serial & 0xFF);
    buffer[5] = (uint8_t)((instance->counter >> 8) & 0xFF);
    buffer[6] = (uint8_t)(instance->counter & 0xFF);
    buffer[7] = (uint8_t)(instance->crc & 0xFF);
    buffer[8] = (uint8_t)(instance->button & 0xF);

    uint8_t original_buffer9 = 0;
    bool has_original_key2 = (instance->key2_low != 0);
    if(has_original_key2) {
        original_buffer9 = (uint8_t)(instance->key2_low & 0xFF);
        buffer[9] = original_buffer9;
    }

    uint8_t initial_plaintext[6];
    initial_plaintext[0] = buffer[2];
    initial_plaintext[1] = buffer[3];
    initial_plaintext[2] = buffer[4];
    initial_plaintext[3] = buffer[5];
    initial_plaintext[4] = buffer[6];
    initial_plaintext[5] = buffer[7];
    uint8_t initial_button = buffer[8] & 0xF;

    bool found = false;
    uint8_t buffer9_to_use = has_original_key2 ? original_buffer9 : 0;
    uint8_t buffer9_end = has_original_key2 ? original_buffer9 + 1 : 255;

    for(uint8_t buffer9_try = buffer9_to_use; buffer9_try < buffer9_end && !found; buffer9_try++) {
        for(uint8_t buffer8_high_try = 0; buffer8_high_try < 16 && !found; buffer8_high_try++) {
            buffer[2] = initial_plaintext[0];
            buffer[3] = initial_plaintext[1];
            buffer[4] = initial_plaintext[2];
            buffer[5] = initial_plaintext[3];
            buffer[6] = initial_plaintext[4];
            buffer[7] = initial_plaintext[5];
            buffer[8] = initial_button | (buffer8_high_try << 4);
            buffer[9] = buffer9_try;

            psa_second_stage_xor_encrypt(buffer);
            psa_calculate_checksum(buffer);
            uint8_t checksum_after = buffer[11];
            uint8_t key2_high_after = checksum_after & 0xF0;

            uint8_t validation = (checksum_after ^ buffer[8]) & 0xF0;
            if(validation == 0) {
                buffer[8] = (buffer[8] & 0x0F) | key2_high_after;
                buffer[13] = buffer[9] ^ buffer[8];
                found = true;
                break;
            }
        }
    }

    if(!found) {
        buffer[2] = initial_plaintext[0];
        buffer[3] = initial_plaintext[1];
        buffer[4] = initial_plaintext[2];
        buffer[5] = initial_plaintext[3];
        buffer[6] = initial_plaintext[4];
        buffer[7] = initial_plaintext[5];
        buffer[8] = initial_button;
        buffer[9] = has_original_key2 ? original_buffer9 : 0x23;

        psa_second_stage_xor_encrypt(buffer);
        psa_calculate_checksum(buffer);
        uint8_t checksum_after = buffer[11];
        uint8_t key2_high_after = checksum_after & 0xF0;
        buffer[8] = (buffer[8] & 0x0F) | key2_high_after;
        buffer[13] = buffer[9] ^ buffer[8];
    }

    if(preserve_buffer01 != NULL) {
        buffer[0] = preserve_buffer01[0];
        buffer[1] = preserve_buffer01[1];
    } else {
        buffer[0] = buffer[2] ^ buffer[6];
        buffer[1] = buffer[3] ^ buffer[7];
    }
}

static void psa_build_buffer_mode36(SubGhzProtocolEncoderPSA* instance, uint8_t* buffer, uint8_t* preserve_buffer01) {
    memset(buffer, 0, 48);

    uint32_t v0 = ((instance->serial & 0xFFFFFF) << 8) |
                   ((instance->button & 0xF) << 4) |
                   ((instance->counter >> 24) & 0x0F);
    uint32_t v1 = ((instance->counter & 0xFFFFFF) << 8) |
                   (instance->crc & 0xFF);

    uint8_t crc = psa_calculate_tea_crc(v0, v1);
    v1 = (v1 & 0xFFFFFF00) | crc;

    uint32_t bf_counter = PSA_BF1_START | (instance->serial & 0xFFFFFF);

    uint32_t working_key[4];

    uint32_t wk2 = PSA_BF1_CONST_U4;
    uint32_t wk3 = bf_counter;
    psa_tea_encrypt(&wk2, &wk3, PSA_BF1_KEY_SCHEDULE);

    uint32_t wk0 = (bf_counter << 8) | 0x0E;
    uint32_t wk1 = PSA_BF1_CONST_U5;
    psa_tea_encrypt(&wk0, &wk1, PSA_BF1_KEY_SCHEDULE);

    working_key[0] = wk0;
    working_key[1] = wk1;
    working_key[2] = wk2;
    working_key[3] = wk3;

    psa_tea_encrypt(&v0, &v1, working_key);

    psa_unpack_tea_result_to_buffer(buffer, v0, v1);

    if(preserve_buffer01 != NULL) {
        buffer[0] = preserve_buffer01[0];
        buffer[1] = preserve_buffer01[1];
    } else {
        buffer[0] = buffer[2] ^ buffer[6];
        buffer[1] = buffer[3] ^ buffer[7];
    }
}

static void psa_encoder_build_upload(SubGhzProtocolEncoderPSA* instance) {
    furi_assert(instance);

    uint32_t key1_high;
    uint32_t key1_low;
    uint16_t validation_field;

    if(instance->raw_replay) {
        key1_high = instance->key1_high;
        key1_low = instance->key1_low;
        validation_field = instance->validation_field;
    } else {
        uint8_t buffer[48] = {0};

        uint8_t preserve_buffer01[2] = {0};
        uint8_t* preserve_ptr = NULL;

        if(instance->key1_low != 0 || instance->key1_high != 0) {
            uint8_t orig_buffer[48] = {0};
            psa_setup_byte_buffer(orig_buffer, instance->key1_low, instance->key1_high, instance->key2_low);
            preserve_buffer01[0] = orig_buffer[0];
            preserve_buffer01[1] = orig_buffer[1];
            preserve_ptr = preserve_buffer01;
        }

        if(instance->mode == 0x23) {
            psa_build_buffer_mode23(instance, buffer, preserve_ptr);
        } else if(instance->mode == 0x36) {
            psa_build_buffer_mode36(instance, buffer, preserve_ptr);
        } else {
            return;
        }

        key1_high = ((uint32_t)buffer[0] << 24) | ((uint32_t)buffer[1] << 16) |
                    ((uint32_t)buffer[2] << 8) | (uint32_t)buffer[3];
        key1_low = ((uint32_t)buffer[4] << 24) | ((uint32_t)buffer[5] << 16) |
                   ((uint32_t)buffer[6] << 8) | (uint32_t)buffer[7];
        validation_field = ((uint16_t)buffer[8] << 8) | (uint16_t)buffer[9];
    }

    size_t index = 0;
    uint32_t te = PSA_TE_LONG_250;

    for(int i = 0; i < 80; i++) {
        if(index >= instance->encoder.size_upload - 2) break;
        instance->encoder.upload[index++] = level_duration_make(true, te);
        instance->encoder.upload[index++] = level_duration_make(false, te);
    }

    uint32_t te_long_transition = subghz_protocol_psa_const.te_long;
    if(index < instance->encoder.size_upload - 3) {
        instance->encoder.upload[index++] = level_duration_make(false, te);
        instance->encoder.upload[index++] = level_duration_make(true, te_long_transition);
        instance->encoder.upload[index++] = level_duration_make(false, te);
    }

    uint64_t key1_data = ((uint64_t)key1_high << 32) | key1_low;
    for(int bit = 63; bit >= 0; bit--) {
        if(index >= instance->encoder.size_upload - 2) break;
        bool bit_value = (key1_data >> bit) & 1;
        if(bit_value) {
            instance->encoder.upload[index++] = level_duration_make(true, te);
            instance->encoder.upload[index++] = level_duration_make(false, te);
        } else {
            instance->encoder.upload[index++] = level_duration_make(false, te);
            instance->encoder.upload[index++] = level_duration_make(true, te);
        }
    }

    for(int bit = 15; bit >= 0; bit--) {
        if(index >= instance->encoder.size_upload - 2) break;
        bool bit_value = (validation_field >> bit) & 1;
        if(bit_value) {
            instance->encoder.upload[index++] = level_duration_make(true, te);
            instance->encoder.upload[index++] = level_duration_make(false, te);
        } else {
            instance->encoder.upload[index++] = level_duration_make(false, te);
            instance->encoder.upload[index++] = level_duration_make(true, te);
        }
    }

    uint32_t end_duration = PSA_TE_END_1000;
    if(index < instance->encoder.size_upload - 1) {
        instance->encoder.upload[index++] = level_duration_make(true, end_duration);
        instance->encoder.upload[index++] = level_duration_make(false, end_duration);
    }

    instance->encoder.size_upload = index;
    instance->encoder.front = 0;
    instance->encoder.repeat = 10;

    instance->key1_high = key1_high;
    instance->key1_low = key1_low;
    instance->key2_low = validation_field;
}

void* subghz_protocol_encoder_psa_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderPSA* instance = malloc(sizeof(SubGhzProtocolEncoderPSA));
    if(instance) {
        memset(instance, 0, sizeof(SubGhzProtocolEncoderPSA));
        instance->base.protocol = &subghz_protocol_psa;
        instance->generic.protocol_name = instance->base.protocol->name;
        instance->encoder.size_upload = 600;
        instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
        instance->encoder.repeat = 10;
        instance->encoder.front = 0;
        instance->encoder.is_running = false;
        instance->is_running = false;
    }
    return instance;
}

void subghz_protocol_encoder_psa_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderPSA* instance = context;
    if(instance->encoder.upload) free(instance->encoder.upload);
    free(instance);
}

SubGhzProtocolStatus subghz_protocol_encoder_psa_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderPSA* instance = context;

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    FuriString* temp_str = furi_string_alloc();

    do {
        flipper_format_rewind(flipper_format);
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        uint64_t key1 = instance->generic.data;
        instance->key1_low = (uint32_t)(key1 & 0xFFFFFFFF);
        instance->key1_high = (uint32_t)((key1 >> 32) & 0xFFFFFFFF);

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_string(flipper_format, "Key_2", temp_str)) {
            const char* key2_str = furi_string_get_cstr(temp_str);
            uint64_t key2 = 0;
            for(size_t i = 0; i < strlen(key2_str); i++) {
                char c = key2_str[i];
                if(c == ' ') continue;
                uint8_t nibble;
                if(c >= '0' && c <= '9') {
                    nibble = c - '0';
                } else if(c >= 'A' && c <= 'F') {
                    nibble = c - 'A' + 10;
                } else if(c >= 'a' && c <= 'f') {
                    nibble = c - 'a' + 10;
                } else {
                    break;
                }
                key2 = (key2 << 4) | nibble;
            }
            instance->key2_low = (uint32_t)(key2 & 0xFFFFFFFF);
            instance->validation_field = (uint16_t)(key2 & 0xFFFF);
        } else {
            break;
        }

        uint32_t serial = 0;
        uint32_t counter = 0;
        uint8_t button = 0;
        uint8_t type = 0;
        uint16_t crc = 0;
        uint32_t seed = 0;

        bool has_decrypted_data = true;

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_string(flipper_format, "Serial", temp_str)) {
            const char* serial_str = furi_string_get_cstr(temp_str);
            for(size_t i = 0; i < strlen(serial_str); i++) {
                char c = serial_str[i];
                if(c == ' ') continue;
                uint8_t nibble = (c >= '0' && c <= '9') ? c - '0' :
                               (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
                               (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
                serial = (serial << 4) | nibble;
            }
        } else {
            has_decrypted_data = false;
        }

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_string(flipper_format, "Cnt", temp_str)) {
            const char* cnt_str = furi_string_get_cstr(temp_str);
            for(size_t i = 0; i < strlen(cnt_str); i++) {
                char c = cnt_str[i];
                if(c == ' ') continue;
                uint8_t nibble = (c >= '0' && c <= '9') ? c - '0' :
                               (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
                               (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
                counter = (counter << 4) | nibble;
            }
        } else {
            flipper_format_rewind(flipper_format);
            if(!flipper_format_read_uint32(flipper_format, "Cnt", &counter, 1)) {
                has_decrypted_data = false;
            }
        }

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_string(flipper_format, "Btn", temp_str)) {
            const char* btn_str = furi_string_get_cstr(temp_str);
            uint32_t btn_val = 0;
            for(size_t i = 0; i < strlen(btn_str); i++) {
                char c = btn_str[i];
                if(c == ' ') continue;
                uint8_t nibble = (c >= '0' && c <= '9') ? c - '0' :
                               (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
                               (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
                btn_val = (btn_val << 4) | nibble;
            }
            button = (uint8_t)(btn_val & 0xFF);
        } else {
            uint32_t btn_val = 0;
            flipper_format_rewind(flipper_format);
            if(flipper_format_read_uint32(flipper_format, "Btn", &btn_val, 1)) {
                button = (uint8_t)(btn_val & 0xFF);
            } else {
                has_decrypted_data = false;
            }
        }

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_string(flipper_format, "Type", temp_str)) {
            const char* type_str = furi_string_get_cstr(temp_str);
            uint32_t type_val = 0;
            for(size_t i = 0; i < strlen(type_str); i++) {
                char c = type_str[i];
                if(c == ' ') continue;
                uint8_t nibble = (c >= '0' && c <= '9') ? c - '0' :
                               (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
                               (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
                type_val = (type_val << 4) | nibble;
            }
            type = (uint8_t)(type_val & 0xFF);
        } else {
            has_decrypted_data = false;
        }

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_string(flipper_format, "CRC", temp_str)) {
            const char* crc_str = furi_string_get_cstr(temp_str);
            uint32_t crc_val = 0;
            for(size_t i = 0; i < strlen(crc_str); i++) {
                char c = crc_str[i];
                if(c == ' ') continue;
                uint8_t nibble = (c >= '0' && c <= '9') ? c - '0' :
                               (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
                               (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
                crc_val = (crc_val << 4) | nibble;
            }
            crc = (uint16_t)(crc_val & 0xFFFF);
        } else {
            has_decrypted_data = false;
        }

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_string(flipper_format, "Seed", temp_str)) {
            const char* seed_str = furi_string_get_cstr(temp_str);
            for(size_t i = 0; i < strlen(seed_str); i++) {
                char c = seed_str[i];
                if(c == ' ') continue;
                uint8_t nibble = (c >= '0' && c <= '9') ? c - '0' :
                               (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
                               (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
                seed = (seed << 4) | nibble;
            }
        }

        instance->raw_replay = false;
        {
            uint32_t txraw = 0;
            flipper_format_rewind(flipper_format);
            if(flipper_format_read_uint32(flipper_format, "TxRaw", &txraw, 1) && txraw != 0) {
                instance->raw_replay = true;
            }
        }
        if(!(has_decrypted_data && type != 0x00)) {
            instance->raw_replay = true;
        }

        if(instance->raw_replay) {
            psa_encoder_build_upload(instance);
            instance->generic.data =
                ((uint64_t)instance->key1_high << 32) | instance->key1_low;
            instance->is_running = true;
            ret = SubGhzProtocolStatusOk;
        } else if(has_decrypted_data && type != 0x00) {
            instance->serial = serial;
            instance->counter = counter;
            instance->button = button;
            instance->type = type;
            instance->crc = crc;
            instance->seed = (uint8_t)(seed & 0xFF);

            instance->mode = instance->type;
            if(instance->mode == 0x23 || instance->mode == 0) {
                instance->mode = 0x23;
            } else if(instance->mode == 0x36) {
                instance->mode = 0x36;
            } else {
                instance->mode = 0x23;
            }

            uint8_t file_btn = button;
            subghz_custom_btn_set_original(psa_btn_to_custom(file_btn));
            subghz_custom_btn_set_max(4);
            if(!subghz_block_generic_global_button_override_get(&instance->button)) {
                instance->button = psa_get_btn_code();
            }
            FURI_LOG_I("PSA_ENC", "file_btn=%02X custom=%02X result=%02X orig=%02X", file_btn, subghz_custom_btn_get(), instance->button, subghz_custom_btn_get_original());

            uint32_t override_cnt = 0;
            if(subghz_block_generic_global_counter_override_get(&override_cnt)) {
                instance->counter = override_cnt;
            } else {
                uint32_t mult = furi_hal_subghz_get_rolling_counter_mult();
                instance->counter = (instance->counter + mult) & 0xFFFFFFFF;
            }

            psa_encoder_build_upload(instance);

            instance->generic.data = ((uint64_t)instance->key1_high << 32) | instance->key1_low;
            instance->generic.cnt = instance->counter;
            instance->generic.serial = instance->serial;
            instance->generic.btn = instance->button;

            flipper_format_rewind(flipper_format);

            char cnt_str[24];
            if(instance->type == 0x23) {
                snprintf(cnt_str, sizeof(cnt_str), "%02X %02X",
                        (unsigned int)((instance->counter >> 8) & 0xFF),
                        (unsigned int)(instance->counter & 0xFF));
            } else {
                snprintf(cnt_str, sizeof(cnt_str), "%02X %02X %02X %02X",
                        (unsigned int)((instance->counter >> 24) & 0x0F),
                        (unsigned int)((instance->counter >> 16) & 0xFF),
                        (unsigned int)((instance->counter >> 8) & 0xFF),
                        (unsigned int)(instance->counter & 0xFF));
            }
            flipper_format_insert_or_update_string_cstr(flipper_format, "Cnt", cnt_str);

            char btn_str[8];
            snprintf(btn_str, sizeof(btn_str), "%02X", (unsigned int)instance->button);
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_string_cstr(flipper_format, "Btn", btn_str);

            char key_str[32];
            uint64_t key1 = ((uint64_t)instance->key1_high << 32) | instance->key1_low;
            snprintf(key_str, sizeof(key_str), "%02X %02X %02X %02X %02X %02X %02X %02X",
                    (unsigned int)((key1 >> 56) & 0xFF),
                    (unsigned int)((key1 >> 48) & 0xFF),
                    (unsigned int)((key1 >> 40) & 0xFF),
                    (unsigned int)((key1 >> 32) & 0xFF),
                    (unsigned int)((key1 >> 24) & 0xFF),
                    (unsigned int)((key1 >> 16) & 0xFF),
                    (unsigned int)((key1 >> 8) & 0xFF),
                    (unsigned int)(key1 & 0xFF));
            flipper_format_rewind(flipper_format);
            flipper_format_update_string_cstr(flipper_format, "Key", key_str);

            char key2_str[32];
            snprintf(key2_str, sizeof(key2_str), "%02X %02X %02X %02X %02X %02X %02X %02X",
                    0, 0, 0, 0, 0, 0,
                    (unsigned int)((instance->key2_low >> 8) & 0xFF),
                    (unsigned int)(instance->key2_low & 0xFF));
            flipper_format_rewind(flipper_format);
            flipper_format_update_string_cstr(flipper_format, "Key_2", key2_str);

            instance->is_running = true;
            ret = SubGhzProtocolStatusOk;
        } else {
            ret = SubGhzProtocolStatusErrorParserOthers;
        }

    } while(false);

    furi_string_free(temp_str);
    return ret;
}

void subghz_protocol_encoder_psa_stop(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderPSA* instance = context;
    instance->is_running = false;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_psa_yield(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderPSA* instance = context;
    if(!instance->is_running || instance->encoder.size_upload == 0) {
        instance->is_running = false;
        return level_duration_reset();
    }
    LevelDuration ret = instance->encoder.upload[instance->encoder.front];
    instance->encoder.front++;
    if(instance->encoder.front >= instance->encoder.size_upload) {
        instance->encoder.front = 0;
        if(!subghz_block_generic_global.endless_tx) instance->encoder.repeat--;
        if(instance->encoder.repeat <= 0) instance->is_running = false;
    }
    return ret;
}

static const SubGhzProtocolEncoder enc_psa_vt = {
    .alloc = subghz_protocol_encoder_psa_alloc,
    .free = subghz_protocol_encoder_psa_free,
    .deserialize = subghz_protocol_encoder_psa_deserialize,
    .stop = subghz_protocol_encoder_psa_stop,
    .yield = subghz_protocol_encoder_psa_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_psa = {
    .name = SUBGHZ_PROTOCOL_PSA_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_psa_vt,
};

static const SubGhzEncodePluginEntry enc_psa_entry = {.name = SUBGHZ_PROTOCOL_PSA_NAME, .encoder = &enc_psa_vt};
static const FlipperAppPluginDescriptor enc_psa_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_psa_entry};
const FlipperAppPluginDescriptor* enc_psa_ep(void) { return &enc_psa_descriptor; }
