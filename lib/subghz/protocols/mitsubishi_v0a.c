#include "mitsubishi_v0a.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/custom_btn_i.h"

#define TAG "MitsubishiProtocolV0a"

static const SubGhzBlockConst subghz_protocol_mitsubishi_v0a_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 61,
};

#define MITSUBISHI_V0A_BIT_COUNT 61U
#define MITSUBISHI_V0A_LONG_PREAMBLE_PAIRS 0x13FU
#define MITSUBISHI_V0A_SHORT_PREAMBLE_PAIRS 0x50U

#define MITSUBISHI_V0A_PREAMBLE_MIN 72U
#define MITSUBISHI_V0A_PREAMBLE_MAX 88U
#define MITSUBISHI_V0A_SYNC_US 750U
#define MITSUBISHI_V0A_GAP_US 1500U

#define MITSUBISHI_V0A_UPLOAD_CAPACITY                                          \
    (2U + (MITSUBISHI_V0A_LONG_PREAMBLE_PAIRS * 2U) + (MITSUBISHI_V0A_BIT_COUNT * 2U) + 2U + \
     (MITSUBISHI_V0A_SHORT_PREAMBLE_PAIRS * 2U) + (MITSUBISHI_V0A_BIT_COUNT * 2U) + 2U)

struct SubGhzProtocolDecoderMitsubishiV0a {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;

    uint64_t last_data;
    bool have_last;
};

struct SubGhzProtocolEncoderMitsubishiV0a {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderMitsubishiV0a);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderMitsubishiV0a);

typedef enum {
    MitsubishiV0aDecoderStepReset = 0,
    MitsubishiV0aDecoderStepCheckPreambula,
    MitsubishiV0aDecoderStepSaveDuration,
    MitsubishiV0aDecoderStepCheckDuration,
} MitsubishiV0aDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_mitsubishi_v0a_decoder = {
    .alloc = subghz_protocol_decoder_mitsubishi_v0a_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_mitsubishi_v0a_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_mitsubishi_v0a_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_mitsubishi_v0a_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_mitsubishi_v0a");

const SubGhzProtocol subghz_protocol_mitsubishi_v0a = {
    .name = MITSUBISHI_PROTOCOL_V0A_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_mitsubishi_v0a_decoder,
    .encoder = &subghz_protocol_mitsubishi_v0a_encoder_ext.vtable,
};

static uint8_t mitsubishi_v0a_crc8(uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(size_t j = 0; j < 8; j++) {
            if((crc & 0x80) != 0)
                crc = (uint8_t)((crc << 1) ^ 0x7F);
            else
                crc <<= 1;
        }
    }
    return crc;
}

static uint8_t mitsubishi_v0a_calculate_crc(uint64_t data) {
    uint8_t crc_data[6];
    crc_data[0] = (data >> 48) & 0xFF;
    crc_data[1] = (data >> 40) & 0xFF;
    crc_data[2] = (data >> 32) & 0xFF;
    crc_data[3] = (data >> 24) & 0xFF;
    crc_data[4] = (data >> 16) & 0xFF;
    crc_data[5] = (data >> 8) & 0xFF;

    return mitsubishi_v0a_crc8(crc_data, 6);
}

static bool mitsubishi_v0a_verify_crc(uint64_t data) {
    uint8_t received_crc = data & 0xFF;
    uint8_t calculated_crc = mitsubishi_v0a_calculate_crc(data);
    return (received_crc == calculated_crc);
}


void* subghz_protocol_decoder_mitsubishi_v0a_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMitsubishiV0a* instance =
        malloc(sizeof(SubGhzProtocolDecoderMitsubishiV0a));
    instance->base.protocol = &subghz_protocol_mitsubishi_v0a;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->have_last = false;
    instance->last_data = 0;

    return instance;
}

void subghz_protocol_decoder_mitsubishi_v0a_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderMitsubishiV0a* instance = context;

    switch(instance->decoder.parser_step) {
    case MitsubishiV0aDecoderStepReset:
        if((level) && (DURATION_DIFF(duration, subghz_protocol_mitsubishi_v0a_const.te_short) <
                       subghz_protocol_mitsubishi_v0a_const.te_delta)) {
            instance->decoder.parser_step = MitsubishiV0aDecoderStepCheckPreambula;
            instance->decoder.te_last = duration;
            instance->header_count = 0;
        }
        break;

    case MitsubishiV0aDecoderStepCheckPreambula:
        if(level) {
            if((DURATION_DIFF(duration, subghz_protocol_mitsubishi_v0a_const.te_short) <
                subghz_protocol_mitsubishi_v0a_const.te_delta) ||
               (DURATION_DIFF(duration, subghz_protocol_mitsubishi_v0a_const.te_long) <
                subghz_protocol_mitsubishi_v0a_const.te_delta)) {
                instance->decoder.te_last = duration;
            } else {
                instance->decoder.parser_step = MitsubishiV0aDecoderStepReset;
            }
        } else if(
            (DURATION_DIFF(duration, subghz_protocol_mitsubishi_v0a_const.te_short) <
             subghz_protocol_mitsubishi_v0a_const.te_delta) &&
            (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_mitsubishi_v0a_const.te_short) <
             subghz_protocol_mitsubishi_v0a_const.te_delta)) {

            instance->header_count++;
            break;
        } else if(
            (DURATION_DIFF(duration, subghz_protocol_mitsubishi_v0a_const.te_long) <
             subghz_protocol_mitsubishi_v0a_const.te_delta) &&
            (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_mitsubishi_v0a_const.te_long) <
             subghz_protocol_mitsubishi_v0a_const.te_delta)) {

            if(instance->header_count > 15) {
                instance->decoder.parser_step = MitsubishiV0aDecoderStepSaveDuration;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 1;
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
            } else {
                instance->decoder.parser_step = MitsubishiV0aDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = MitsubishiV0aDecoderStepReset;
        }
        break;

    case MitsubishiV0aDecoderStepSaveDuration:
        if(level) {
            if(duration >= (subghz_protocol_mitsubishi_v0a_const.te_long +
                             subghz_protocol_mitsubishi_v0a_const.te_delta * 2UL)) {

                instance->decoder.parser_step = MitsubishiV0aDecoderStepReset;
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_mitsubishi_v0a_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(mitsubishi_v0a_verify_crc(instance->generic.data)) {
                        bool is_repeat_in_range = instance->have_last &&
                            (instance->last_data == instance->generic.data) &&
                            (instance->header_count >= MITSUBISHI_V0A_PREAMBLE_MIN) &&
                            (instance->header_count <= MITSUBISHI_V0A_PREAMBLE_MAX);
                        instance->last_data = instance->generic.data;
                        instance->have_last = true;

                        if(is_repeat_in_range) {
                            if(instance->base.callback)
                                instance->base.callback(&instance->base, instance->base.context);
                        }
                    } else {
                        FURI_LOG_W(TAG, "CRC verification failed, packet rejected");
                    }
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                break;
            } else {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = MitsubishiV0aDecoderStepCheckDuration;
            }

        } else {
            instance->decoder.parser_step = MitsubishiV0aDecoderStepReset;
        }
        break;

    case MitsubishiV0aDecoderStepCheckDuration:
        if(!level) {
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_mitsubishi_v0a_const.te_short) <
                subghz_protocol_mitsubishi_v0a_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_mitsubishi_v0a_const.te_short) <
                subghz_protocol_mitsubishi_v0a_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = MitsubishiV0aDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_mitsubishi_v0a_const.te_long) <
                 subghz_protocol_mitsubishi_v0a_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_mitsubishi_v0a_const.te_long) <
                 subghz_protocol_mitsubishi_v0a_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = MitsubishiV0aDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = MitsubishiV0aDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = MitsubishiV0aDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_mitsubishi_v0a_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderMitsubishiV0a* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);

    if(ret == SubGhzProtocolStatusOk) {
        if(instance->generic.data_count_bit <
           subghz_protocol_mitsubishi_v0a_const.min_count_bit_for_found) {
            ret = SubGhzProtocolStatusErrorParserBitCount;
        }
    }

    return ret;
}

