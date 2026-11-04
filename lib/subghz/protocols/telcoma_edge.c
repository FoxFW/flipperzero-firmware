#include "telcoma_edge.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolTelcomaEdge"

#define TELCOMA_EDGE_UPLOAD_SIZE 128

static const SubGhzBlockConst subghz_protocol_telcoma_edge_const = {
    .te_short = 1270,
    .te_long = 2540,
    .te_delta = 520,
    .min_count_bit_for_found = 32,
};

struct SubGhzProtocolDecoderTelcomaEdge {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    bool half_pending;
    bool half_level;
};

struct SubGhzProtocolEncoderTelcomaEdge {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderTelcomaEdge);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderTelcomaEdge);

const SubGhzProtocolDecoder subghz_protocol_telcoma_edge_decoder = {
    .alloc = subghz_protocol_decoder_telcoma_edge_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_telcoma_edge_feed,
    .reset = subghz_protocol_decoder_telcoma_edge_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_telcoma_edge_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_telcoma_edge_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_telcoma_edge");

const SubGhzProtocol subghz_protocol_telcoma_edge = {
    .name = SUBGHZ_PROTOCOL_TELCOMA_EDGE_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_telcoma_edge_decoder,
    .encoder = &subghz_protocol_telcoma_edge_encoder_ext.vtable,
};

void* subghz_protocol_decoder_telcoma_edge_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderTelcomaEdge* instance = malloc(sizeof(SubGhzProtocolDecoderTelcomaEdge));
    instance->base.protocol = &subghz_protocol_telcoma_edge;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->half_pending = false;
    return instance;
}

void subghz_protocol_decoder_telcoma_edge_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderTelcomaEdge* instance = context;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->half_pending = false;
}

static void subghz_protocol_decoder_telcoma_edge_add_half(
    SubGhzProtocolDecoderTelcomaEdge* instance,
    bool level) {
    if(!instance->half_pending) {
        instance->half_pending = true;
        instance->half_level = level;
        return;
    }
    instance->half_pending = false;
    if(instance->half_level && !level) {
        subghz_protocol_blocks_add_bit(&instance->decoder, 1);
    } else if(!instance->half_level && level) {
        subghz_protocol_blocks_add_bit(&instance->decoder, 0);
    }

}

void subghz_protocol_decoder_telcoma_edge_feed(void* context, bool level, volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderTelcomaEdge* instance = context;

    if(DURATION_DIFF(duration, subghz_protocol_telcoma_edge_const.te_short) <
       subghz_protocol_telcoma_edge_const.te_delta) {
        subghz_protocol_decoder_telcoma_edge_add_half(instance, level);
    } else if(
        DURATION_DIFF(duration, subghz_protocol_telcoma_edge_const.te_long) <
        subghz_protocol_telcoma_edge_const.te_delta) {
        subghz_protocol_decoder_telcoma_edge_add_half(instance, level);
        subghz_protocol_decoder_telcoma_edge_add_half(instance, level);
    } else {

        uint32_t count = instance->decoder.decode_count_bit;
        if(count == 32 || count == 33) {
            uint32_t key = (count == 33) ? (uint32_t)(instance->decoder.decode_data >> 1) :
                                            (uint32_t)instance->decoder.decode_data;

            if((key >> 24) == 0xFF) {
                instance->generic.data = key;
                instance->generic.data_count_bit =
                    subghz_protocol_telcoma_edge_const.min_count_bit_for_found;
                if(instance->base.callback) {
                    instance->base.callback(&instance->base, instance->base.context);
                }
            }
        }
        instance->decoder.decode_data = 0;
        instance->decoder.decode_count_bit = 0;
        instance->half_pending = false;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_telcoma_edge_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderTelcomaEdge* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_telcoma_edge_const.min_count_bit_for_found);
}

