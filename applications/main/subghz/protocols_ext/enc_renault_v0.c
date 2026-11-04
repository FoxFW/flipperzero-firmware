/* enc_renault_v0.fal : SD encoder for renault_v0 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define renault_v0_protocol enc_renault_v0__renault_v0_protocol
#define subghz_protocol_encoder_common_free enc_renault_v0__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_yield enc_renault_v0__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_renault_v0_alloc enc_renault_v0__subghz_protocol_encoder_renault_v0_alloc
#define subghz_protocol_encoder_renault_v0_deserialize enc_renault_v0__subghz_protocol_encoder_renault_v0_deserialize
#define subghz_protocol_encoder_renault_v0_stop enc_renault_v0__subghz_protocol_encoder_renault_v0_stop
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/renault_v0.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/toolbox/level_duration.h>
#include <lib/subghz/protocols/common.h>

#define RENAULT_V0_MIN_BITS           0x52U
#define RENAULT_V0_UPLOAD_CAPACITY    0x258U
#define RENAULT_V0_TE_DEFAULT_US      125U
#define RENAULT_V0_TE_PREAMBLE_12_US  140U
#define RENAULT_V0_ROLLING_REPEAT     1U
#define RENAULT_V0_REPLAY_REPEAT      10U
#define RENAULT_V0_KEY2_FIELD    "Key2"
#define RENAULT_V0_PREAMBLE_FIELD "Preamble"

typedef struct {
    uint32_t low;
    uint32_t high;
} RenaultV0MatrixRow;

typedef enum {
    RenaultV0TypeUnknown = 0,
    RenaultV0Type13,
    RenaultV0Type04,
    RenaultV0Type0C,
    RenaultV0Type1A,
    RenaultV0Type3B,
    RenaultV0Type3F,
    RenaultV0TypeDynamic,
} RenaultV0TypeId;

typedef struct {
    RenaultV0TypeId id;
    uint8_t value;
    const char* name;
    uint8_t checksum_low6;
    uint8_t checksum_high2_xor;
} RenaultV0TypeEntry;

typedef struct {
    uint64_t data;
    uint32_t key2;
    uint32_t serial;
    uint8_t button;
    uint8_t counter;
    RenaultV0TypeId type_id;
    uint8_t type_tag;
    uint8_t preamble_bits;
    bool c1_ok;
    bool c2_ok;
    bool ic_ok;
} RenaultV0DecodeAttempt;

typedef struct SubGhzProtocolEncoderRenaultV0 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint16_t packet_bit_count;
    uint8_t tx_button;
    uint8_t preamble_bits;
    uint32_t key2;
} 
SubGhzProtocolEncoderRenaultV0;

typedef struct {
    uint8_t preamble_pairs;
    uint8_t burst_count;
    uint32_t te_short;
    uint32_t inter_burst_low;
    uint32_t final_low;
} RenaultV0UploadShape;

static const RenaultV0MatrixRow renault_v0_matrix[42] = {
    {0x00000001, 0x00000000},
    {0x04000029, 0x00000000},
    {0x0000001B, 0x00000000},
    {0x00000000, 0x00000000},
    {0x00000001, 0x00000000},
    {0x05220124, 0x00000000},
    {0x00000001, 0x00000000},
    {0x00088410, 0x00000000},
    {0x60132D1D, 0x00000000},
    {0x60170F87, 0x00001004},
    {0x00000000, 0x00000000},
    {0x002000A9, 0x00000000},
    {0x20863E01, 0x0000100C},
    {0x24BB3755, 0x00000004},
    {0x640199A4, 0x00000004},
    {0x24225C43, 0x00001004},
    {0x607886F1, 0x0000100C},
    {0x6007A101, 0x0000000C},
    {0x66672A10, 0x00000004},
    {0x4651623F, 0x00001008},
    {0x43380BBF, 0x00001008},
    {0x20237F84, 0x00001000},
    {0x4245755E, 0x00001008},
    {0x60AAF581, 0x00000004},
    {0x22722DAD, 0x0000000C},
    {0x27C617F7, 0x00000000},
    {0x46DE8F1B, 0x0000000C},
    {0x231DEC51, 0x00000000},
    {0x03ACAA0B, 0x00000008},
    {0x22D2BF81, 0x00000004},
    {0x626EF6AE, 0x0000100C},
    {0x40441F95, 0x0000000C},
    {0x00000001, 0x00000000},
    {0x00000000, 0x00000000},
    {0x20B9A590, 0x00000008},
    {0x656C8E86, 0x00001008},
    {0x60129F96, 0x0000000C},
    {0x2368F667, 0x00001000},
    {0x442A1A5C, 0x00000000},
    {0x04C43242, 0x0000100C},
    {0x22198640, 0x00001000},
    {0x23D6B958, 0x00001008},
};

static const RenaultV0TypeEntry renault_v0_types[] = {
    {RenaultV0Type13, 0x13U, "13", 0x13U, 0x00U},
    {RenaultV0Type04, 0x04U, "04", 0x04U, 0x00U},
    {RenaultV0Type0C, 0x0CU, "0C", 0x0CU, 0x00U},
    {RenaultV0Type1A, 0x1AU, "1A", 0x1AU, 0x03U},
    {RenaultV0Type3B, 0x3BU, "3B", 0x3BU, 0x00U},
    {RenaultV0Type3F, 0x3FU, "3F", 0x3FU, 0x00U},
};

static void renault_v0_set_split_bit(uint32_t* low, uint32_t* high, uint8_t bit);
static uint8_t renault_v0_parity32(uint32_t value);
static bool renault_v0_type_button_valid(RenaultV0TypeId type_id, uint8_t button);
static const RenaultV0TypeEntry* renault_v0_find_type_by_checks(
    uint8_t checksum,
    uint32_t key2,
    bool* c1_ok,
    bool* c2_ok);
static bool renault_v0_checksum_hi2xor_valid(uint8_t hi2xor);
static bool renault_v0_button_valid_generic(uint8_t button);
static bool renault_v0_preamble_bits_valid(uint8_t preamble_bits);
static uint8_t renault_v0_default_preamble_bits(RenaultV0TypeId type_id);
static void renault_v0_parse_fields(uint64_t data, uint32_t* serial, uint8_t* button, uint8_t* counter);
static void renault_v0_build_key(
    uint32_t serial,
    uint8_t button,
    uint8_t counter,
    uint64_t* out_data,
    uint32_t* out_key2);
static RenaultV0TypeId renault_v0_detect_type(uint8_t checksum, uint32_t key2, uint8_t button);
static bool renault_v0_classify_frame(uint64_t data, uint32_t key2, RenaultV0DecodeAttempt* attempt);
static void renault_v0_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]);
static uint8_t renault_v0_checksum(uint64_t data, uint32_t key2);
static bool renault_v0_model_matches(
    uint64_t data,
    uint32_t key2,
    uint32_t serial,
    uint8_t button,
    uint8_t counter);
static bool renault_v0_get_bit_msb82(uint64_t data, uint32_t key2, uint8_t bit_index);
static bool renault_v0_upload_shape_for_type(RenaultV0TypeId type_id, RenaultV0UploadShape* shape);
static bool renault_v0_upload_shape_for_preamble(uint8_t preamble_bits, RenaultV0UploadShape* shape);
static uint32_t renault_v0_upload_te_for_preamble(uint8_t preamble_bits);
static bool renault_v0_emit(
    SubGhzProtocolEncoderRenaultV0* instance,
    size_t* index,
    bool level,
    uint32_t duration);
static bool renault_v0_emit_decoded_bit(
    SubGhzProtocolEncoderRenaultV0* instance,
    size_t* index,
    uint8_t* state,
    uint32_t te_short,
    bool bit);
static bool renault_v0_build_upload(
    SubGhzProtocolEncoderRenaultV0* instance,
    RenaultV0TypeId type_id,
    uint8_t preamble_bits);

static void renault_v0_set_split_bit(uint32_t* low, uint32_t* high, uint8_t bit) {
    if(bit < 32U) {
        *low |= (1UL << bit);
    } else {
        *high |= (1UL << (bit - 32U));
    }
}

static uint8_t renault_v0_parity32(uint32_t value) {
    value ^= value >> 16U;
    value ^= value >> 8U;
    value ^= value >> 4U;
    value ^= value >> 2U;
    value ^= value >> 1U;
    return (uint8_t)(value & 1U);
}

static bool renault_v0_type_button_valid(RenaultV0TypeId type_id, uint8_t button) {
    switch(type_id) {
    case RenaultV0Type13:
        return (button == 0x06U) || (button == 0x0AU);
    case RenaultV0Type04:
        return (button >= 0x04U) && (button <= 0x0BU);
    case RenaultV0Type0C:
        return (button >= 0xC4U) && (button <= 0xCBU);
    case RenaultV0Type1A:
    case RenaultV0Type3B:
        return (button >= 0x44U) && (button <= 0x4BU);
    case RenaultV0Type3F:
        return (button >= 0xC4U) && (button <= 0xCBU);
    case RenaultV0TypeDynamic:
        return false;
    default:
        return false;
    }
}

static const RenaultV0TypeEntry* renault_v0_find_type_by_checks(
    uint8_t checksum,
    uint32_t key2,
    bool* c1_ok,
    bool* c2_ok) {
    const uint8_t checksum_low6 = checksum & 0x3FU;
    const uint8_t checksum_high2_xor =
        (uint8_t)(((checksum >> 6U) & 0x03U) ^ (key2 & 0x03U));

    if(c1_ok) {
        *c1_ok = false;
    }
    if(c2_ok) {
        *c2_ok = false;
    }

    for(size_t i = 0; i < COUNT_OF(renault_v0_types); i++) {
        const RenaultV0TypeEntry* type = &renault_v0_types[i];
        if(checksum_low6 != type->checksum_low6) {
            continue;
        }

        if(c1_ok) {
            *c1_ok = true;
        }
        if(checksum_high2_xor != type->checksum_high2_xor) {
            return NULL;
        }

        if(c2_ok) {
            *c2_ok = true;
        }
        return type;
    }

    return NULL;
}

static bool renault_v0_checksum_hi2xor_valid(uint8_t hi2xor) {
    return (hi2xor == 0x00U) || (hi2xor == 0x03U);
}

static bool renault_v0_button_valid_generic(uint8_t button) {
    if((button == 0x06U) || (button == 0x0AU)) {
        return true;
    }
    if((button >= 0x04U) && (button <= 0x0BU)) {
        return true;
    }
    if((button >= 0x44U) && (button <= 0x4BU)) {
        return true;
    }
    if((button >= 0xC4U) && (button <= 0xCBU)) {
        return true;
    }
    return false;
}

static bool renault_v0_preamble_bits_valid(uint8_t preamble_bits) {
    switch(preamble_bits) {
    case 12U:
    case 16U:
    case 18U:
    case 20U:
        return true;
    default:
        return false;
    }
}

static uint8_t renault_v0_default_preamble_bits(RenaultV0TypeId type_id) {
    switch(type_id) {
    case RenaultV0Type13:
    case RenaultV0Type04:
        return 16U;
    case RenaultV0Type0C:
        return 18U;
    case RenaultV0Type1A:
        return 12U;
    case RenaultV0Type3B:
    case RenaultV0Type3F:
        return 20U;
    default:
        return 0U;
    }
}

static void renault_v0_parse_fields(uint64_t data, uint32_t* serial, uint8_t* button, uint8_t* counter) {
    if(serial) {
        *serial = (uint32_t)(data >> 40U);
    }
    if(button) {
        *button = (uint8_t)(data >> 32U);
    }
    if(counter) {
        *counter = (uint8_t)(((uint32_t)data >> 24U) & 0xFFU);
    }
}

static void renault_v0_build_key(
    uint32_t serial,
    uint8_t button,
    uint8_t counter,
    uint64_t* out_data,
    uint32_t* out_key2) {
    uint8_t vars[7];
    uint8_t parity_bits[42];

    vars[0] = (button == 0x0AU) ? 1U : 0U;
    for(uint8_t bit = 0; bit < 6U; bit++) {
        vars[bit + 1U] = (counter >> bit) & 1U;
    }

    uint32_t mask_low = 1U;
    uint32_t mask_high = 0U;
    uint8_t mask_bit = 1U;

    for(uint8_t i = 0; i < 7U; i++, mask_bit++) {
        if(vars[i]) {
            renault_v0_set_split_bit(&mask_low, &mask_high, mask_bit);
        }
    }

    for(uint8_t i = 0; i < 6U; i++) {
        for(uint8_t j = i + 1U; j < 7U; j++, mask_bit++) {
            if(vars[i] & vars[j]) {
                renault_v0_set_split_bit(&mask_low, &mask_high, mask_bit);
            }
        }
    }

    for(uint8_t i = 0; i < 6U; i++) {
        for(uint8_t j = i + 1U; j < 7U; j++) {
            for(uint8_t k = j + 1U; k < 7U; k++, mask_bit++) {
                if(vars[i] & vars[j] & vars[k]) {
                    renault_v0_set_split_bit(&mask_low, &mask_high, mask_bit);
                }
            }
        }
    }

    for(size_t row = 0; row < COUNT_OF(renault_v0_matrix); row++) {
        const uint32_t mixed = (renault_v0_matrix[row].low & mask_low) ^
                               (renault_v0_matrix[row].high & mask_high);
        parity_bits[row] = renault_v0_parity32(mixed);
    }

    if(counter & 0x40U) {
        parity_bits[41] ^= 1U;
    }
    if((counter >> 7U) != 0U) {
        parity_bits[40] ^= 1U;
    }

    uint32_t data_low = ((uint32_t)counter) << 24U;
    uint32_t data_high = (serial << 8U) | (uint32_t)button;

    for(uint8_t i = 0; i < 24U; i++) {
        if(parity_bits[i]) {
            data_low |= 1UL << (23U - i);
        }
    }

    uint32_t key2 = 0U;
    for(uint8_t i = 24U; i < 42U; i++) {
        if(parity_bits[i]) {
            key2 |= 1UL << (41U - i);
        }
    }

    if(out_data) {
        *out_data = ((uint64_t)data_high << 32U) | data_low;
    }
    if(out_key2) {
        *out_key2 = key2;
    }
}

static RenaultV0TypeId renault_v0_detect_type(uint8_t checksum, uint32_t key2, uint8_t button) {
    const RenaultV0TypeEntry* type =
        renault_v0_find_type_by_checks(checksum, key2, NULL, NULL);
    if(type && renault_v0_type_button_valid(type->id, button)) {
        return type->id;
    }

    return RenaultV0TypeUnknown;
}

static bool renault_v0_classify_frame(uint64_t data, uint32_t key2, RenaultV0DecodeAttempt* attempt) {
    furi_check(attempt);

    uint32_t serial = 0U;
    uint8_t button = 0U;
    uint8_t counter = 0U;
    renault_v0_parse_fields(data, &serial, &button, &counter);

    const uint8_t checksum = renault_v0_checksum(data, key2);
    const uint8_t checksum_low6 = checksum & 0x3FU;
    const uint8_t checksum_high2_xor =
        (uint8_t)(((checksum >> 6U) & 0x03U) ^ (key2 & 0x03U));
    bool c1_ok = false;
    bool c2_ok = false;
    const RenaultV0TypeEntry* type =
        renault_v0_find_type_by_checks(checksum, key2, &c1_ok, &c2_ok);
    const bool ic_ok = renault_v0_model_matches(data, key2, serial, button, counter);

    attempt->data = data;
    attempt->key2 = key2;
    attempt->serial = serial;
    attempt->button = button;
    attempt->counter = counter;
    attempt->c1_ok = c1_ok;
    attempt->c2_ok = c2_ok;
    attempt->ic_ok = ic_ok;

    if(type && c1_ok && c2_ok && renault_v0_type_button_valid(type->id, button)) {
        attempt->type_id = type->id;
        attempt->type_tag = type->value;
        return true;
    }

    const bool dynamic_c1_ok =
        renault_v0_button_valid_generic(button) && (serial != 0U) && (serial <= 0xFFFFFFU);
    const bool dynamic_c2_ok = renault_v0_checksum_hi2xor_valid(checksum_high2_xor);
    attempt->c1_ok = dynamic_c1_ok;
    attempt->c2_ok = dynamic_c2_ok;

    if(dynamic_c1_ok && dynamic_c2_ok) {
        attempt->type_id = RenaultV0TypeDynamic;
        attempt->type_tag = checksum_low6;
        return true;
    }

    return false;
}

static void renault_v0_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]) {
    for(size_t i = 0; i < 8; i++) {
        bytes[i] = (uint8_t)((data >> ((7U - i) * 8U)) & 0xFFU);
    }
}

static uint8_t renault_v0_checksum(uint64_t data, uint32_t key2) {
    uint8_t bytes[10];
    renault_v0_u64_to_bytes_be(data, bytes);
    bytes[8] = (uint8_t)((key2 >> 10U) & 0xFFU);
    bytes[9] = (uint8_t)((key2 >> 2U) & 0xFFU);

    uint8_t checksum = 0U;
    for(size_t i = 0; i < COUNT_OF(bytes); i++) {
        checksum ^= bytes[i];
    }
    return checksum;
}

static bool renault_v0_model_matches(
    uint64_t data,
    uint32_t key2,
    uint32_t serial,
    uint8_t button,
    uint8_t counter) {
    uint64_t rebuilt_data = 0ULL;
    uint32_t rebuilt_key2 = 0U;
    renault_v0_build_key(serial, button, counter, &rebuilt_data, &rebuilt_key2);
    return (rebuilt_data == data) && (rebuilt_key2 == key2);
}

static bool renault_v0_get_bit_msb82(uint64_t data, uint32_t key2, uint8_t bit_index) {
    if(bit_index <= 0x3FU) {
        return ((data >> (63U - bit_index)) & 1ULL) != 0ULL;
    }

    return ((key2 >> (0x51U - bit_index)) & 1U) != 0U;
}

static bool renault_v0_upload_shape_for_type(RenaultV0TypeId type_id, RenaultV0UploadShape* shape) {
    furi_check(shape);

    switch(type_id) {
    case RenaultV0Type13:
        shape->preamble_pairs = 16U;
        shape->burst_count = 3U;
        shape->te_short = RENAULT_V0_TE_DEFAULT_US;
        shape->inter_burst_low = 0x61A8U;
        shape->final_low = 250U;
        return true;
    case RenaultV0Type04:
    case RenaultV0Type0C:
    case RenaultV0Type1A:
    case RenaultV0Type3B:
    case RenaultV0Type3F:
        return renault_v0_upload_shape_for_preamble(
            renault_v0_default_preamble_bits(type_id), shape);
    default:
        return false;
    }
}

static bool renault_v0_upload_shape_for_preamble(uint8_t preamble_bits, RenaultV0UploadShape* shape) {
    furi_check(shape);

    if(!renault_v0_preamble_bits_valid(preamble_bits)) {
        return false;
    }

    shape->preamble_pairs = preamble_bits;
    shape->burst_count = 3U;
    shape->te_short = renault_v0_upload_te_for_preamble(preamble_bits);
    shape->inter_burst_low = 1500U;
    shape->final_low = 1500U;
    return true;
}

static uint32_t renault_v0_upload_te_for_preamble(uint8_t preamble_bits) {
    if(preamble_bits == 12U) {
        return RENAULT_V0_TE_PREAMBLE_12_US;
    }
    return RENAULT_V0_TE_DEFAULT_US;
}

static bool renault_v0_emit(
    SubGhzProtocolEncoderRenaultV0* instance,
    size_t* index,
    bool level,
    uint32_t duration) {
    furi_check(instance);
    furi_check(index);

    size_t idx = *index;
    if(idx > 0 && level_duration_get_level(instance->encoder.upload[idx - 1]) == level) {
        uint32_t prev = level_duration_get_duration(instance->encoder.upload[idx - 1]);
        instance->encoder.upload[idx - 1] = level_duration_make(level, prev + duration);
        *index = idx;
        return true;
    }

    if(idx >= RENAULT_V0_UPLOAD_CAPACITY) {
        return false;
    }
    instance->encoder.upload[idx] = level_duration_make(level, duration);
    *index = idx + 1;
    return true;
}

static bool renault_v0_emit_decoded_bit(
    SubGhzProtocolEncoderRenaultV0* instance,
    size_t* index,
    uint8_t* state,
    uint32_t te_short,
    bool bit) {
    furi_check(state);

    const uint32_t te_long = te_short * 2U;

    if(*state == 1U) {
        if(bit) {
            return renault_v0_emit(instance, index, false, te_short) &&
                   renault_v0_emit(instance, index, true, te_short);
        }

        *state = 2U;
        return renault_v0_emit(instance, index, false, te_long);
    }

    if(bit) {
        *state = 1U;
        return renault_v0_emit(instance, index, true, te_long);
    }

    return renault_v0_emit(instance, index, true, te_short) &&
           renault_v0_emit(instance, index, false, te_short);
}

static bool renault_v0_build_upload(
    SubGhzProtocolEncoderRenaultV0* instance,
    RenaultV0TypeId type_id,
    uint8_t preamble_bits) {
    furi_check(instance);

    RenaultV0UploadShape shape;
    if(type_id == RenaultV0Type13) {
        if(!renault_v0_upload_shape_for_type(type_id, &shape)) {
            return false;
        }
    } else if(renault_v0_preamble_bits_valid(preamble_bits)) {
        if(!renault_v0_upload_shape_for_preamble(preamble_bits, &shape)) {
            return false;
        }
    } else {
        if(!renault_v0_upload_shape_for_type(type_id, &shape)) {
            return false;
        }
    }

    size_t write_index = 0U;
    for(uint8_t burst = 0U; burst < shape.burst_count; burst++) {
        if(!renault_v0_emit(instance, &write_index, true, 1000U)) {
            return false;
        }

        uint8_t state = 1U;
        for(uint8_t pair = 0U; pair < shape.preamble_pairs; pair++) {
            if(!renault_v0_emit_decoded_bit(
                   instance, &write_index, &state, shape.te_short, true)) {
                return false;
            }
        }

        for(uint8_t bit_index = 0U; bit_index < RENAULT_V0_MIN_BITS; bit_index++) {
            const bool bit =
                renault_v0_get_bit_msb82(instance->generic.data, instance->key2, bit_index);
            if(!renault_v0_emit_decoded_bit(
                   instance, &write_index, &state, shape.te_short, bit)) {
                return false;
            }
        }

        if(state == 2U) {
            if(!renault_v0_emit(instance, &write_index, true, shape.te_short)) {
                return false;
            }
        }

        const uint32_t trailing_low =
            (burst + 1U < shape.burst_count) ? shape.inter_burst_low : shape.final_low;
        if(!renault_v0_emit(instance, &write_index, false, trailing_low)) {
            return false;
        }
    }

    instance->encoder.size_upload = write_index;
    instance->encoder.front = 0U;
    return true;
}

/* vendored from lib/subghz/protocols/common.c (not exported) */
void subghz_protocol_encoder_common_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderCommon* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

/* vendored from lib/subghz/protocols/common.c (not exported) */
LevelDuration subghz_protocol_encoder_common_yield(void* context) {
    SubGhzProtocolEncoderCommon* instance = context;

    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        if(!subghz_block_generic_global.endless_tx) instance->encoder.repeat--;
        instance->encoder.front = 0;
    }

    return ret;
}

void* subghz_protocol_encoder_renault_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolEncoderRenaultV0* instance = calloc(1, sizeof(SubGhzProtocolEncoderRenaultV0));
    furi_check(instance);

    instance->base.protocol = &renault_v0_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 1U;
    instance->encoder.front = 0;
    instance->encoder.is_running = false;
    instance->encoder.upload = malloc(RENAULT_V0_UPLOAD_CAPACITY * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);

    return instance;
}

void subghz_protocol_encoder_renault_v0_stop(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderRenaultV0* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_renault_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);

    SubGhzProtocolEncoderRenaultV0* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    instance->encoder.is_running = false;
    instance->encoder.front = 0;

    do {
        flipper_format_rewind(flipper_format);
        FuriString* temp_str = furi_string_alloc();
        if(!temp_str) break;
        if(!flipper_format_read_string(flipper_format, "Protocol", temp_str)) {
            furi_string_free(temp_str);
            break;
        }
        if(!furi_string_equal(temp_str, instance->base.protocol->name)) {
            furi_string_free(temp_str);
            break;
        }
        furi_string_free(temp_str);

        flipper_format_rewind(flipper_format);
        SubGhzProtocolStatus load_st = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic, flipper_format, RENAULT_V0_MIN_BITS);
        if(load_st != SubGhzProtocolStatusOk) {
            break;
        }

        if(!flipper_format_rewind(flipper_format)) {
            break;
        }

        uint32_t key2 = 0U;
        if(!flipper_format_read_uint32(flipper_format, RENAULT_V0_KEY2_FIELD, &key2, 1)) {
            break;
        }
        instance->key2 = key2;
        const uint64_t captured_data = instance->generic.data;
        const uint32_t captured_key2 = instance->key2;

        uint32_t preamble_bits = 0U;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(
               flipper_format, RENAULT_V0_PREAMBLE_FIELD, &preamble_bits, 1) &&
           renault_v0_preamble_bits_valid((uint8_t)preamble_bits)) {
            instance->preamble_bits = (uint8_t)preamble_bits;
        } else {
            instance->preamble_bits = 0U;
        }

        uint32_t serial = 0;
        uint8_t button = 0;
        uint8_t counter = 0;
        renault_v0_parse_fields(instance->generic.data, &serial, &button, &counter);
        const uint32_t captured_serial = serial;
        const uint8_t captured_button = button;
        const uint8_t captured_counter = counter;

        RenaultV0DecodeAttempt captured_attempt = {0};
        if(!renault_v0_classify_frame(captured_data, captured_key2, &captured_attempt)) {
            break;
        }
        const RenaultV0TypeId captured_type = captured_attempt.type_id;
        if(instance->preamble_bits == 0U) {
            instance->preamble_bits = renault_v0_default_preamble_bits(captured_type);
        }
        captured_attempt.preamble_bits = instance->preamble_bits;
        if((captured_type == RenaultV0TypeDynamic) &&
           !renault_v0_preamble_bits_valid(instance->preamble_bits)) {
            break;
        }
        const bool rolling_supported = (captured_type == RenaultV0Type13) && captured_attempt.ic_ok;

        if(rolling_supported) {
            uint32_t serial_u32 = serial;
            uint32_t btn_u32 = button;
            uint32_t cnt_u32 = counter;

            flipper_format_rewind(flipper_format);
            flipper_format_read_uint32(flipper_format, "Serial", &serial_u32, 1);
            flipper_format_rewind(flipper_format);
            const bool got_btn = flipper_format_read_uint32(flipper_format, "Btn", &btn_u32, 1);
            flipper_format_rewind(flipper_format);
            const bool got_cnt = flipper_format_read_uint32(flipper_format, "Cnt", &cnt_u32, 1);

            if(!got_cnt) {
                uint32_t mult = furi_hal_subghz_get_rolling_counter_mult();
                if(mult == 0U) mult = 1U;
                cnt_u32 = (cnt_u32 + mult) & 0xFFU;
            }

            if(!got_btn) {
                subghz_custom_btn_set_max(4);
                switch(subghz_custom_btn_get()) {
                case SUBGHZ_CUSTOM_BTN_UP:
                    btn_u32 = 0x06U;
                    break;
                case SUBGHZ_CUSTOM_BTN_DOWN:
                    btn_u32 = 0x0AU;
                    break;
                case SUBGHZ_CUSTOM_BTN_OK:
                case SUBGHZ_CUSTOM_BTN_LEFT:
                case SUBGHZ_CUSTOM_BTN_RIGHT:
                default:
                    break;
                }
            }

            instance->tx_button = (uint8_t)btn_u32;
            if(!renault_v0_type_button_valid(captured_type, instance->tx_button)) {
                break;
            }
            counter = (uint8_t)(cnt_u32 & 0xFFU);
            serial = serial_u32 & 0x00FFFFFFU;

            renault_v0_build_key(
                serial, instance->tx_button, counter, &instance->generic.data, &instance->key2);
            const uint8_t generated_checksum =
                renault_v0_checksum(instance->generic.data, instance->key2);
            if(renault_v0_detect_type(
                   generated_checksum, instance->key2, instance->tx_button) != RenaultV0Type13) {
                break;
            }
        } else {
            instance->tx_button = captured_button;
            serial = captured_serial;
            counter = captured_counter;
            instance->generic.data = captured_data;
            instance->key2 = captured_key2;
        }

        instance->packet_bit_count = RENAULT_V0_MIN_BITS;
        instance->generic.data_count_bit = RENAULT_V0_MIN_BITS;
        instance->generic.serial = serial;
        instance->generic.btn = instance->tx_button;
        instance->generic.cnt = counter;

        const uint32_t default_repeat =
            rolling_supported ? RENAULT_V0_ROLLING_REPEAT : RENAULT_V0_REPLAY_REPEAT;
        uint32_t tx_repeat = default_repeat;
        flipper_format_rewind(flipper_format);
        flipper_format_read_uint32(flipper_format, "Repeat", &tx_repeat, 1);
        if(tx_repeat == 0U) {
            tx_repeat = default_repeat;
        }
        if(!rolling_supported && (tx_repeat < RENAULT_V0_REPLAY_REPEAT)) {
            tx_repeat = RENAULT_V0_REPLAY_REPEAT;
        }
        instance->encoder.repeat = tx_repeat;

        if(!renault_v0_build_upload(instance, captured_type, instance->preamble_bits)) {
            break;
        }
        if(instance->encoder.size_upload == 0) {
            break;
        }

        if(rolling_supported) {
            flipper_format_rewind(flipper_format);
            uint8_t key_data[8];
            renault_v0_u64_to_bytes_be(instance->generic.data, key_data);
            if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(key_data))) {
                flipper_format_rewind(flipper_format);
                if(!flipper_format_insert_or_update_hex(
                       flipper_format, "Key", key_data, sizeof(key_data))) {
                    break;
                }
            }

            flipper_format_rewind(flipper_format);
            if(!flipper_format_update_uint32(
                   flipper_format, RENAULT_V0_KEY2_FIELD, &instance->key2, 1)) {
                flipper_format_rewind(flipper_format);
                if(!flipper_format_insert_or_update_uint32(
                       flipper_format, RENAULT_V0_KEY2_FIELD, &instance->key2, 1)) {
                    break;
                }
            }

            flipper_format_rewind(flipper_format);
            uint32_t cnt_store = instance->generic.cnt;
            flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &cnt_store, 1);
        }

        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_renault_v0_vt = {
    .alloc = subghz_protocol_encoder_renault_v0_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_renault_v0_deserialize,
    .stop = subghz_protocol_encoder_renault_v0_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol renault_v0_protocol = {
    .name = RENAULT_PROTOCOL_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 |
            SubGhzProtocolFlag_868 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_renault_v0_vt,
};

static const SubGhzEncodePluginEntry enc_renault_v0_entry = {.name = RENAULT_PROTOCOL_V0_NAME, .encoder = &enc_renault_v0_vt};
static const FlipperAppPluginDescriptor enc_renault_v0_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_renault_v0_entry};
const FlipperAppPluginDescriptor* enc_renault_v0_ep(void) { return &enc_renault_v0_descriptor; }
