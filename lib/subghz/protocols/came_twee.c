#include "came_twee.h"

#include "common.h"
#include "../encode_plugin.h"
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolCameTwee"

#define DIP_PATTERN "%c%c%c%c%c%c%c%c%c%c"
#define CNT_TO_DIP(dip)                                                                     \
    (dip & 0x0200 ? '1' : '0'), (dip & 0x0100 ? '1' : '0'), (dip & 0x0080 ? '1' : '0'),     \
        (dip & 0x0040 ? '1' : '0'), (dip & 0x0020 ? '1' : '0'), (dip & 0x0010 ? '1' : '0'), \
        (dip & 0x0008 ? '1' : '0'), (dip & 0x0004 ? '1' : '0'), (dip & 0x0002 ? '1' : '0'), \
        (dip & 0x0001 ? '1' : '0')

static const SubGhzBlockConst subghz_protocol_came_twee_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 250,
    .min_count_bit_for_found = 54,
};

struct SubGhzProtocolDecoderCameTwee {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    ManchesterState manchester_saved_state;
};

struct SubGhzProtocolEncoderCameTwee {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderCameTwee);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderCameTwee);

typedef enum {
    CameTweeDecoderStepReset = 0,
    CameTweeDecoderStepDecoderData,
} CameTweeDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_came_twee_decoder = {
    .alloc = subghz_protocol_decoder_came_twee_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_came_twee_feed,
    .reset = subghz_protocol_decoder_came_twee_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_came_twee_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_came_twee_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_came_twee");

const SubGhzProtocol subghz_protocol_came_twee = {
    .name = SUBGHZ_PROTOCOL_CAME_TWEE_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_came_twee_decoder,
    .encoder = &subghz_protocol_came_twee_encoder_ext.vtable,
};

void* subghz_protocol_decoder_came_twee_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderCameTwee), &subghz_protocol_came_twee);
}

void subghz_protocol_decoder_came_twee_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderCameTwee* instance = context;
    instance->decoder.parser_step = CameTweeDecoderStepReset;
    manchester_advance(
        instance->manchester_saved_state,
        ManchesterEventReset,
        &instance->manchester_saved_state,
        NULL);
}

void subghz_protocol_decoder_came_twee_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderCameTwee* instance = context;
    ManchesterEvent event = ManchesterEventReset;
    switch(instance->decoder.parser_step) {
    case CameTweeDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_came_twee_const.te_long * 51) <
                        subghz_protocol_came_twee_const.te_delta * 20)) {

            instance->decoder.parser_step = CameTweeDecoderStepDecoderData;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            manchester_advance(
                instance->manchester_saved_state,
                ManchesterEventLongLow,
                &instance->manchester_saved_state,
                NULL);
            manchester_advance(
                instance->manchester_saved_state,
                ManchesterEventLongHigh,
                &instance->manchester_saved_state,
                NULL);
            manchester_advance(
                instance->manchester_saved_state,
                ManchesterEventShortLow,
                &instance->manchester_saved_state,
                NULL);
        }
        break;
    case CameTweeDecoderStepDecoderData:
        if(!level) {
            if(DURATION_DIFF(duration, subghz_protocol_came_twee_const.te_short) <
               subghz_protocol_came_twee_const.te_delta) {
                event = ManchesterEventShortLow;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_came_twee_const.te_long) <
                subghz_protocol_came_twee_const.te_delta) {
                event = ManchesterEventLongLow;
            } else if(
                duration >= ((uint32_t)subghz_protocol_came_twee_const.te_long * 2 +
                             subghz_protocol_came_twee_const.te_delta)) {
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_came_twee_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                manchester_advance(
                    instance->manchester_saved_state,
                    ManchesterEventLongLow,
                    &instance->manchester_saved_state,
                    NULL);
                manchester_advance(
                    instance->manchester_saved_state,
                    ManchesterEventLongHigh,
                    &instance->manchester_saved_state,
                    NULL);
                manchester_advance(
                    instance->manchester_saved_state,
                    ManchesterEventShortLow,
                    &instance->manchester_saved_state,
                    NULL);
            } else {
                instance->decoder.parser_step = CameTweeDecoderStepReset;
            }
        } else {
            if(DURATION_DIFF(duration, subghz_protocol_came_twee_const.te_short) <
               subghz_protocol_came_twee_const.te_delta) {
                event = ManchesterEventShortHigh;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_came_twee_const.te_long) <
                subghz_protocol_came_twee_const.te_delta) {
                event = ManchesterEventLongHigh;
            } else {
                instance->decoder.parser_step = CameTweeDecoderStepReset;
            }
        }
        if(event != ManchesterEventReset) {
            bool data;
            bool data_ok = manchester_advance(
                instance->manchester_saved_state, event, &instance->manchester_saved_state, &data);

            if(data_ok) {
                instance->decoder.decode_data = (instance->decoder.decode_data << 1) | !data;
                instance->decoder.decode_count_bit++;
            }
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_came_twee_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderCameTwee* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_came_twee_const.min_count_bit_for_found);
}

