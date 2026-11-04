#include "revers_rb2.h"

#include "common.h"
#include "../encode_plugin.h"
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolRevers_RB2"

static const SubGhzBlockConst subghz_protocol_revers_rb2_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 160,
    .min_count_bit_for_found = 64,
};

struct SubGhzProtocolDecoderRevers_RB2 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    ManchesterState manchester_saved_state;
    uint16_t header_count;
};

struct SubGhzProtocolEncoderRevers_RB2 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderRevers_RB2);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderRevers_RB2);

typedef enum {
    Revers_RB2DecoderStepReset = 0,
    Revers_RB2DecoderStepHeader,
    Revers_RB2DecoderStepDecoderData,
} Revers_RB2DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_revers_rb2_decoder = {
    .alloc = subghz_protocol_decoder_revers_rb2_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_revers_rb2_feed,
    .reset = subghz_protocol_decoder_revers_rb2_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_revers_rb2_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_revers_rb2_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_revers_rb2");

const SubGhzProtocol subghz_protocol_revers_rb2 = {
    .name = SUBGHZ_PROTOCOL_REVERSRB2_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_ReversRB2,

    .decoder = &subghz_protocol_revers_rb2_decoder,
    .encoder = &subghz_protocol_revers_rb2_encoder_ext.vtable,
};

void* subghz_protocol_decoder_revers_rb2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderRevers_RB2), &subghz_protocol_revers_rb2);
}

void subghz_protocol_decoder_revers_rb2_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderRevers_RB2* instance = context;
    instance->decoder.parser_step = Revers_RB2DecoderStepReset;
    instance->header_count = 0;
    manchester_advance(
        instance->manchester_saved_state,
        ManchesterEventReset,
        &instance->manchester_saved_state,
        NULL);
}

void subghz_protocol_decoder_revers_rb2_addbit(void* context, bool data) {
    SubGhzProtocolDecoderRevers_RB2* instance = context;
    instance->decoder.decode_data = (instance->decoder.decode_data << 1) | data;
    instance->decoder.decode_count_bit++;

    if(instance->decoder.decode_count_bit >= 65) {
        instance->decoder.decode_data = 0;
        instance->decoder.decode_count_bit = 0;
        return;
    }

    if(instance->decoder.decode_count_bit <
       subghz_protocol_revers_rb2_const.min_count_bit_for_found) {
        return;
    }

    uint16_t preamble = (instance->decoder.decode_data >> 48) & 0xFF;
    uint16_t stop_code = (instance->decoder.decode_data & 0x3FF);

    if(preamble == 0xFF && stop_code == 0x200) {

        instance->generic.data = instance->decoder.decode_data;
        instance->generic.data_count_bit = instance->decoder.decode_count_bit;

        if(instance->base.callback)
            instance->base.callback(&instance->base, instance->base.context);

        instance->decoder.decode_data = 0;
        instance->decoder.decode_count_bit = 0;
        manchester_advance(
            instance->manchester_saved_state,
            ManchesterEventReset,
            &instance->manchester_saved_state,
            NULL);
    }
}

void subghz_protocol_decoder_revers_rb2_feed(void* context, bool level, volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderRevers_RB2* instance = context;
    ManchesterEvent event = ManchesterEventReset;

    switch(instance->decoder.parser_step) {
    case Revers_RB2DecoderStepReset:
        if((!level) &&
           (DURATION_DIFF(duration, 600) < subghz_protocol_revers_rb2_const.te_delta)) {
            instance->decoder.parser_step = Revers_RB2DecoderStepHeader;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            manchester_advance(
                instance->manchester_saved_state,
                ManchesterEventReset,
                &instance->manchester_saved_state,
                NULL);
        }
        break;
    case Revers_RB2DecoderStepHeader:
        if(!level) {
            if(DURATION_DIFF(duration, subghz_protocol_revers_rb2_const.te_short) <
               subghz_protocol_revers_rb2_const.te_delta) {
                if(instance->decoder.te_last == 1) {
                    instance->header_count++;
                }
                instance->decoder.te_last = level;
            } else {
                instance->header_count = 0;
                instance->decoder.te_last = 0;
                instance->decoder.parser_step = Revers_RB2DecoderStepReset;
            }
        } else {
            if(DURATION_DIFF(duration, subghz_protocol_revers_rb2_const.te_short) <
               subghz_protocol_revers_rb2_const.te_delta) {
                if(instance->decoder.te_last == 0) {
                    instance->header_count++;
                }
                instance->decoder.te_last = level;
            } else {
                instance->header_count = 0;
                instance->decoder.te_last = 0;
                instance->decoder.parser_step = Revers_RB2DecoderStepReset;
            }
        }

        if(instance->header_count == 4) {
            instance->header_count = 0;
            instance->decoder.decode_data = 0xF;
            instance->decoder.decode_count_bit = 4;
            instance->decoder.parser_step = Revers_RB2DecoderStepDecoderData;
        }
        break;
    case Revers_RB2DecoderStepDecoderData:
        if(!level) {
            if(DURATION_DIFF(duration, subghz_protocol_revers_rb2_const.te_short) <
               subghz_protocol_revers_rb2_const.te_delta) {
                event = ManchesterEventShortLow;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_revers_rb2_const.te_long) <
                subghz_protocol_revers_rb2_const.te_delta) {
                event = ManchesterEventLongLow;
            } else {
                instance->decoder.parser_step = Revers_RB2DecoderStepReset;
            }
        } else {
            if(DURATION_DIFF(duration, subghz_protocol_revers_rb2_const.te_short) <
               subghz_protocol_revers_rb2_const.te_delta) {
                event = ManchesterEventShortHigh;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_revers_rb2_const.te_long) <
                subghz_protocol_revers_rb2_const.te_delta) {
                event = ManchesterEventLongHigh;
            } else {
                instance->decoder.parser_step = Revers_RB2DecoderStepReset;
            }
        }
        if(event != ManchesterEventReset) {
            bool data;
            bool data_ok = manchester_advance(
                instance->manchester_saved_state, event, &instance->manchester_saved_state, &data);

            if(data_ok) {
                subghz_protocol_decoder_revers_rb2_addbit(instance, data);
            }
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_revers_rb2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderRevers_RB2* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_revers_rb2_const.min_count_bit_for_found);
}

