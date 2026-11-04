#include "bmw_cas3.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "common.h"
#include "../encode_plugin.h"

#define TAG "BmwCas3"

/* BMW CAS3/CAS4 PPM key fob. Constant ~250 us marks; a ~500 us space is 0 and a
 * ~1500 us space is 1. Each frame follows two back-to-back intervals of at least
 * 10 ms. The payload is AES encrypted, so the frame is identified structurally and
 * kept verbatim: the encoder replays exactly the captured bits.
 * Gate and timing from FOBworks for Flipper (GPL-3.0, flipper_oem_wire.c). */

#define BMW_CAS3_SYNC_MIN_US  10000u
#define BMW_CAS3_MARK_MIN_US  100u
#define BMW_CAS3_MARK_MAX_US  400u
#define BMW_CAS3_ZERO_MIN_US  300u
#define BMW_CAS3_ZERO_MAX_US  700u
#define BMW_CAS3_ONE_MIN_US   1050u
#define BMW_CAS3_ONE_MAX_US   1950u
#define BMW_CAS3_BYTES        (BMW_CAS3_MAX_BITS / 8u)

typedef enum {
    BmwCas3StepSync = 0,
    BmwCas3StepMark,
    BmwCas3StepSpace,
} BmwCas3Step;

struct SubGhzProtocolDecoderBmwCas3 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint8_t step;
    uint8_t sync_count;
    uint8_t bit_count;
    uint8_t bits[BMW_CAS3_BYTES];
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderBmwCas3);

const SubGhzProtocolDecoder subghz_protocol_bmw_cas3_decoder = {
    .alloc = subghz_protocol_decoder_bmw_cas3_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_bmw_cas3_feed,
    .reset = subghz_protocol_decoder_bmw_cas3_reset,
    .get_hash_data = subghz_protocol_decoder_bmw_cas3_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize_data_2,
    .deserialize = subghz_protocol_decoder_bmw_cas3_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_bmw_cas3_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_bmw_cas3");

const SubGhzProtocol subghz_protocol_bmw_cas3 = {
    .name = BMW_CAS3_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_bmw_cas3_decoder,
    .encoder = &subghz_protocol_bmw_cas3_encoder_ext.vtable,
};

void* subghz_protocol_decoder_bmw_cas3_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderBmwCas3), &subghz_protocol_bmw_cas3);
}

void subghz_protocol_decoder_bmw_cas3_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderBmwCas3* instance = context;
    instance->step = BmwCas3StepSync;
    instance->sync_count = 0;
    instance->bit_count = 0;
}

static void bmw_cas3_finish(SubGhzProtocolDecoderBmwCas3* instance) {
    if(instance->bit_count >= BMW_CAS3_MIN_BITS) {
        uint64_t data = 0;
        uint64_t data_2 = 0;
        for(uint8_t i = 0; i < instance->bit_count; i++) {
            uint8_t bit = (instance->bits[i / 8] >> (7 - (i % 8))) & 1;
            if(i < 64) {
                data = (data << 1) | bit;
            } else {
                data_2 = (data_2 << 1) | bit;
            }
        }
        instance->generic.data = data;
        instance->generic.data_2 = data_2;
        instance->generic.data_count_bit = instance->bit_count;
        instance->generic.serial = (uint32_t)(data >> 32);
        if(instance->base.callback) {
            instance->base.callback(&instance->base, instance->base.context);
        }
    }
    instance->bit_count = 0;
}

static void bmw_cas3_resync(SubGhzProtocolDecoderBmwCas3* instance, uint32_t duration) {
    instance->step = BmwCas3StepSync;
    instance->sync_count = (duration >= BMW_CAS3_SYNC_MIN_US) ? 1 : 0;
}

void subghz_protocol_decoder_bmw_cas3_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderBmwCas3* instance = context;

    switch(instance->step) {
    case BmwCas3StepSync:
        if(duration >= BMW_CAS3_SYNC_MIN_US) {
            if(++instance->sync_count >= 2) {
                instance->step = BmwCas3StepMark;
                instance->bit_count = 0;
                memset(instance->bits, 0, sizeof(instance->bits));
            }
        } else {
            instance->sync_count = 0;
        }
        break;

    case BmwCas3StepMark:
        if(level && duration >= BMW_CAS3_MARK_MIN_US && duration <= BMW_CAS3_MARK_MAX_US) {
            instance->step = BmwCas3StepSpace;
        } else {
            bmw_cas3_finish(instance);
            bmw_cas3_resync(instance, duration);
        }
        break;

    case BmwCas3StepSpace: {
        int8_t bit = -1;
        if(!level) {
            if(duration >= BMW_CAS3_ZERO_MIN_US && duration <= BMW_CAS3_ZERO_MAX_US) {
                bit = 0;
            } else if(duration >= BMW_CAS3_ONE_MIN_US && duration <= BMW_CAS3_ONE_MAX_US) {
                bit = 1;
            }
        }
        if(bit < 0) {
            bmw_cas3_finish(instance);
            bmw_cas3_resync(instance, duration);
            break;
        }
        if(bit) instance->bits[instance->bit_count / 8] |= (uint8_t)(0x80 >> (instance->bit_count % 8));
        instance->bit_count++;
        if(instance->bit_count >= BMW_CAS3_MAX_BITS) {
            bmw_cas3_finish(instance);
            instance->step = BmwCas3StepSync;
            instance->sync_count = 0;
        } else {
            instance->step = BmwCas3StepMark;
        }
        break;
    }
    default:
        instance->step = BmwCas3StepSync;
        instance->sync_count = 0;
        break;
    }
}

uint8_t subghz_protocol_decoder_bmw_cas3_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderBmwCas3* instance = context;
    uint8_t hash = (uint8_t)instance->generic.data_count_bit;
    for(uint8_t i = 0; i < 8; i++) {
        hash ^= (uint8_t)(instance->generic.data >> (i * 8));
        hash ^= (uint8_t)(instance->generic.data_2 >> (i * 8));
    }
    return hash;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_bmw_cas3_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderBmwCas3* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret != SubGhzProtocolStatusOk) return ret;
    if(instance->generic.data_count_bit < BMW_CAS3_MIN_BITS ||
       instance->generic.data_count_bit > BMW_CAS3_MAX_BITS) {
        return SubGhzProtocolStatusErrorParserBitCount;
    }
    instance->generic.data_2 = 0;
    if(instance->generic.data_count_bit > 64) {
        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(!flipper_format_rewind(flipper_format) ||
           !flipper_format_read_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
            return SubGhzProtocolStatusErrorParserOthers;
        }
        for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
            instance->generic.data_2 = (instance->generic.data_2 << 8) | key_data[i];
        }
    }
    instance->generic.serial = (uint32_t)(instance->generic.data >> 32);
    return SubGhzProtocolStatusOk;
}
