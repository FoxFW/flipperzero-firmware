#include "audi_v2.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include "common.h"
#include "../encode_plugin.h"

#define TAG "AudiV2"

#define AUDI_V2_TE_SHORT  280
#define AUDI_V2_TE_LONG   560
#define AUDI_V2_TE_DELTA  100
#define AUDI_V2_PREAMBLE  40
#define AUDI_V2_DATA_BITS 72
#define AUDI_V2_TRAILER   0x93

static const SubGhzBlockConst subghz_protocol_audi_v2_const = {
    .te_short = AUDI_V2_TE_SHORT,
    .te_long = AUDI_V2_TE_LONG,
    .te_delta = AUDI_V2_TE_DELTA,
    .min_count_bit_for_found = AUDI_V2_DATA_BITS,
};

struct SubGhzProtocolDecoderAudiV2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint8_t parser_step;
    uint16_t preamble_count;
    uint64_t gdata;
    uint64_t acc;
    uint16_t bit_count;
    uint8_t trailer;
    bool crc_ok;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderAudiV2);

typedef enum {
    AudiV2StepReset = 0,
    AudiV2StepPreamble = 1,
    AudiV2StepData = 2,
} AudiV2Step;

static ManchesterEvent audi_v2_event(uint32_t duration, bool level) {
    if(DURATION_DIFF(duration, AUDI_V2_TE_SHORT) <= AUDI_V2_TE_DELTA) {
        return level ? ManchesterEventShortHigh : ManchesterEventShortLow;
    }
    if(DURATION_DIFF(duration, AUDI_V2_TE_LONG) <= AUDI_V2_TE_DELTA) {
        return level ? ManchesterEventLongHigh : ManchesterEventLongLow;
    }
    return ManchesterEventReset;
}

static void audi_v2_complete(SubGhzProtocolDecoderAudiV2* instance) {
    if(instance->bit_count < AUDI_V2_DATA_BITS) return;
    uint8_t trailer = (uint8_t)(instance->acc & 0xFFU);
    instance->trailer = trailer;
    instance->crc_ok = (trailer == AUDI_V2_TRAILER);
    if(!instance->crc_ok) return;

    instance->generic.data = instance->gdata;
    instance->generic.data_count_bit = AUDI_V2_DATA_BITS;
    instance->generic.serial = 0;
    instance->generic.btn = 0;
    instance->generic.cnt = 0;
    instance->decoder.decode_data = instance->gdata;
    instance->decoder.decode_count_bit = AUDI_V2_DATA_BITS;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_audi_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderAudiV2* instance = calloc(1, sizeof(SubGhzProtocolDecoderAudiV2));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_audi_v2;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->manchester_state = ManchesterStateMid1;
    return instance;
}

void subghz_protocol_decoder_audi_v2_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderAudiV2* instance = context;
    instance->parser_step = AudiV2StepReset;
    instance->preamble_count = 0;
    instance->gdata = 0;
    instance->acc = 0;
    instance->bit_count = 0;
    instance->trailer = 0;
    instance->crc_ok = false;
    instance->manchester_state = ManchesterStateMid1;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_audi_v2_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderAudiV2* instance = context;

    switch(instance->parser_step) {
    case AudiV2StepReset:

        if(DURATION_DIFF(duration, AUDI_V2_TE_SHORT) <= AUDI_V2_TE_DELTA) {
            instance->preamble_count = 1;
            instance->parser_step = AudiV2StepPreamble;
        }
        break;

    case AudiV2StepPreamble:
        if(DURATION_DIFF(duration, AUDI_V2_TE_SHORT) <= AUDI_V2_TE_DELTA) {
            instance->preamble_count++;
        } else if(
            DURATION_DIFF(duration, AUDI_V2_TE_LONG) <= AUDI_V2_TE_DELTA &&
            instance->preamble_count >= AUDI_V2_PREAMBLE) {
            instance->parser_step = AudiV2StepData;
            instance->acc = 0;
            instance->bit_count = 0;
            instance->gdata = 0;

            manchester_advance(
                ManchesterStateMid1,
                level ? ManchesterEventLongHigh : ManchesterEventLongLow,
                &instance->manchester_state,
                NULL);
        } else {
            instance->parser_step = AudiV2StepReset;
            instance->preamble_count = 0;
        }
        break;

    case AudiV2StepData: {
        ManchesterEvent event = audi_v2_event(duration, level);
        if(event != ManchesterEventReset) {
            bool bit = false;
            if(manchester_advance(
                   instance->manchester_state, event, &instance->manchester_state, &bit)) {
                instance->acc = (instance->acc << 1) | (bit ? 1U : 0U);
                instance->bit_count++;
                if(instance->bit_count == 64) {
                    instance->gdata = instance->acc;
                    instance->acc = 0;
                }
            }
        }
        if(instance->bit_count >= AUDI_V2_DATA_BITS) {
            audi_v2_complete(instance);
            instance->acc = 0;
            instance->bit_count = 0;
            instance->parser_step = AudiV2StepReset;
            return;
        }
        if(event == ManchesterEventReset) {
            instance->acc = 0;
            instance->bit_count = 0;
            instance->parser_step = AudiV2StepReset;
        }
        break;
    }

    default:
        break;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_audi_v2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderAudiV2* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t trailer = instance->trailer;
    if(!flipper_format_write_uint32(flipper_format, "Trailer", &trailer, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint8_t fdata[9];
    uint64_t gd = instance->generic.data;
    for(uint8_t b = 0; b < 8; b++) fdata[b] = (uint8_t)(gd >> (56 - b * 8));
    fdata[8] = instance->trailer;
    if(!flipper_format_write_hex(flipper_format, "Data", fdata, sizeof(fdata))) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_audi_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderAudiV2* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_audi_v2_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    instance->gdata = instance->generic.data;
    uint32_t trailer = 0;
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Trailer", &trailer, 1)) {
        instance->trailer = (uint8_t)(trailer & 0xFFU);
    } else {
        instance->trailer = 0;
    }
    instance->crc_ok = (instance->trailer == AUDI_V2_TRAILER);

    instance->generic.serial = 0;
    instance->generic.btn = 0;
    instance->generic.cnt = 0;
    instance->decoder.decode_data = instance->gdata;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_audi_v2_decoder = {
    .alloc = subghz_protocol_decoder_audi_v2_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_audi_v2_feed,
    .reset = subghz_protocol_decoder_audi_v2_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_audi_v2_serialize,
    .deserialize = subghz_protocol_decoder_audi_v2_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_audi_v2_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_recon_manchester");

const SubGhzProtocol subghz_protocol_audi_v2 = {
    .name = AUDI_V2_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_audi_v2_decoder,
    .encoder = &subghz_protocol_audi_v2_encoder_ext.vtable,
};
