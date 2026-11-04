#include "honeywell_wdb.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolHoneywellWdb"

static const SubGhzBlockConst subghz_protocol_honeywell_wdb_const = {
    .te_short = 160,
    .te_long = 320,
    .te_delta = 60,
    .min_count_bit_for_found = 48,
};

struct SubGhzProtocolDecoderHoneywell_WDB {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    const char* device_type;
    const char* alert;
    uint8_t secret_knock;
    uint8_t relay;
    uint8_t lowbat;
};

struct SubGhzProtocolEncoderHoneywell_WDB {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderHoneywell_WDB);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderHoneywell_WDB);

typedef enum {
    Honeywell_WDBDecoderStepReset = 0,
    Honeywell_WDBDecoderStepFoundStartBit,
    Honeywell_WDBDecoderStepSaveDuration,
    Honeywell_WDBDecoderStepCheckDuration,
} Honeywell_WDBDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_honeywell_wdb_decoder = {
    .alloc = subghz_protocol_decoder_honeywell_wdb_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_honeywell_wdb_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_honeywell_wdb_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_honeywell_wdb_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_honeywell_wdb");

const SubGhzProtocol subghz_protocol_honeywell_wdb = {
    .name = SUBGHZ_PROTOCOL_HONEYWELL_WDB_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send | SubGhzProtocolFlag_Sensors,

    .decoder = &subghz_protocol_honeywell_wdb_decoder,
    .encoder = &subghz_protocol_honeywell_wdb_encoder_ext.vtable,
};

void* subghz_protocol_decoder_honeywell_wdb_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderHoneywell_WDB), &subghz_protocol_honeywell_wdb);
}

void subghz_protocol_decoder_honeywell_wdb_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderHoneywell_WDB* instance = context;
    switch(instance->decoder.parser_step) {
    case Honeywell_WDBDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_honeywell_wdb_const.te_short * 3) <
                        subghz_protocol_honeywell_wdb_const.te_delta)) {

            instance->decoder.decode_count_bit = 0;
            instance->decoder.decode_data = 0;
            instance->decoder.parser_step = Honeywell_WDBDecoderStepSaveDuration;
        }
        break;
    case Honeywell_WDBDecoderStepSaveDuration:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_honeywell_wdb_const.te_short * 3) <
               subghz_protocol_honeywell_wdb_const.te_delta) {
                if((instance->decoder.decode_count_bit ==
                    subghz_protocol_honeywell_wdb_const.min_count_bit_for_found) &&
                   ((instance->decoder.decode_data & 0x01) ==
                    subghz_protocol_blocks_get_parity(
                        instance->decoder.decode_data >> 1,
                        subghz_protocol_honeywell_wdb_const.min_count_bit_for_found - 1))) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.parser_step = Honeywell_WDBDecoderStepReset;
                break;
            }
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = Honeywell_WDBDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = Honeywell_WDBDecoderStepReset;
        }
        break;
    case Honeywell_WDBDecoderStepCheckDuration:
        if(!level) {
            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_honeywell_wdb_const.te_short) <
                subghz_protocol_honeywell_wdb_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_honeywell_wdb_const.te_long) <
                subghz_protocol_honeywell_wdb_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = Honeywell_WDBDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_honeywell_wdb_const.te_long) <
                 subghz_protocol_honeywell_wdb_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_honeywell_wdb_const.te_short) <
                 subghz_protocol_honeywell_wdb_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = Honeywell_WDBDecoderStepSaveDuration;
            } else
                instance->decoder.parser_step = Honeywell_WDBDecoderStepReset;
        } else {
            instance->decoder.parser_step = Honeywell_WDBDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_honeywell_wdb_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderHoneywell_WDB* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_honeywell_wdb_const.min_count_bit_for_found);
}

