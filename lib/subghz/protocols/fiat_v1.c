#include "fiat_v1.h"

#include "common.h"
#include "../encode_plugin.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>
#include <lib/subghz/blocks/bitbuf.h>

#define TAG "FiatProtocolV1"

#define FIAT_V1_TE_SHORT          250U
#define FIAT_V1_TE_LONG           500U
#define FIAT_V1_TE_DELTA          100U
#define FIAT_V1_TE_B_SHORT        100U
#define FIAT_V1_TE_B_LONG         200U
#define FIAT_V1_TE_B_DELTA        50U
#define FIAT_V1_WIRE_BITS         104U
#define FIAT_V1_WIRE_BYTES        13U
#define FIAT_V1_WIRE_CELLS        (FIAT_V1_WIRE_BITS * 2U)
#define FIAT_V1_LOGICAL_BITS      102U
#define FIAT_V1_VARIANT_COUNT     2U
#define FIAT_V1_BOUNDARY_MIN_US   800U
#define FIAT_V1_DEFAULT_TAIL_BITS 2U
#define FIAT_V1_RAW_FIELD         "Raw"
#define FIAT_V1_HOP_FIELD         "Hop"
#define FIAT_V1_TAIL_BITS_FIELD   "Tail Bits"
#define FIAT_V1_XOR_FIELD         "XOR"
#define FIAT_V1_KNOWN_KEY_COUNT   95U
// Only the first N keys feed the slow full-epoch sweep; the rest extend the fast
// epoch-0 verify + Renault find path only (see the fiat_v1_known_keys comment).
#define FIAT_V1_HITAG2_EPOCH_SPACE 0x40000UL

#define FIAT_V1_ENC_LEAD_US        2033U
#define FIAT_V1_ENC_GAP_US         3252U
#define FIAT_V1_ENC_DEFAULT_REPEAT 6U
#define FIAT_V1_UPLOAD_CAPACITY    240U

static const SubGhzBlockConst subghz_protocol_fiat_v1_const = {
    .te_short = FIAT_V1_TE_SHORT,
    .te_long = FIAT_V1_TE_LONG,
    .te_delta = FIAT_V1_TE_DELTA,
    .min_count_bit_for_found = FIAT_V1_LOGICAL_BITS,
};

static const SubGhzBlockConst subghz_protocol_fiat_v1_const_b = {
    .te_short = FIAT_V1_TE_B_SHORT,
    .te_long = FIAT_V1_TE_B_LONG,
    .te_delta = FIAT_V1_TE_B_DELTA,
    .min_count_bit_for_found = FIAT_V1_LOGICAL_BITS,
};

static const SubGhzBlockConst* fiat_v1_variant_const(uint8_t variant) {
    return (variant == 0U) ? &subghz_protocol_fiat_v1_const : &subghz_protocol_fiat_v1_const_b;
}

typedef enum {
    FiatV1DecoderStepReset = 0,
    FiatV1DecoderStepData = 1,
} FiatV1DecoderStep;

struct SubGhzProtocolDecoderFiatV1 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t cells[FIAT_V1_VARIANT_COUNT][SUBGHZ_BITBUF_BYTES(FIAT_V1_WIRE_CELLS)];
    uint16_t cell_count[FIAT_V1_VARIANT_COUNT];

    uint8_t raw_data[FIAT_V1_WIRE_BYTES];
    uint8_t last_raw_data[FIAT_V1_WIRE_BYTES];
    bool last_raw_valid;

    uint32_t uid;
    uint32_t hop;
    uint8_t family;
    uint8_t tail_bits;
    uint8_t frame_xor;

    uint8_t hitag2_key[6];
    uint32_t hitag2_epoch;
    bool hitag2_key_valid;
};

struct SubGhzProtocolEncoderFiatV1 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_data[FIAT_V1_WIRE_BYTES];
    uint8_t hitag2_key[6];
    uint32_t epoch;
    uint32_t hop;
    uint8_t tail_bits;
    uint8_t frame_xor;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderFiatV1);


SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderFiatV1);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderFiatV1);

static bool fiat_v1_feed_data_pulse(
    SubGhzProtocolDecoderFiatV1* instance,
    bool level,
    uint32_t duration);
static bool fiat_v1_frame_valid(const uint8_t raw[FIAT_V1_WIRE_BYTES]);
static void fiat_v1_build_raw(
    uint8_t raw[FIAT_V1_WIRE_BYTES],
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    uint32_t auth,
    uint8_t tail_bits);
static void fiat_v1_verify_hitag2_key(SubGhzProtocolDecoderFiatV1* instance);



const SubGhzProtocolDecoder subghz_protocol_fiat_v1_decoder = {
    .alloc = subghz_protocol_decoder_fiat_v1_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_fiat_v1_feed,
    .reset = subghz_protocol_decoder_fiat_v1_reset,
    .get_hash_data = subghz_protocol_decoder_fiat_v1_get_hash_data,
    .serialize = subghz_protocol_decoder_fiat_v1_serialize,
    .deserialize = subghz_protocol_decoder_fiat_v1_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_fiat_v1_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_fiat_v1");

const SubGhzProtocol fiat_v1_protocol = {
    .name = FIAT_V1_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = &subghz_protocol_fiat_v1_decoder,
    .encoder = &subghz_protocol_fiat_v1_encoder_ext.vtable,
};

static bool fiat_v1_duration_is_short(uint8_t variant, uint32_t duration) {
    const SubGhzBlockConst* c = fiat_v1_variant_const(variant);
    return DURATION_DIFF(duration, c->te_short) < c->te_delta;
}

static bool fiat_v1_duration_is_long(uint8_t variant, uint32_t duration) {
    const SubGhzBlockConst* c = fiat_v1_variant_const(variant);
    return DURATION_DIFF(duration, c->te_long) < c->te_delta;
}

static bool fiat_v1_duration_is_pulse(uint32_t duration) {
    for(uint8_t variant = 0U; variant < FIAT_V1_VARIANT_COUNT; variant++) {
        if(fiat_v1_duration_is_short(variant, duration) ||
           fiat_v1_duration_is_long(variant, duration)) {
            return true;
        }
    }
    return false;
}

static bool fiat_v1_button_valid(uint8_t button) {
    return button == 0x1U || button == 0x2U || button == 0x4U || button == 0x8U;
}

static uint8_t fiat_v1_frame_xor(const uint8_t raw[FIAT_V1_WIRE_BYTES]) {
    uint8_t value = 0x01U;
    for(uint8_t i = 0U; i < FIAT_V1_WIRE_BYTES - 1U; i++) {
        value ^= raw[i];
    }
    return value;
}

static void fiat_v1_build_raw(
    uint8_t raw[FIAT_V1_WIRE_BYTES],
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    uint32_t auth,
    uint8_t tail_bits) {
    memset(raw, 0, FIAT_V1_WIRE_BYTES);
    raw[1] = 0x01U;
    raw[2] = (uint8_t)(uid >> 24U);
    raw[3] = (uint8_t)(uid >> 16U);
    raw[4] = (uint8_t)(uid >> 8U);
    raw[5] = (uint8_t)uid;
    raw[6] = (uint8_t)(((button & 0x0FU) << 4U) | ((control >> 6U) & 0x0FU));
    raw[7] = (uint8_t)(((control & 0x3FU) << 2U) | ((auth >> 30U) & 0x03U));
    raw[8] = (uint8_t)(auth >> 22U);
    raw[9] = (uint8_t)(auth >> 14U);
    raw[10] = (uint8_t)(auth >> 6U);
    raw[11] = (uint8_t)((auth << 2U) | (tail_bits & 0x03U));
    raw[12] = fiat_v1_frame_xor(raw);
}

static uint32_t fiat_v1_uid(const uint8_t raw[FIAT_V1_WIRE_BYTES]) {
    return ((uint32_t)raw[2] << 24U) | ((uint32_t)raw[3] << 16U) |
           ((uint32_t)raw[4] << 8U) | raw[5];
}

static uint32_t fiat_v1_counter(const uint8_t raw[FIAT_V1_WIRE_BYTES]) {
    return ((uint32_t)(raw[6] & 0x0FU) << 6U) | (raw[7] >> 2U);
}

static uint32_t fiat_v1_hop(const uint8_t raw[FIAT_V1_WIRE_BYTES]) {
    return ((uint32_t)(raw[7] & 0x03U) << 30U) | ((uint32_t)raw[8] << 22U) |
           ((uint32_t)raw[9] << 14U) | ((uint32_t)raw[10] << 6U) | (raw[11] >> 2U);
}

static bool fiat_v1_frame_valid(const uint8_t raw[FIAT_V1_WIRE_BYTES]) {
    if(raw[0] != 0x00U || raw[1] != 0x01U) {
        return false;
    }
    if(fiat_v1_frame_xor(raw) != raw[12]) {
        return false;
    }
    if(!fiat_v1_button_valid(raw[6] >> 4U)) {
        return false;
    }

    const uint32_t uid = fiat_v1_uid(raw);
    return uid != 0U && uid != UINT32_MAX;
}

static void fiat_v1_clear_cells(SubGhzProtocolDecoderFiatV1* instance, uint8_t variant) {
    instance->cell_count[variant] = 0U;
    memset(instance->cells[variant], 0, SUBGHZ_BITBUF_BYTES(FIAT_V1_WIRE_CELLS));
}

static void fiat_v1_clear_all_cells(SubGhzProtocolDecoderFiatV1* instance) {
    for(uint8_t variant = 0U; variant < FIAT_V1_VARIANT_COUNT; variant++) {
        fiat_v1_clear_cells(instance, variant);
    }
}

static void fiat_v1_decode_fields(SubGhzProtocolDecoderFiatV1* instance) {
    instance->family = instance->raw_data[1];
    instance->uid = fiat_v1_uid(instance->raw_data);
    instance->generic.serial = instance->uid;
    instance->generic.btn = instance->raw_data[6] >> 4U;
    instance->generic.cnt = fiat_v1_counter(instance->raw_data);
    instance->hop = fiat_v1_hop(instance->raw_data);
    instance->tail_bits = instance->raw_data[11] & 0x03U;
    instance->frame_xor = instance->raw_data[12];
    instance->generic.data = ((uint64_t)instance->generic.serial << 32U) | instance->hop;
    instance->generic.data_count_bit = FIAT_V1_LOGICAL_BITS;
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    fiat_v1_verify_hitag2_key(instance);

    if(instance->hitag2_key_valid) {
        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(instance->generic.btn);
        }
        subghz_custom_btn_set_max(4);
    }
}

static bool fiat_v1_commit(
    SubGhzProtocolDecoderFiatV1* instance,
    const uint8_t raw[FIAT_V1_WIRE_BYTES]) {
    if(!fiat_v1_frame_valid(raw)) {
        return false;
    }

    if(instance->last_raw_valid && memcmp(instance->last_raw_data, raw, FIAT_V1_WIRE_BYTES) == 0) {
        return true;
    }

    memcpy(instance->raw_data, raw, FIAT_V1_WIRE_BYTES);
    memcpy(instance->last_raw_data, raw, FIAT_V1_WIRE_BYTES);
    instance->last_raw_valid = true;
    fiat_v1_decode_fields(instance);

    FURI_LOG_D(
        TAG,
        "Accepted UID:%08lX Btn:%02X Cnt:%03lX Auth:%08lX XOR:%02X",
        (unsigned long)instance->uid,
        instance->generic.btn,
        (unsigned long)instance->generic.cnt,
        (unsigned long)instance->hop,
        instance->frame_xor);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
    return true;
}

static bool
    fiat_v1_try_decode_window(SubGhzProtocolDecoderFiatV1* instance, uint8_t variant, bool invert) {
    if(instance->cell_count[variant] != FIAT_V1_WIRE_CELLS) {
        return false;
    }

    const uint8_t* cells = instance->cells[variant];
    uint8_t raw[FIAT_V1_WIRE_BYTES] = {0};
    for(uint8_t bit_index = 0U; bit_index < FIAT_V1_WIRE_BITS; bit_index++) {
        const uint8_t first = subghz_bitbuf_get(cells, bit_index * 2U);
        const uint8_t second = subghz_bitbuf_get(cells, bit_index * 2U + 1U);
        if(first == second) {
            return false;
        }

        bool bit = first != 0U;
        if(invert) {
            bit = !bit;
        }
        if(bit) {
            raw[bit_index >> 3U] |= (uint8_t)(1U << (7U - (bit_index & 7U)));
        }
    }

    return fiat_v1_commit(instance, raw);
}

static void fiat_v1_try_decode(SubGhzProtocolDecoderFiatV1* instance, uint8_t variant) {
    if(fiat_v1_try_decode_window(instance, variant, false)) {
        return;
    }
    (void)fiat_v1_try_decode_window(instance, variant, true);
}

static void
    fiat_v1_push_cell(SubGhzProtocolDecoderFiatV1* instance, uint8_t variant, bool level) {
    uint8_t* cells = instance->cells[variant];
    if(instance->cell_count[variant] < FIAT_V1_WIRE_CELLS) {
        subghz_bitbuf_set(cells, instance->cell_count[variant]++, level ? 1U : 0U);
    } else {
        subghz_bitbuf_shift_down1(cells, FIAT_V1_WIRE_CELLS);
        subghz_bitbuf_set(cells, FIAT_V1_WIRE_CELLS - 1U, level ? 1U : 0U);
    }
    fiat_v1_try_decode(instance, variant);
}

static bool fiat_v1_feed_data_pulse(
    SubGhzProtocolDecoderFiatV1* instance,
    bool level,
    uint32_t duration) {
    bool matched = false;

    for(uint8_t variant = 0U; variant < FIAT_V1_VARIANT_COUNT; variant++) {
        if(fiat_v1_duration_is_short(variant, duration)) {
            fiat_v1_push_cell(instance, variant, level);
            matched = true;
        } else if(fiat_v1_duration_is_long(variant, duration)) {
            fiat_v1_push_cell(instance, variant, level);
            fiat_v1_push_cell(instance, variant, level);
            matched = true;
        } else {
            if(!level && duration >= FIAT_V1_BOUNDARY_MIN_US) {
                fiat_v1_push_cell(instance, variant, false);
            }
            fiat_v1_clear_cells(instance, variant);
        }
    }

    return matched;
}

static void fiat_v1_rebuild_raw(SubGhzProtocolDecoderFiatV1* instance) {
    fiat_v1_build_raw(
        instance->raw_data,
        instance->generic.serial,
        instance->generic.btn,
        (uint16_t)(instance->generic.cnt & 0x03FFU),
        instance->hop,
        instance->tail_bits);
    if(instance->family && instance->family != 0x01U) {
        instance->raw_data[1] = instance->family;
        instance->raw_data[12] = fiat_v1_frame_xor(instance->raw_data);
    }
    if(instance->frame_xor) {
        instance->raw_data[12] = instance->frame_xor;
    }
}

static uint8_t fiat_v1_truth(uint32_t table, uint8_t index) {
    return (uint8_t)((table >> index) & 1U);
}

static uint8_t fiat_v1_filter_index(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (uint8_t)((a << 3U) | (b << 2U) | (c << 1U) | d);
}

static uint8_t fiat_v1_byte_bit(uint8_t byte, uint8_t bit) {
    return (uint8_t)((byte >> bit) & 1U);
}

static uint8_t fiat_v1_bcm_hitag2_filter(const uint8_t state[6]) {
    uint8_t group = 0U;
    group |= fiat_v1_truth(
        0x2c79U,
        fiat_v1_filter_index(
            fiat_v1_byte_bit(state[0], 1U),
            fiat_v1_byte_bit(state[0], 2U),
            fiat_v1_byte_bit(state[0], 4U),
            fiat_v1_byte_bit(state[0], 5U)));
    group |= (uint8_t)(fiat_v1_truth(
                           0x6671U,
                           fiat_v1_filter_index(
                               fiat_v1_byte_bit(state[1], 0U),
                               fiat_v1_byte_bit(state[1], 1U),
                               fiat_v1_byte_bit(state[1], 3U),
                               fiat_v1_byte_bit(state[1], 7U)))
                       << 1U);
    group |= (uint8_t)(fiat_v1_truth(
                           0x6671U,
                           fiat_v1_filter_index(
                               fiat_v1_byte_bit(state[3], 5U),
                               fiat_v1_byte_bit(state[2], 0U),
                               fiat_v1_byte_bit(state[2], 2U),
                               fiat_v1_byte_bit(state[2], 6U)))
                       << 2U);
    group |= (uint8_t)(fiat_v1_truth(
                           0x6671U,
                           fiat_v1_filter_index(
                               fiat_v1_byte_bit(state[4], 6U),
                               fiat_v1_byte_bit(state[3], 0U),
                               fiat_v1_byte_bit(state[3], 2U),
                               fiat_v1_byte_bit(state[3], 3U)))
                       << 3U);
    group |= (uint8_t)(fiat_v1_truth(
                           0x2c79U,
                           fiat_v1_filter_index(
                               fiat_v1_byte_bit(state[5], 1U),
                               fiat_v1_byte_bit(state[5], 3U),
                               fiat_v1_byte_bit(state[5], 4U),
                               fiat_v1_byte_bit(state[4], 5U)))
                       << 4U);
    return fiat_v1_truth(0x7907287bUL, group);
}

static uint8_t fiat_v1_parity8(uint8_t value) {
    value ^= (uint8_t)(value >> 4U);
    value ^= (uint8_t)(value >> 2U);
    value ^= (uint8_t)(value >> 1U);
    return value & 1U;
}

static uint8_t fiat_v1_bcm_hitag2_feedback(const uint8_t state[6]) {
    static const uint8_t masks[6] = {0xb3U, 0x80U, 0x83U, 0x22U, 0x00U, 0x73U};
    uint8_t feedback = 0U;
    for(uint8_t i = 0U; i < 6U; i++) {
        feedback ^= fiat_v1_parity8((uint8_t)(state[i] & masks[i]));
    }
    return feedback & 1U;
}

static void fiat_v1_bcm_hitag2_shift(uint8_t state[6], uint8_t input) {
    for(uint8_t i = 0U; i < 5U; i++) {
        state[i] = (uint8_t)((state[i] << 1U) | (state[i + 1U] >> 7U));
    }
    state[5] = (uint8_t)((state[5] << 1U) | (input & 1U));
}

static uint8_t fiat_v1_input_bit_u32_be(uint32_t value, uint8_t index) {
    return (uint8_t)((value >> (31U - index)) & 1U);
}

static uint8_t fiat_v1_input_bit_bytes_be(const uint8_t* bytes, uint8_t index) {
    return (uint8_t)((bytes[index >> 3U] >> (7U - (index & 7U))) & 1U);
}

static uint32_t fiat_v1_bcm_generate_authenticator(
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    const uint8_t key[6],
    uint32_t epoch) {
    uint8_t state[6] = {
        (uint8_t)(uid >> 24U),
        (uint8_t)(uid >> 16U),
        (uint8_t)(uid >> 8U),
        (uint8_t)uid,
        key[4],
        key[5],
    };

    const uint32_t iv =
        ((epoch & 0x3FFFFUL) << 14U) | (((uint32_t)control & 0x03FFUL) << 4U) |
        ((uint32_t)button & 0x0FUL);

    for(uint8_t i = 0U; i < 32U; i++) {
        const uint8_t input = fiat_v1_input_bit_u32_be(iv, i) ^
                              fiat_v1_input_bit_bytes_be(key, i) ^
                              fiat_v1_bcm_hitag2_filter(state);
        fiat_v1_bcm_hitag2_shift(state, input);
    }

    uint32_t authenticator = 0U;
    for(uint8_t i = 0U; i < 32U; i++) {
        authenticator = (authenticator << 1U) | fiat_v1_bcm_hitag2_filter(state);
        fiat_v1_bcm_hitag2_shift(state, fiat_v1_bcm_hitag2_feedback(state));
    }
    return authenticator;
}

static const uint8_t fiat_v1_known_keys[FIAT_V1_KNOWN_KEY_COUNT][6] = {
    // The first FIAT_V1_DEEP_SWEEP_KEY_COUNT entries are the original
    // high-confidence keys used by the slow full-epoch sweep (fiat_v1_hitag2_recover).
    // The remainder extend only the fast epoch-0 verify + Renault find-known-key
    // path (curated from the community KeeLoq-RX hitag2_keys.py, same 6-byte
    // MSB-first format). Keeping the deep sweep on the first 8 holds its time and
    // false-positive rate down; the extra keys catch manufacturer/factory keys at
    // epoch 0, the common case. (Expanded 2026-10-03.)
    {0xB7U, 0x92U, 0x80U, 0xAEU, 0xCCU, 0x37U}, // deep-sweep
    {0xD4U, 0x24U, 0x28U, 0xF7U, 0xD9U, 0x66U}, // deep-sweep
    {0x4DU, 0x34U, 0x3FU, 0xD4U, 0xE7U, 0xB6U}, // deep-sweep
    {0x6DU, 0x6BU, 0xF2U, 0x1DU, 0x3AU, 0x1AU}, // deep-sweep
    {0xA3U, 0xF3U, 0xACU, 0xF7U, 0xB9U, 0x10U}, // deep-sweep
    {0x4DU, 0x49U, 0x4BU, 0x52U, 0x4FU, 0x4EU}, // MIKRON, deep-sweep
    {0xCDU, 0x49U, 0x4BU, 0x52U, 0x4FU, 0x4EU}, // MIKRON hi-bit, deep-sweep
    {0x33U, 0xFAU, 0x2FU, 0xCDU, 0xC3U, 0x3BU}, // deep-sweep
    {0x4DU, 0x49U, 0x4BU, 0x52U, 0x4FU, 0x4DU}, // MIKROM
    {0x4DU, 0x69U, 0x6BU, 0x72U, 0x6FU, 0x6EU}, // Mikron
    {0x6DU, 0x69U, 0x6BU, 0x72U, 0x6FU, 0x6EU}, // mikron
    {0x44U, 0x45U, 0x4CU, 0x50U, 0x48U, 0x49U}, // DELPHI
    {0x44U, 0x65U, 0x6CU, 0x70U, 0x68U, 0x69U}, // Delphi
    {0x4DU, 0x41U, 0x52U, 0x45U, 0x4CU, 0x4CU}, // MARELL
    {0x4DU, 0x61U, 0x72U, 0x65U, 0x6CU, 0x6CU}, // Marell
    {0x4DU, 0x41U, 0x52U, 0x45U, 0x4CU, 0x49U}, // MARELI
    {0x43U, 0x4FU, 0x4EU, 0x54U, 0x49U, 0x4EU}, // CONTIN
    {0x53U, 0x49U, 0x45U, 0x4DU, 0x45U, 0x4EU}, // SIEMEN
    {0x42U, 0x4FU, 0x53U, 0x43U, 0x48U, 0x21U}, // BOSCH!
    {0x56U, 0x44U, 0x4FU, 0x43U, 0x41U, 0x52U}, // VDOCAR
    {0x46U, 0x49U, 0x41U, 0x54U, 0x30U, 0x30U}, // FIAT00
    {0x46U, 0x69U, 0x61U, 0x74U, 0x56U, 0x31U}, // FiatV1
    {0x41U, 0x4CU, 0x46U, 0x41U, 0x52U, 0x4FU}, // ALFARO
    {0x4CU, 0x41U, 0x4EU, 0x43U, 0x49U, 0x41U}, // LANCIA
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U}, // zeros
    {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU}, // ones
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U}, // 00..01
    {0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U}, // 010203..
    {0xAAU, 0xAAU, 0xAAU, 0xAAU, 0xAAU, 0xAAU}, // AAx6
    {0x55U, 0x55U, 0x55U, 0x55U, 0x55U, 0x55U}, // 55x6
    {0xDEU, 0xADU, 0xBEU, 0xEFU, 0xCAU, 0xFEU}, // DEADBEEFCAFE
    {0xCAU, 0xFEU, 0xBAU, 0xBEU, 0xDEU, 0xADU}, // CAFEBABEDEAD
    {0x12U, 0x34U, 0x56U, 0x78U, 0x9AU, 0xBCU}, // 123456789ABC
    {0xBDU, 0xF5U, 0xE8U, 0x46U, 0xC3U, 0x99U}, // proxmark-1
    {0x9AU, 0xC4U, 0x99U, 0x9CU, 0x1EU, 0x1CU}, // proxmark-2
    {0x40U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U}, // proxmark-3
    {0x21U, 0x8FU, 0x24U, 0x0FU, 0x59U, 0xC3U}, // proxmark-4
    {0x53U, 0x03U, 0x64U, 0x3FU, 0xFBU, 0xE3U}, // proxmark-5
    {0x8AU, 0x8BU, 0x94U, 0x00U, 0x00U, 0x00U}, // proxmark-6
    {0xE7U, 0x81U, 0xA3U, 0xD5U, 0xBEU, 0xF3U}, // proxmark-7
    {0xADU, 0x2EU, 0xCEU, 0x5FU, 0x6AU, 0x1FU}, // proxmark-8
    {0xCDU, 0x27U, 0x40U, 0x89U, 0x81U, 0xF4U}, // proxmark-9
    {0x37U, 0xCCU, 0xAEU, 0x80U, 0x92U, 0xB7U}, // mut-B792-rev
    {0xB7U, 0x92U, 0x80U, 0xAEU, 0xCCU, 0x38U}, // mut-B792+1
    {0xB7U, 0x92U, 0x80U, 0xAEU, 0xCCU, 0x36U}, // mut-B792-1
    {0xB8U, 0x92U, 0x80U, 0xAEU, 0xCCU, 0x37U}, // mut-B792hi
    {0x66U, 0xD9U, 0xF7U, 0x28U, 0x24U, 0xD4U}, // mut-D424-rev
    {0xD4U, 0x24U, 0x28U, 0xF7U, 0xD9U, 0x67U}, // mut-D424+1
    {0xD4U, 0x24U, 0x28U, 0xF7U, 0xD9U, 0x65U}, // mut-D424-1
    {0xB6U, 0xE7U, 0xD4U, 0x3FU, 0x34U, 0x4DU}, // mut-4D34-rev
    {0x4DU, 0x34U, 0x3FU, 0xD4U, 0xE7U, 0xB7U}, // mut-4D34+1
    {0x1AU, 0x3AU, 0x1DU, 0xF2U, 0x6BU, 0x6DU}, // mut-6D6B-rev
    {0x6DU, 0x6BU, 0xF2U, 0x1DU, 0x3AU, 0x1BU}, // mut-6D6B+1
    {0x10U, 0xB9U, 0xF7U, 0xACU, 0xF3U, 0xA3U}, // mut-A3F3-rev
    {0xA3U, 0xF3U, 0xACU, 0xF7U, 0xB9U, 0x11U}, // mut-A3F3+1
    {0x3BU, 0xC3U, 0xCDU, 0x2FU, 0xFAU, 0x33U}, // mut-33FA-rev
    {0x33U, 0xFAU, 0x2FU, 0xCDU, 0xC3U, 0x3AU}, // mut-33FA-1
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U}, // pat-02
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x03U}, // pat-03
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x04U}, // pat-04
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x08U}, // pat-08
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x10U}, // pat-10
    {0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U}, // pat-0100
    {0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U}, // pat-010000
    {0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U}, // pat-01000000
    {0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U}, // pat-0100000000
    {0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U}, // pat-010000000000
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x99U}, // pat-99
    {0x00U, 0x00U, 0x00U, 0x00U, 0x99U, 0x99U}, // pat-9999
    {0x00U, 0x00U, 0x00U, 0x99U, 0x99U, 0x99U}, // pat-999999
    {0x99U, 0x99U, 0x99U, 0x99U, 0x99U, 0x99U}, // pat-99x6
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x11U}, // pat-11
    {0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U}, // pat-11x6
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x22U}, // pat-22
    {0x22U, 0x22U, 0x22U, 0x22U, 0x22U, 0x22U}, // pat-22x6
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x33U}, // pat-33
    {0x33U, 0x33U, 0x33U, 0x33U, 0x33U, 0x33U}, // pat-33x6
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x44U}, // pat-44
    {0x44U, 0x44U, 0x44U, 0x44U, 0x44U, 0x44U}, // pat-44x6
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x77U}, // pat-77
    {0x77U, 0x77U, 0x77U, 0x77U, 0x77U, 0x77U}, // pat-77x6
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x88U}, // pat-88
    {0x88U, 0x88U, 0x88U, 0x88U, 0x88U, 0x88U}, // pat-88x6
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xEEU}, // pat-EE
    {0xEEU, 0xEEU, 0xEEU, 0xEEU, 0xEEU, 0xEEU}, // pat-EEx6
    {0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U}, // seq-000102
    {0x10U, 0x20U, 0x30U, 0x40U, 0x50U, 0x60U}, // seq-102030
    {0xABU, 0xCDU, 0xEFU, 0x01U, 0x23U, 0x45U}, // seq-ABCDEF
    {0x12U, 0x34U, 0x56U, 0x78U, 0x90U, 0xABU}, // seq-123456
    {0x8DU, 0xCAU, 0x22U, 0x11U, 0x00U, 0xFEU}, // fiat-c1
    {0x7BU, 0x3FU, 0x91U, 0x2EU, 0xC5U, 0x08U}, // fiat-c2
    {0xF1U, 0xE2U, 0xD3U, 0xC4U, 0xB5U, 0xA6U}, // fiat-c3
    {0x1FU, 0x2EU, 0x3DU, 0x4CU, 0x5BU, 0x6AU}, // fiat-c4
    {0xE3U, 0xD1U, 0xB4U, 0x7CU, 0x8FU, 0x25U}, // fiat-c5
    {0x9CU, 0x4AU, 0x71U, 0xE8U, 0x03U, 0x56U}, // fiat-c6
};

static bool fiat_v1_key_matches(
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    uint32_t hop,
    const uint8_t key[6],
    uint32_t epoch) {
    return fiat_v1_bcm_generate_authenticator(uid, button, control, key, epoch) == hop;
}

uint32_t fiat_v1_hitag2_recovery_total_range(uint8_t key_start, uint8_t key_end) {
    if(key_end > FIAT_V1_KNOWN_KEY_COUNT) key_end = FIAT_V1_KNOWN_KEY_COUNT;
    if(key_start >= key_end) return 0U;
    return (uint32_t)(key_end - key_start) * FIAT_V1_HITAG2_EPOCH_SPACE;
}

uint32_t fiat_v1_hitag2_recovery_total(void) {
    return fiat_v1_hitag2_recovery_total_range(0U, FIAT_V1_DEEP_SWEEP_KEY_COUNT);
}

bool fiat_v1_hitag2_recover_range(
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    uint32_t hop,
    uint8_t key_start,
    uint8_t key_end,
    uint8_t out_key[6],
    uint32_t* out_epoch,
    FiatV1RecoverProgressCallback progress_cb,
    void* progress_ctx) {
    furi_check(out_key);
    furi_check(out_epoch);
    if(key_end > FIAT_V1_KNOWN_KEY_COUNT) key_end = FIAT_V1_KNOWN_KEY_COUNT;
    if(key_start >= key_end) return false;

    const uint32_t total = fiat_v1_hitag2_recovery_total_range(key_start, key_end);
    control &= 0x03FFU;
    uint32_t tested = 0U;

    for(uint8_t key_index = key_start; key_index < key_end; key_index++) {
        for(uint32_t epoch = 0U; epoch < FIAT_V1_HITAG2_EPOCH_SPACE; epoch++, tested++) {
            if(progress_cb && (tested & 0xFFFU) == 0U) {
                uint8_t pct = (uint8_t)((uint64_t)tested * 100U / (total ? total : 1U));
                if(!progress_cb(pct, tested, progress_ctx)) {
                    return false;
                }
            }
            if(fiat_v1_key_matches(
                   uid, button, control, hop, fiat_v1_known_keys[key_index], epoch)) {
                memcpy(out_key, fiat_v1_known_keys[key_index], 6U);
                *out_epoch = epoch;
                return true;
            }
        }
    }
    return false;
}

bool fiat_v1_hitag2_recover(
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    uint32_t hop,
    uint8_t out_key[6],
    uint32_t* out_epoch,
    FiatV1RecoverProgressCallback progress_cb,
    void* progress_ctx) {
    return fiat_v1_hitag2_recover_range(
        uid, button, control, hop, 0U, FIAT_V1_DEEP_SWEEP_KEY_COUNT, out_key, out_epoch,
        progress_cb, progress_ctx);
}

static void fiat_v1_verify_hitag2_key(SubGhzProtocolDecoderFiatV1* instance) {
    instance->hitag2_key_valid = false;
    instance->hitag2_epoch = 0U;
    memset(instance->hitag2_key, 0, sizeof(instance->hitag2_key));

    const uint32_t uid = instance->uid;
    const uint8_t button = instance->generic.btn;
    const uint16_t control = (uint16_t)(instance->generic.cnt & 0x03FFU);
    const uint32_t hop = instance->hop;

    for(uint8_t i = 0U; i < FIAT_V1_KNOWN_KEY_COUNT; i++) {
        if(fiat_v1_key_matches(
               uid, button, control, hop, fiat_v1_known_keys[i], instance->hitag2_epoch)) {
            memcpy(instance->hitag2_key, fiat_v1_known_keys[i], sizeof(instance->hitag2_key));
            instance->hitag2_key_valid = true;
            return;
        }
    }
}

void* subghz_protocol_decoder_fiat_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderFiatV1* instance = calloc(1, sizeof(SubGhzProtocolDecoderFiatV1));
    furi_check(instance);
    instance->base.protocol = &fiat_v1_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    subghz_protocol_decoder_fiat_v1_reset(instance);
    return instance;
}

void subghz_protocol_decoder_fiat_v1_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderFiatV1* instance = context;

    memset(instance->raw_data, 0, sizeof(instance->raw_data));
    memset(instance->last_raw_data, 0, sizeof(instance->last_raw_data));
    instance->decoder.parser_step = FiatV1DecoderStepReset;
    instance->decoder.decode_data = 0U;
    instance->decoder.decode_count_bit = 0U;
    instance->last_raw_valid = false;
    instance->generic.data = 0U;
    instance->generic.data_count_bit = 0U;
    instance->generic.serial = 0U;
    instance->generic.btn = 0U;
    instance->generic.cnt = 0U;
    instance->uid = 0U;
    instance->hop = 0U;
    instance->family = 0U;
    instance->tail_bits = FIAT_V1_DEFAULT_TAIL_BITS;
    instance->frame_xor = 0U;
    instance->hitag2_key_valid = false;
    instance->hitag2_epoch = 0U;
    memset(instance->hitag2_key, 0, sizeof(instance->hitag2_key));
    fiat_v1_clear_all_cells(instance);
}

void subghz_protocol_decoder_fiat_v1_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderFiatV1* instance = context;

    switch(instance->decoder.parser_step) {
    case FiatV1DecoderStepReset:
        if(fiat_v1_duration_is_pulse(duration)) {
            fiat_v1_clear_all_cells(instance);
            instance->decoder.parser_step = FiatV1DecoderStepData;
            (void)fiat_v1_feed_data_pulse(instance, level, duration);
        }
        break;

    case FiatV1DecoderStepData:
        if(!fiat_v1_feed_data_pulse(instance, level, duration)) {
            instance->decoder.parser_step = FiatV1DecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_fiat_v1_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderFiatV1* instance = context;
    SubGhzBlockDecoder decoder = {
        .decode_data = instance->generic.data,
        .decode_count_bit = 64U,
    };
    return subghz_protocol_blocks_get_hash_data(&decoder, 8U) ^
           (uint8_t)(instance->generic.cnt >> 8U) ^ (uint8_t)instance->generic.cnt ^
           instance->generic.btn ^ instance->frame_xor;
}

SubGhzProtocolStatus subghz_protocol_decoder_fiat_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderFiatV1* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    flipper_format_rewind(flipper_format);
    flipper_format_insert_or_update_hex(
        flipper_format, FIAT_V1_RAW_FIELD, instance->raw_data, FIAT_V1_WIRE_BYTES);

    uint32_t hop = instance->hop;
    uint32_t frame_xor = instance->frame_xor;
    uint32_t tail_bits = instance->tail_bits;
    if(!flipper_format_write_uint32(flipper_format, FIAT_V1_HOP_FIELD, &hop, 1) ||
       !flipper_format_write_uint32(flipper_format, FIAT_V1_XOR_FIELD, &frame_xor, 1) ||
       !flipper_format_write_uint32(flipper_format, FIAT_V1_TAIL_BITS_FIELD, &tail_bits, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint32_t serial_val = instance->generic.serial;
    uint32_t btn_val = instance->generic.btn;
    uint32_t cnt_val = instance->generic.cnt;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial_val, 1) ||
       !flipper_format_write_uint32(flipper_format, "Btn", &btn_val, 1) ||
       !flipper_format_write_uint32(flipper_format, "Cnt", &cnt_val, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    if(instance->hitag2_key_valid) {
        uint32_t epoch = instance->hitag2_epoch & 0x3FFFFUL;
        if(!flipper_format_insert_or_update_hex(
               flipper_format, FIAT_V1_HITAG2_KEY_FIELD, instance->hitag2_key, 6U) ||
           !flipper_format_write_uint32(
               flipper_format, FIAT_V1_HITAG2_EPOCH_FIELD, &epoch, 1)) {
            return SubGhzProtocolStatusErrorParserOthers;
        }
    }

    return SubGhzProtocolStatusOk;
}

static void fiat_v1_load_hitag2_key(
    SubGhzProtocolDecoderFiatV1* instance,
    FlipperFormat* flipper_format) {
    uint8_t key[6] = {0};
    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(flipper_format, FIAT_V1_HITAG2_KEY_FIELD, key, 6U)) {
        return;
    }

    uint32_t epoch = 0U;
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, FIAT_V1_HITAG2_EPOCH_FIELD, &epoch, 1U)) {
        epoch &= 0x3FFFFUL;
    } else {
        epoch = 0U;
    }

    memcpy(instance->hitag2_key, key, sizeof(instance->hitag2_key));
    instance->hitag2_epoch = epoch;
    instance->hitag2_key_valid = fiat_v1_key_matches(
        instance->uid,
        instance->generic.btn,
        (uint16_t)(instance->generic.cnt & 0x03FFU),
        instance->hop,
        key,
        epoch);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_fiat_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderFiatV1* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_fiat_v1_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }
    if(instance->generic.data_count_bit != FIAT_V1_LOGICAL_BITS) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_hex(
           flipper_format, FIAT_V1_RAW_FIELD, instance->raw_data, FIAT_V1_WIRE_BYTES)) {
        if(!fiat_v1_frame_valid(instance->raw_data)) {
            return SubGhzProtocolStatusErrorParserOthers;
        }
        fiat_v1_decode_fields(instance);
        fiat_v1_load_hitag2_key(instance, flipper_format);
        return SubGhzProtocolStatusOk;
    }

    instance->generic.serial = (uint32_t)(instance->generic.data >> 32U);
    instance->hop = (uint32_t)instance->generic.data;
    instance->family = 0x01U;
    instance->uid = instance->generic.serial;
    instance->tail_bits = FIAT_V1_DEFAULT_TAIL_BITS;

    uint32_t value = 0U;
    if(flipper_format_read_uint32(flipper_format, "Serial", &value, 1)) {
        instance->generic.serial = value;
    }
    if(flipper_format_read_uint32(flipper_format, "Btn", &value, 1)) {
        instance->generic.btn = (uint8_t)value;
    }
    if(flipper_format_read_uint32(flipper_format, "Cnt", &value, 1)) {
        instance->generic.cnt = value;
    }
    if(flipper_format_read_uint32(flipper_format, FIAT_V1_HOP_FIELD, &value, 1)) {
        instance->hop = value;
    }
    if(flipper_format_read_uint32(flipper_format, FIAT_V1_XOR_FIELD, &value, 1)) {
        instance->frame_xor = (uint8_t)value;
    }
    if(flipper_format_read_uint32(flipper_format, FIAT_V1_TAIL_BITS_FIELD, &value, 1)) {
        instance->tail_bits = (uint8_t)(value & 0x03U);
    }

    fiat_v1_rebuild_raw(instance);
    if(!fiat_v1_frame_valid(instance->raw_data)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    fiat_v1_decode_fields(instance);
    fiat_v1_load_hitag2_key(instance, flipper_format);
    return SubGhzProtocolStatusOk;
}

uint32_t subghz_protocol_fiat_v1_compute_auth(
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    const uint8_t key[6],
    uint32_t epoch) {
    return fiat_v1_bcm_generate_authenticator(uid, button, control, key, epoch);
}

bool subghz_protocol_fiat_v1_verify_key(
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    uint32_t hop,
    const uint8_t key[6],
    uint32_t epoch) {
    return fiat_v1_key_matches(uid, button, control, hop, key, epoch);
}

const uint8_t (*subghz_protocol_fiat_v1_get_known_keys(void))[6] {
    return fiat_v1_known_keys;
}
