#include "mercedes.h"

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
#include "decode_plugin.h"

#define MERCEDES_TE_SHORT      500U
#define MERCEDES_TE_DELTA      125U
#define MERCEDES_LEAD_GAP_US   3700U
#define MERCEDES_PREAMBLE_PAIRS 55U
#define MERCEDES_WIRE_BYTES    14U
#define MERCEDES_WIRE_BITS     112U

#define MERCEDES_BOUNDARY_MIN_US 1500U

#define MERCEDES_UPLOAD_CAPACITY \
    (1U + (MERCEDES_PREAMBLE_PAIRS * 2U) + 4U + (MERCEDES_WIRE_BITS * 2U) + 2U)

#define MERCEDES_DEFAULT_REPEAT 4U

#define MERCEDES_DATA_FIELD "Data"

static const SubGhzBlockConst subghz_protocol_mercedes_const = {
    .te_short = MERCEDES_TE_SHORT,
    .te_long = MERCEDES_TE_SHORT * 2U,
    .te_delta = MERCEDES_TE_DELTA,
    .min_count_bit_for_found = MERCEDES_WIRE_BITS,
};

typedef enum {
    MercedesDecoderStepReset = 0,
    MercedesDecoderStepPreamble,
    MercedesDecoderStepData,
} MercedesDecoderStep;

struct SubGhzProtocolDecoderMercedes {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t preamble_count;

    ManchesterState manchester_state;
    uint8_t bytes[MERCEDES_WIRE_BYTES];
    uint16_t bit_count;

    uint8_t raw_data[MERCEDES_WIRE_BYTES];
    uint8_t last_raw_data[MERCEDES_WIRE_BYTES];
    bool last_raw_valid;
};

struct SubGhzProtocolEncoderMercedes {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_data[MERCEDES_WIRE_BYTES];
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderMercedes);

const SubGhzProtocolDecoder subghz_protocol_mercedes_decoder = {
    .alloc = subghz_protocol_decoder_mercedes_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_mercedes_feed,
    .reset = subghz_protocol_decoder_mercedes_reset,
    .get_hash_data = subghz_protocol_decoder_mercedes_get_hash_data,
    .serialize = subghz_protocol_decoder_mercedes_serialize,
    .deserialize = subghz_protocol_decoder_mercedes_deserialize,
    .get_string = subghz_protocol_decoder_mercedes_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_mercedes_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_mercedes");

const SubGhzProtocol subghz_protocol_mercedes = {
    .name = MERCEDES_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_mercedes_decoder,
    .encoder = &subghz_protocol_mercedes_encoder_ext.vtable,
};

static uint32_t mercedes_uid(const uint8_t raw[MERCEDES_WIRE_BYTES]) {
    return ((uint32_t)raw[0] << 24U) | ((uint32_t)raw[1] << 16U) | ((uint32_t)raw[2] << 8U) |
           raw[3];
}

static uint16_t mercedes_counter(const uint8_t raw[MERCEDES_WIRE_BYTES]) {
    return (uint16_t)(((uint16_t)raw[5] << 8U) | raw[6]);
}

static void mercedes_decode_fields(SubGhzBlockGeneric* generic, const uint8_t raw[MERCEDES_WIRE_BYTES]) {
    generic->serial = mercedes_uid(raw);
    generic->btn = raw[4];
    generic->cnt = mercedes_counter(raw);
    generic->data_count_bit = MERCEDES_WIRE_BITS;

    generic->data = ((uint64_t)generic->serial << 16U) | generic->cnt;
}

static bool mercedes_frame_valid(const uint8_t raw[MERCEDES_WIRE_BYTES]) {

    const uint32_t uid = mercedes_uid(raw);
    return uid != 0U && uid != UINT32_MAX;
}

static bool mercedes_duration_is_short(uint32_t duration) {
    return DURATION_DIFF(duration, MERCEDES_TE_SHORT) < MERCEDES_TE_DELTA;
}

static bool mercedes_duration_is_long(uint32_t duration) {
    return DURATION_DIFF(duration, MERCEDES_TE_SHORT * 2U) < MERCEDES_TE_DELTA;
}

static void mercedes_data_reset(SubGhzProtocolDecoderMercedes* instance) {
    instance->bit_count = 0U;
    instance->manchester_state = ManchesterStateMid1;
    memset(instance->bytes, 0, sizeof(instance->bytes));
}

static void mercedes_add_bit(SubGhzProtocolDecoderMercedes* instance, bool bit) {
    if(instance->bit_count >= MERCEDES_WIRE_BITS) return;
    if(bit) {
        instance->bytes[instance->bit_count >> 3U] |=
            (uint8_t)(1U << (7U - (instance->bit_count & 7U)));
    }
    instance->bit_count++;
}

static void mercedes_report(SubGhzProtocolDecoderMercedes* instance) {
    if(instance->bit_count < MERCEDES_WIRE_BITS) return;

    if(!mercedes_frame_valid(instance->bytes)) return;

    if(instance->last_raw_valid &&
       memcmp(instance->last_raw_data, instance->bytes, MERCEDES_WIRE_BYTES) == 0) {
        return;
    }

    memcpy(instance->raw_data, instance->bytes, MERCEDES_WIRE_BYTES);
    memcpy(instance->last_raw_data, instance->bytes, MERCEDES_WIRE_BYTES);
    instance->last_raw_valid = true;
    mercedes_decode_fields(&instance->generic, instance->raw_data);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

static void mercedes_feed_manchester(
    SubGhzProtocolDecoderMercedes* instance,
    bool level,
    bool is_long) {
    ManchesterEvent event;
    if(is_long) {
        event = level ? ManchesterEventLongHigh : ManchesterEventLongLow;
    } else {
        event = level ? ManchesterEventShortHigh : ManchesterEventShortLow;
    }

    bool data_bit;
    if(manchester_advance(
           instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
        mercedes_add_bit(instance, data_bit);
        if(instance->bit_count == MERCEDES_WIRE_BITS) {
            mercedes_report(instance);
        }
    }
}

void* subghz_protocol_decoder_mercedes_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMercedes* instance = calloc(1, sizeof(SubGhzProtocolDecoderMercedes));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_mercedes;
    instance->generic.protocol_name = instance->base.protocol->name;
    subghz_protocol_decoder_mercedes_reset(instance);
    return instance;
}

void subghz_protocol_decoder_mercedes_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMercedes* instance = context;
    instance->decoder.parser_step = MercedesDecoderStepReset;
    instance->decoder.decode_data = 0U;
    instance->decoder.decode_count_bit = 0U;
    instance->preamble_count = 0U;
    instance->last_raw_valid = false;
    memset(instance->raw_data, 0, sizeof(instance->raw_data));
    memset(instance->last_raw_data, 0, sizeof(instance->last_raw_data));
    mercedes_data_reset(instance);
}

void subghz_protocol_decoder_mercedes_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderMercedes* instance = context;

    switch(instance->decoder.parser_step) {
    case MercedesDecoderStepReset:

        if(!level && duration >= MERCEDES_BOUNDARY_MIN_US) {
            instance->decoder.parser_step = MercedesDecoderStepPreamble;
            instance->preamble_count = 0U;
        }
        break;

    case MercedesDecoderStepPreamble:

        if(mercedes_duration_is_short(duration)) {
            instance->preamble_count++;
        } else if(
            !level && mercedes_duration_is_long(duration) &&
            instance->preamble_count >= (MERCEDES_PREAMBLE_PAIRS * 2U - 6U)) {

            instance->decoder.parser_step = MercedesDecoderStepData;
            mercedes_data_reset(instance);
        } else if(!level && duration >= MERCEDES_BOUNDARY_MIN_US) {

            instance->preamble_count = 0U;
        } else {
            instance->decoder.parser_step = MercedesDecoderStepReset;
            instance->preamble_count = 0U;
        }
        break;

    case MercedesDecoderStepData:
        if(mercedes_duration_is_short(duration)) {
            mercedes_feed_manchester(instance, level, false);
        } else if(mercedes_duration_is_long(duration)) {
            mercedes_feed_manchester(instance, level, true);
        } else {

            mercedes_report(instance);
            mercedes_data_reset(instance);
            instance->decoder.parser_step = MercedesDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_mercedes_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMercedes* instance = context;
    uint8_t hash = 0U;
    for(size_t i = 0U; i < MERCEDES_WIRE_BYTES; i++) {
        hash ^= instance->raw_data[i];
    }
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_mercedes_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderMercedes* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    flipper_format_rewind(flipper_format);
    if(!flipper_format_insert_or_update_hex(
           flipper_format, MERCEDES_DATA_FIELD, instance->raw_data, MERCEDES_WIRE_BYTES)) {
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
    subghz_protocol_decoder_mercedes_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderMercedes* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_mercedes_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, MERCEDES_DATA_FIELD, instance->raw_data, MERCEDES_WIRE_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    if(!mercedes_frame_valid(instance->raw_data)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    mercedes_decode_fields(&instance->generic, instance->raw_data);
    return SubGhzProtocolStatusOk;
}

void subghz_protocol_decoder_mercedes_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderMercedes* instance = context;
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}

