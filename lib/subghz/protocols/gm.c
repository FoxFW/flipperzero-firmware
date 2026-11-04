#include "gm.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <furi.h>
#include <string.h>
#include "common.h"
#include "../encode_plugin.h"

#define GM_TE_SHORT       300U
#define GM_TE_LONG        500U
#define GM_TE_DELTA       120U
#define GM_PULSE_MIN_US   100U
#define GM_PULSE_MAX_US   1000U
#define GM_RESET_US       12000U

#define GM_WIRE_BYTES     14U
#define GM_WIRE_BITS      112U

#define GM_DEFAULT_REPEAT 5U
#define GM_DATA_FIELD     "Data"

#define GM_UPLOAD_CAPACITY (2U + (GM_WIRE_BITS * 2U) + 2U)

static const SubGhzBlockConst subghz_protocol_gm_const = {
    .te_short = GM_TE_SHORT,
    .te_long = GM_TE_LONG,
    .te_delta = GM_TE_DELTA,
    .min_count_bit_for_found = GM_WIRE_BITS,
};

typedef enum {
    GMDecoderStepReset = 0,
    GMDecoderStepData,
} GMDecoderStep;

struct SubGhzProtocolDecoderGM {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t bits[GM_WIRE_BYTES];
    uint16_t bit_count;

    uint8_t raw_data[GM_WIRE_BYTES];
    uint8_t last_raw_data[GM_WIRE_BYTES];
    bool last_raw_valid;
};

struct SubGhzProtocolEncoderGM {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_data[GM_WIRE_BYTES];
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderGM);


const SubGhzProtocolDecoder subghz_protocol_gm_decoder = {
    .alloc = subghz_protocol_decoder_gm_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_gm_feed,
    .reset = subghz_protocol_decoder_gm_reset,
    .get_hash_data = subghz_protocol_decoder_gm_get_hash_data,
    .serialize = subghz_protocol_decoder_gm_serialize,
    .deserialize = subghz_protocol_decoder_gm_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_gm_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_gm");

const SubGhzProtocol subghz_protocol_gm = {
    .name = GM_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_gm_decoder,
    .encoder = &subghz_protocol_gm_encoder_ext.vtable,
};

static uint32_t gm_add_nibbles(const uint8_t* b, size_t n) {
    uint32_t sum = 0U;
    for(size_t i = 0U; i < n; i++) {
        sum += (uint32_t)(b[i] >> 4U) + (uint32_t)(b[i] & 0x0FU);
    }
    return sum;
}

static uint32_t gm_add_bytes(const uint8_t* b, size_t n) {
    uint32_t sum = 0U;
    for(size_t i = 0U; i < n; i++) {
        sum += b[i];
    }
    return sum;
}

static bool gm_frame_valid(const uint8_t raw[GM_WIRE_BYTES]) {

    if(raw[0] != 0xFFU) return false;

    const uint32_t btn_ck = gm_add_nibbles(&raw[2], 1U);
    if(btn_ck == 0U || (btn_ck & 0x0FU) != 0U) return false;

    const uint32_t full_ck = gm_add_bytes(&raw[1], 13U);
    if(full_ck == 0U || (full_ck & 0xFFU) != 0U) return false;

    return true;
}

static uint32_t gm_id(const uint8_t raw[GM_WIRE_BYTES]) {
    return ((uint32_t)raw[3] << 24U) | ((uint32_t)raw[4] << 16U) | ((uint32_t)raw[5] << 8U) |
           raw[6];
}

static uint32_t gm_sequence(const uint8_t raw[GM_WIRE_BYTES]) {
    return ((uint32_t)raw[7] << 16U) | ((uint32_t)raw[8] << 8U) | raw[9];
}

static uint8_t gm_button(const uint8_t raw[GM_WIRE_BYTES]) {
    return (uint8_t)(raw[2] & 0x07U);
}

static void gm_decode_fields(SubGhzBlockGeneric* generic, const uint8_t raw[GM_WIRE_BYTES]) {
    generic->serial = gm_id(raw);
    generic->btn = gm_button(raw);
    generic->cnt = (uint16_t)(gm_sequence(raw) & 0xFFFFU);
    generic->data_count_bit = GM_WIRE_BITS;

    generic->data = ((uint64_t)generic->serial << 16U) | generic->cnt;
}

static void gm_data_reset(SubGhzProtocolDecoderGM* instance) {
    instance->bit_count = 0U;
    memset(instance->bits, 0, sizeof(instance->bits));
}

static void gm_push_bit(SubGhzProtocolDecoderGM* instance, bool bit) {
    uint8_t carry = bit ? 1U : 0U;
    for(int i = (int)GM_WIRE_BYTES - 1; i >= 0; i--) {
        const uint8_t new_carry = (uint8_t)((instance->bits[i] >> 7U) & 1U);
        instance->bits[i] = (uint8_t)((instance->bits[i] << 1U) | carry);
        carry = new_carry;
    }
    if(instance->bit_count < 0xFFFFU) instance->bit_count++;
}

static void gm_report(SubGhzProtocolDecoderGM* instance) {
    if(instance->bit_count < GM_WIRE_BITS) return;
    if(!gm_frame_valid(instance->bits)) return;

    if(instance->last_raw_valid &&
       memcmp(instance->last_raw_data, instance->bits, GM_WIRE_BYTES) == 0) {
        return;
    }

    memcpy(instance->raw_data, instance->bits, GM_WIRE_BYTES);
    memcpy(instance->last_raw_data, instance->bits, GM_WIRE_BYTES);
    instance->last_raw_valid = true;
    gm_decode_fields(&instance->generic, instance->raw_data);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void subghz_protocol_decoder_gm_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;

    switch(instance->decoder.parser_step) {
    case GMDecoderStepReset:

        if(level && duration >= GM_PULSE_MIN_US && duration <= GM_PULSE_MAX_US) {
            instance->decoder.parser_step = GMDecoderStepData;
            gm_data_reset(instance);
        }
        break;

    case GMDecoderStepData:
        if(level) {

            if(duration < GM_PULSE_MIN_US || duration > GM_PULSE_MAX_US) {
                gm_report(instance);
                instance->decoder.parser_step = GMDecoderStepReset;
                gm_data_reset(instance);
            }

        } else {

            if(duration >= GM_RESET_US) {
                gm_report(instance);
                instance->decoder.parser_step = GMDecoderStepReset;
                gm_data_reset(instance);
            } else if(DURATION_DIFF(duration, GM_TE_SHORT) < GM_TE_DELTA) {
                gm_push_bit(instance, false);
                if(instance->bit_count >= GM_WIRE_BITS) gm_report(instance);
            } else if(DURATION_DIFF(duration, GM_TE_LONG) < GM_TE_DELTA) {
                gm_push_bit(instance, true);
                if(instance->bit_count >= GM_WIRE_BITS) gm_report(instance);
            } else {

                gm_report(instance);
                instance->decoder.parser_step = GMDecoderStepReset;
                gm_data_reset(instance);
            }
        }
        break;
    }
}

void* subghz_protocol_decoder_gm_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderGM* instance = calloc(1, sizeof(SubGhzProtocolDecoderGM));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_gm;
    instance->generic.protocol_name = instance->base.protocol->name;
    subghz_protocol_decoder_gm_reset(instance);
    return instance;
}



void subghz_protocol_decoder_gm_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;
    instance->decoder.parser_step = GMDecoderStepReset;
    instance->decoder.decode_data = 0U;
    instance->decoder.decode_count_bit = 0U;
    instance->last_raw_valid = false;
    memset(instance->raw_data, 0, sizeof(instance->raw_data));
    memset(instance->last_raw_data, 0, sizeof(instance->last_raw_data));
    gm_data_reset(instance);
}

uint8_t subghz_protocol_decoder_gm_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;
    uint8_t hash = 0U;
    for(size_t i = 0U; i < GM_WIRE_BYTES; i++) {
        hash ^= instance->raw_data[i];
    }
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_gm_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    flipper_format_rewind(flipper_format);
    if(!flipper_format_insert_or_update_hex(
           flipper_format, GM_DATA_FIELD, instance->raw_data, GM_WIRE_BYTES)) {
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
    subghz_protocol_decoder_gm_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_gm_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, GM_DATA_FIELD, instance->raw_data, GM_WIRE_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    if(!gm_frame_valid(instance->raw_data)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    gm_decode_fields(&instance->generic, instance->raw_data);
    return SubGhzProtocolStatusOk;
}

