#include "opel_v0.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"

#define TAG "OpelV0"

#define OPEL_V0_CARRIER_PERIOD  830
#define OPEL_V0_MERGE_THRESHOLD 120
#define OPEL_V0_GAP_MIN         4000
#define OPEL_V0_GAP_MAX         7000
#define OPEL_V0_PREAMBLE_MIN    50000
#define OPEL_V0_BIT_THRESHOLD   3200
#define OPEL_V0_MIN_BITS        40
#define OPEL_V0_MAX_BITS        150
#define OPEL_V0_FRAME_GAP       20000

#define OPEL_V0_FRAME_BITS      64
#define OPEL_V0_FRAME_DELTA     2

static const SubGhzBlockConst subghz_protocol_opel_v0_const = {
    .te_short = 830,
    .te_long = 5400,
    .te_delta = 300,
    .min_count_bit_for_found = OPEL_V0_MIN_BITS,
};

typedef enum {
    OpelV0DecoderStepReset = 0,
    OpelV0DecoderStepPreamble,
    OpelV0DecoderStepData,
} OpelV0DecoderStep;

struct SubGhzProtocolDecoderOpelV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t current_burst;
    uint32_t preamble_len;
    uint16_t bit_count;

    uint16_t burst_widths[OPEL_V0_MAX_BITS];
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderOpelV0);

struct SubGhzProtocolEncoderOpelV0 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderOpelV0);

#ifdef SUBGHZ_DECODER_TEST
#endif

const SubGhzProtocolDecoder subghz_protocol_opel_v0_decoder = {
    .alloc = subghz_protocol_decoder_opel_v0_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_opel_v0_feed,
    .reset = subghz_protocol_decoder_opel_v0_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_opel_v0_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
#ifdef SUBGHZ_DECODER_TEST
#endif
};

static const SubGhzEncoderExternal subghz_protocol_opel_v0_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_recon_manchester");

const SubGhzProtocol subghz_protocol_opel_v0 = {
    .name = SUBGHZ_PROTOCOL_OPEL_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_opel_v0_decoder,
    .encoder = &subghz_protocol_opel_v0_encoder_ext.vtable,
};

static void subghz_protocol_opel_v0_process_frame(SubGhzProtocolDecoderOpelV0* instance) {
    if(instance->bit_count < OPEL_V0_MIN_BITS) return;

    uint64_t data = 0;
    uint32_t data_2 = 0;
    uint16_t count = instance->bit_count;
    if(count > 96) count = 96;

    for(uint16_t i = 0; i < count; i++) {
        uint8_t bit = (instance->burst_widths[i] > OPEL_V0_BIT_THRESHOLD) ? 1 : 0;
        if(i < 64) {
            data = (data << 1) | bit;
        } else {
            data_2 = (data_2 << 1) | bit;
        }
    }

    if(count < (OPEL_V0_FRAME_BITS - OPEL_V0_FRAME_DELTA) ||
       count > (OPEL_V0_FRAME_BITS + OPEL_V0_FRAME_DELTA)) {
        return;
    }
    {
        uint64_t used_mask =
            (count >= 64) ? 0xFFFFFFFFFFFFFFFFULL : ((1ULL << count) - 1ULL);
        bool all_zero = (data == 0) && (data_2 == 0);
        bool all_one = (data == used_mask) &&
                       (count <= 64 || data_2 == ((1UL << (count - 64)) - 1UL));
        if(all_zero || all_one) {
            return;
        }
    }

    if(count < 64) {
        data <<= (64 - count);
    }

    instance->generic.data = data;
    instance->generic.data_count_bit = count;

    if(count > 64) {
        uint8_t extra = count - 64;
        data_2 <<= (32 - extra);
        instance->generic.seed = data_2;
    } else {
        instance->generic.seed = 0;
    }

    FURI_LOG_D(TAG, "Frame: %d bits, Key:%016llX", count, data);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void* subghz_protocol_decoder_opel_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderOpelV0* instance = malloc(sizeof(SubGhzProtocolDecoderOpelV0));
    instance->base.protocol = &subghz_protocol_opel_v0;
    instance->generic.protocol_name = subghz_protocol_opel_v0.name;
    return instance;
}

void subghz_protocol_decoder_opel_v0_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderOpelV0* instance = context;
    instance->decoder.parser_step = OpelV0DecoderStepReset;
    instance->current_burst = 0;
    instance->preamble_len = 0;
    instance->bit_count = 0;
}

void subghz_protocol_decoder_opel_v0_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderOpelV0* instance = context;

    switch(instance->decoder.parser_step) {
    case OpelV0DecoderStepReset:
        if(level) {

            instance->current_burst = duration;
            instance->preamble_len = 0;
            instance->bit_count = 0;
            instance->decoder.parser_step = OpelV0DecoderStepPreamble;
        }
        break;

    case OpelV0DecoderStepPreamble:
        if(level) {

            instance->current_burst += duration;
        } else {

            if(duration < OPEL_V0_MERGE_THRESHOLD) {

                instance->current_burst += duration;
            } else if(duration < OPEL_V0_GAP_MAX) {

                instance->preamble_len += instance->current_burst;

                if(instance->preamble_len >= OPEL_V0_PREAMBLE_MIN) {

                    FURI_LOG_D(TAG, "Preamble: %luus", instance->preamble_len);
                    instance->decoder.parser_step = OpelV0DecoderStepData;
                    instance->bit_count = 0;
                }
                instance->current_burst = 0;
            } else {

                instance->decoder.parser_step = OpelV0DecoderStepReset;
            }
        }
        break;

    case OpelV0DecoderStepData:
        if(level) {

            instance->current_burst += duration;

            if(instance->current_burst > 10000) {

                subghz_protocol_opel_v0_process_frame(instance);

                instance->preamble_len = instance->current_burst;
                instance->current_burst = 0;
                instance->bit_count = 0;
                instance->decoder.parser_step = OpelV0DecoderStepPreamble;
            }
        } else {

            if(duration < OPEL_V0_MERGE_THRESHOLD) {

                instance->current_burst += duration;
            } else if(duration >= OPEL_V0_GAP_MIN && duration <= OPEL_V0_GAP_MAX) {

                if(instance->current_burst > 500 &&
                   instance->bit_count < OPEL_V0_MAX_BITS) {
                    instance->burst_widths[instance->bit_count] =
                        (instance->current_burst > 0xFFFF) ? 0xFFFF : instance->current_burst;
                    instance->bit_count++;
                }
                instance->current_burst = 0;
            } else if(duration > OPEL_V0_FRAME_GAP) {

                if(instance->current_burst > 500 &&
                   instance->bit_count < OPEL_V0_MAX_BITS) {
                    instance->burst_widths[instance->bit_count] =
                        (instance->current_burst > 0xFFFF) ? 0xFFFF : instance->current_burst;
                    instance->bit_count++;
                }
                subghz_protocol_opel_v0_process_frame(instance);
                instance->current_burst = 0;
                instance->bit_count = 0;
                instance->preamble_len = 0;
                instance->decoder.parser_step = OpelV0DecoderStepReset;
            } else {

                if(instance->current_burst > 0) {
                    instance->current_burst += duration;
                }
            }
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_opel_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderOpelV0* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_opel_v0_const.min_count_bit_for_found);
}

