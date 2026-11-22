#include "audi.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <furi.h>
#include <furi_hal.h>
#include <string.h>
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

#define AUDI_TE_SHORT     550U
#define AUDI_TE_LONG      1100U
#define AUDI_TE_DELTA     137U
#define AUDI_HEADER_HIGH  2500U
#define AUDI_HEADER_LOW   850U
#define AUDI_GAP_US       850U
#define AUDI_WIRE_BYTES   8U
#define AUDI_WIRE_BITS    64U
#define AUDI_REPEATS      5U
#define AUDI_SCRAMBLE_ROUNDS 4U

#define AUDI_HEADER_MIN_US 1900U

#define AUDI_UPLOAD_CAPACITY ((2U + (AUDI_WIRE_BITS * 2U) + 1U) * AUDI_REPEATS)

#define AUDI_DEFAULT_REPEAT AUDI_REPEATS

#define AUDI_DATA_FIELD "Data"

#define AUDI_BTN_0 0x01U
#define AUDI_BTN_1 0x02U
#define AUDI_BTN_2 0x04U

static const SubGhzBlockConst subghz_protocol_audi_const = {
    .te_short = AUDI_TE_SHORT,
    .te_long = AUDI_TE_LONG,
    .te_delta = AUDI_TE_DELTA,
    .min_count_bit_for_found = AUDI_WIRE_BITS,
};

typedef enum {
    AudiDecoderStepReset = 0,
    AudiDecoderStepHeaderLow,
    AudiDecoderStepData,
} AudiDecoderStep;

struct SubGhzProtocolDecoderAudi {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t pending_high;
    bool have_high;

    uint8_t bytes[AUDI_WIRE_BYTES];
    uint16_t bit_count;

    uint8_t raw_data[AUDI_WIRE_BYTES];
    uint8_t last_raw_data[AUDI_WIRE_BYTES];
    bool last_raw_valid;
    bool parity_ok;
};

struct SubGhzProtocolEncoderAudi {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_data[AUDI_WIRE_BYTES];
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderAudi);

const SubGhzProtocolDecoder subghz_protocol_audi_decoder = {
    .alloc = subghz_protocol_decoder_audi_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_audi_feed,
    .reset = subghz_protocol_decoder_audi_reset,
    .get_hash_data = subghz_protocol_decoder_audi_get_hash_data,
    .serialize = subghz_protocol_decoder_audi_serialize,
    .deserialize = subghz_protocol_decoder_audi_deserialize,
    .get_string = subghz_protocol_decoder_audi_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_audi_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_audi");

const SubGhzProtocol subghz_protocol_audi = {
    .name = AUDI_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_audi_decoder,
    .encoder = &subghz_protocol_audi_encoder_ext.vtable,
};

static inline uint8_t audi_bit(uint8_t x, uint8_t n) {
    return (uint8_t)((x >> n) & 1U);
}

static uint8_t audi_parity(uint8_t b3, uint8_t b4, uint8_t b5) {
    return (uint8_t)(
        audi_bit(b3, 3) ^ audi_bit(b4, 4) ^ audi_bit(b4, 6) ^ audi_bit(b4, 7) ^ audi_bit(b5, 4) ^
        audi_bit(b5, 7));
}

static bool audi_unscramble_iteration(uint8_t block[3]) {
    const uint8_t o0 = block[0];
    const uint8_t o1 = block[1];
    const uint8_t o2 = block[2];

    const uint8_t p = (uint8_t)(o0 & 1U);

    const uint8_t in0 = (uint8_t)(((o0 >> 1) & 0x7FU) | ((o1 & 1U) << 7));
    const uint8_t in1 = (uint8_t)(((o1 >> 1) & 0x7FU) | ((o2 & 1U) << 7));
    uint8_t in2_low7 = (uint8_t)((o2 >> 1) & 0x7FU);

    const uint8_t known = (uint8_t)(
        audi_bit(in0, 3) ^ audi_bit(in1, 4) ^ audi_bit(in1, 6) ^ audi_bit(in1, 7) ^
        audi_bit(in2_low7, 4));
    const uint8_t in2_bit7 = (uint8_t)(p ^ known);
    const uint8_t in2 = (uint8_t)(in2_low7 | (in2_bit7 << 7));

    block[0] = in0;
    block[1] = in1;
    block[2] = in2;

    return audi_parity(in0, in1, in2) == p;
}

static bool audi_decrypt_block(uint8_t raw[AUDI_WIRE_BYTES]) {
    uint8_t block[3] = {raw[3], raw[4], raw[5]};
    bool ok = true;
    for(uint8_t r = 0; r < AUDI_SCRAMBLE_ROUNDS; r++) {
        ok = audi_unscramble_iteration(block) && ok;
    }
    raw[3] = block[0];
    raw[4] = block[1];
    raw[5] = block[2];
    return ok;
}

static uint32_t audi_uid(const uint8_t raw[AUDI_WIRE_BYTES]) {
    return ((uint32_t)raw[0] << 16U) | ((uint32_t)raw[1] << 8U) | raw[2];
}

static bool audi_frame_valid(const uint8_t raw[AUDI_WIRE_BYTES]) {
    const uint32_t uid = audi_uid(raw);
    return uid != 0U && uid != 0xFFFFFFU;
}

static void audi_decode_fields(SubGhzBlockGeneric* generic, const uint8_t raw[AUDI_WIRE_BYTES]) {
    generic->serial = audi_uid(raw);
    generic->btn = raw[6];

    uint8_t plain[AUDI_WIRE_BYTES];
    memcpy(plain, raw, AUDI_WIRE_BYTES);
    audi_decrypt_block(plain);
    generic->cnt = (uint16_t)(((uint16_t)plain[3] << 8U) | plain[4]);
    generic->data_count_bit = AUDI_WIRE_BITS;

    uint64_t data = 0U;
    for(size_t i = 0; i < AUDI_WIRE_BYTES; i++) data = (data << 8U) | raw[i];
    generic->data = data;
}

static bool audi_is_short(uint32_t d) {
    return DURATION_DIFF(d, AUDI_TE_SHORT) < AUDI_TE_DELTA;
}

static bool audi_is_long(uint32_t d) {
    return DURATION_DIFF(d, AUDI_TE_LONG) < AUDI_TE_DELTA;
}

static void audi_data_reset(SubGhzProtocolDecoderAudi* instance) {
    instance->bit_count = 0U;
    instance->have_high = false;
    instance->pending_high = 0U;
    memset(instance->bytes, 0, sizeof(instance->bytes));
}

static void audi_add_bit(SubGhzProtocolDecoderAudi* instance, bool bit) {
    if(instance->bit_count >= AUDI_WIRE_BITS) return;
    if(bit) {
        instance->bytes[instance->bit_count >> 3U] |=
            (uint8_t)(1U << (instance->bit_count & 7U));
    }
    instance->bit_count++;
}

static void audi_report(SubGhzProtocolDecoderAudi* instance) {
    if(instance->bit_count < AUDI_WIRE_BITS) return;
    if(!audi_frame_valid(instance->bytes)) return;

    uint8_t plain[AUDI_WIRE_BYTES];
    memcpy(plain, instance->bytes, AUDI_WIRE_BYTES);
    const bool parity_ok = audi_decrypt_block(plain);
    if(!parity_ok) return;

    if(instance->last_raw_valid &&
       memcmp(instance->last_raw_data, instance->bytes, AUDI_WIRE_BYTES) == 0) {
        return;
    }

    memcpy(instance->raw_data, instance->bytes, AUDI_WIRE_BYTES);
    memcpy(instance->last_raw_data, instance->bytes, AUDI_WIRE_BYTES);
    instance->last_raw_valid = true;
    instance->parity_ok = parity_ok;

    audi_decode_fields(&instance->generic, instance->raw_data);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void* subghz_protocol_decoder_audi_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderAudi* instance = calloc(1, sizeof(SubGhzProtocolDecoderAudi));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_audi;
    instance->generic.protocol_name = instance->base.protocol->name;
    subghz_protocol_decoder_audi_reset(instance);
    return instance;
}

void subghz_protocol_decoder_audi_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderAudi* instance = context;
    instance->decoder.parser_step = AudiDecoderStepReset;
    instance->decoder.decode_data = 0U;
    instance->decoder.decode_count_bit = 0U;
    instance->last_raw_valid = false;
    instance->parity_ok = false;
    memset(instance->raw_data, 0, sizeof(instance->raw_data));
    memset(instance->last_raw_data, 0, sizeof(instance->last_raw_data));
    audi_data_reset(instance);
}

void subghz_protocol_decoder_audi_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderAudi* instance = context;

    switch(instance->decoder.parser_step) {
    case AudiDecoderStepReset:

        if(level && DURATION_DIFF(duration, AUDI_HEADER_HIGH) < AUDI_TE_DELTA * 2U) {
            instance->decoder.parser_step = AudiDecoderStepHeaderLow;
        }
        break;

    case AudiDecoderStepHeaderLow:

        if(!level && DURATION_DIFF(duration, AUDI_HEADER_LOW) < AUDI_TE_DELTA * 2U) {
            instance->decoder.parser_step = AudiDecoderStepData;
            audi_data_reset(instance);
        } else {
            instance->decoder.parser_step = AudiDecoderStepReset;
        }
        break;

    case AudiDecoderStepData:
        if(level) {

            if(DURATION_DIFF(duration, AUDI_HEADER_HIGH) < AUDI_TE_DELTA * 2U) {
                audi_report(instance);
                instance->decoder.parser_step = AudiDecoderStepHeaderLow;
                break;
            }
            if(audi_is_short(duration) || audi_is_long(duration)) {
                instance->pending_high = duration;
                instance->have_high = true;
            } else {
                audi_report(instance);
                audi_data_reset(instance);
                instance->decoder.parser_step = AudiDecoderStepReset;
            }
        } else {
            if(!instance->have_high) {

                audi_report(instance);
                audi_data_reset(instance);
                instance->decoder.parser_step = AudiDecoderStepReset;
                break;
            }
            instance->have_high = false;

            const bool high_long = audi_is_long(instance->pending_high);
            const bool high_short = audi_is_short(instance->pending_high);
            const bool low_long = audi_is_long(duration);
            const bool low_short = audi_is_short(duration);

            if(high_long && low_short) {
                audi_add_bit(instance, true);
            } else if(high_short && low_long) {
                audi_add_bit(instance, false);
            } else if(high_short && !low_short && !low_long) {

                audi_add_bit(instance, false);
                audi_report(instance);
                audi_data_reset(instance);
                instance->decoder.parser_step = AudiDecoderStepReset;
                break;
            } else {
                audi_report(instance);
                audi_data_reset(instance);
                instance->decoder.parser_step = AudiDecoderStepReset;
                break;
            }

            if(instance->bit_count == AUDI_WIRE_BITS) {
                audi_report(instance);
                audi_data_reset(instance);
                instance->decoder.parser_step = AudiDecoderStepReset;
            }
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_audi_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderAudi* instance = context;
    uint8_t hash = 0U;
    for(size_t i = 0; i < AUDI_WIRE_BYTES; i++) hash ^= instance->raw_data[i];
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_audi_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderAudi* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_insert_or_update_hex(
           flipper_format, AUDI_DATA_FIELD, instance->raw_data, AUDI_WIRE_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    if(!flipper_format_insert_or_update_uint32(
           flipper_format, "Serial", &instance->generic.serial, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    uint32_t btn = instance->generic.btn;
    if(!flipper_format_insert_or_update_uint32(flipper_format, "Btn", &btn, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    uint32_t cnt = instance->generic.cnt;
    if(!flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &cnt, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_audi_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderAudi* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_audi_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) return ret;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, AUDI_DATA_FIELD, instance->raw_data, AUDI_WIRE_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    if(!audi_frame_valid(instance->raw_data)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint8_t plain[AUDI_WIRE_BYTES];
    memcpy(plain, instance->raw_data, AUDI_WIRE_BYTES);
    instance->parity_ok = audi_decrypt_block(plain);
    audi_decode_fields(&instance->generic, instance->raw_data);
    return SubGhzProtocolStatusOk;
}

void subghz_protocol_decoder_audi_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderAudi* instance = context;
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}

