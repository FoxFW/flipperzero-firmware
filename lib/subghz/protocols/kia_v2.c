#include "kia_v2.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <lib/toolbox/manchester_encoder.h>
#include <furi.h>

#define TAG "KiaV2"

#define KIA_V2_HEADER_PAIRS 252
#define KIA_V2_TOTAL_BURSTS 2
#define KIA_V2_UPLOAD_CAPACITY \
    (KIA_V2_TOTAL_BURSTS * ((KIA_V2_HEADER_PAIRS * 2) + 1 + ((53U - 1U) * 2)))

static const SubGhzBlockConst subghz_protocol_kia_v2_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 150,
    .min_count_bit_for_found = 53,
};

struct SubGhzProtocolDecoderKiaV2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;

    ManchesterState manchester_state;
};

struct SubGhzProtocolEncoderKiaV2 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderKiaV2);


SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderKiaV2);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderKiaV2);

typedef enum {
    KiaV2DecoderStepReset = 0,
    KiaV2DecoderStepCheckPreamble,
    KiaV2DecoderStepCollectRawBits,
} KiaV2DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_kia_v2_decoder = {
    .alloc = subghz_protocol_decoder_kia_v2_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_kia_v2_feed,
    .reset = subghz_protocol_decoder_kia_v2_reset,
    .get_hash_data = subghz_protocol_decoder_kia_v2_get_hash_data,
    .serialize = subghz_protocol_decoder_kia_v2_serialize,
    .deserialize = subghz_protocol_decoder_kia_v2_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_kia_v2_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_kia_v2");

const SubGhzProtocol subghz_protocol_kia_v2 = {
    .name = SUBGHZ_PROTOCOL_KIA_V2_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = &subghz_protocol_kia_v2_decoder,
    .encoder = &subghz_protocol_kia_v2_encoder_ext.vtable,
};

static uint8_t kia_v2_calculate_crc(uint64_t data) {
    uint64_t data_without_crc = data >> 4;

    uint8_t bytes[6];
    bytes[0] = (uint8_t)(data_without_crc);
    bytes[1] = (uint8_t)(data_without_crc >> 8);
    bytes[2] = (uint8_t)(data_without_crc >> 16);
    bytes[3] = (uint8_t)(data_without_crc >> 24);
    bytes[4] = (uint8_t)(data_without_crc >> 32);
    bytes[5] = (uint8_t)(data_without_crc >> 40);

    uint8_t crc = 0;
    for(int i = 0; i < 6; i++) {
        crc ^= (bytes[i] & 0x0F) ^ (bytes[i] >> 4);
    }

    return (crc + 1) & 0x0F;
}

void* subghz_protocol_decoder_kia_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderKiaV2* instance = calloc(1, sizeof(SubGhzProtocolDecoderKiaV2));
    if(!instance) {
        return NULL;
    }
    instance->base.protocol = &subghz_protocol_kia_v2;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_kia_v2_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2* instance = context;
    instance->decoder.parser_step = KiaV2DecoderStepReset;
    instance->header_count = 0;
    instance->manchester_state = ManchesterStateMid1;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_kia_v2_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2* instance = context;

    switch(instance->decoder.parser_step) {
    case KiaV2DecoderStepReset:
        if((level) && (DURATION_DIFF(duration, subghz_protocol_kia_v2_const.te_long) <
                       subghz_protocol_kia_v2_const.te_delta)) {
            instance->decoder.parser_step = KiaV2DecoderStepCheckPreamble;
            instance->decoder.te_last = duration;
            instance->header_count = 0;
            manchester_advance(
                instance->manchester_state,
                ManchesterEventReset,
                &instance->manchester_state,
                NULL);
        }
        break;

    case KiaV2DecoderStepCheckPreamble:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_kia_v2_const.te_long) <
               subghz_protocol_kia_v2_const.te_delta) {
                instance->decoder.te_last = duration;
                instance->header_count++;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_kia_v2_const.te_short) <
                subghz_protocol_kia_v2_const.te_delta) {
                if(instance->header_count >= 100) {
                    instance->header_count = 0;
                    instance->decoder.decode_data = 0;
                    instance->decoder.decode_count_bit = 1;
                    instance->decoder.parser_step = KiaV2DecoderStepCollectRawBits;
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                } else {
                    instance->decoder.te_last = duration;
                }
            } else {
                instance->decoder.parser_step = KiaV2DecoderStepReset;
            }
        } else {
            if(DURATION_DIFF(duration, subghz_protocol_kia_v2_const.te_long) <
               subghz_protocol_kia_v2_const.te_delta) {
                instance->header_count++;
                instance->decoder.te_last = duration;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_kia_v2_const.te_short) <
                subghz_protocol_kia_v2_const.te_delta) {
                instance->decoder.te_last = duration;
            } else {
                instance->decoder.parser_step = KiaV2DecoderStepReset;
            }
        }
        break;

    case KiaV2DecoderStepCollectRawBits: {
        ManchesterEvent event;
        if(DURATION_DIFF(duration, subghz_protocol_kia_v2_const.te_short) <
           subghz_protocol_kia_v2_const.te_delta) {
            event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
        } else if(DURATION_DIFF(duration, subghz_protocol_kia_v2_const.te_long) <
                  subghz_protocol_kia_v2_const.te_delta) {
            event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
        } else {
            instance->decoder.parser_step = KiaV2DecoderStepReset;
            break;
        }

        bool data_bit;
        if(manchester_advance(
               instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
            instance->decoder.decode_data = (instance->decoder.decode_data << 1) | data_bit;
            instance->decoder.decode_count_bit++;

            if(instance->decoder.decode_count_bit == 53) {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                instance->generic.serial =
                    (uint32_t)((instance->generic.data >> 20) & 0xFFFFFFFF);
                instance->generic.btn = (uint8_t)((instance->generic.data >> 16) & 0x0F);

                uint16_t raw_count = (uint16_t)((instance->generic.data >> 4) & 0xFFF);
                instance->generic.cnt = ((raw_count >> 4) | (raw_count << 8)) & 0xFFF;

                if(((instance->generic.data & 0x0F) ==
                    kia_v2_calculate_crc(instance->generic.data)) &&
                   instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);

                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->header_count = 0;
                instance->decoder.parser_step = KiaV2DecoderStepReset;
            }
        }
        break;
    }
    }
}

uint8_t subghz_protocol_decoder_kia_v2_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2* instance = context;

    uint32_t hash = instance->generic.serial;
    hash ^= (instance->generic.btn << 24);
    hash ^= (instance->generic.cnt << 12);

    return (uint8_t)(hash ^ (hash >> 8) ^ (hash >> 16) ^ (hash >> 24));
}

SubGhzProtocolStatus subghz_protocol_decoder_kia_v2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t crc = instance->generic.data & 0x0F;
    if(!flipper_format_write_uint32(flipper_format, "CRC", &crc, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    uint32_t raw_count = (uint16_t)((instance->generic.data >> 4) & 0xFFF);
    if(!flipper_format_write_uint32(flipper_format, "RawCnt", &raw_count, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_kia_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_kia_v2_const.min_count_bit_for_found);
    if(ret == SubGhzProtocolStatusOk) {
        instance->generic.serial = (uint32_t)((instance->generic.data >> 20) & 0xFFFFFFFF);
        instance->generic.btn = (uint8_t)((instance->generic.data >> 16) & 0x0F);
        uint16_t raw_count = (uint16_t)((instance->generic.data >> 4) & 0xFFF);
        instance->generic.cnt = ((raw_count >> 4) | (raw_count << 8)) & 0xFFF;
    }
    return ret;
}

