#include "secplus_v1.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocoSecPlusV1"

static bool secplus_v1_raw_replay = false; // #16: TxRaw verbatim replay (freeze counter)

#define SECPLUS_V1_BIT_ERR -1
#define SECPLUS_V1_BIT_0   0
#define SECPLUS_V1_BIT_1   1
#define SECPLUS_V1_BIT_2   2

#define SECPLUS_V1_PACKET_1_HEADER     0x00
#define SECPLUS_V1_PACKET_2_HEADER     0x02
#define SECPLUS_V1_PACKET_1_INDEX_BASE 0
#define SECPLUS_V1_PACKET_2_INDEX_BASE 21
#define SECPLUS_V1_PACKET_1_ACCEPTED   (1 << 0)
#define SECPLUS_V1_PACKET_2_ACCEPTED   (1 << 1)

static const SubGhzBlockConst subghz_protocol_secplus_v1_const = {
    .te_short = 500,
    .te_long = 1500,
    .te_delta = 100,
    .min_count_bit_for_found = 21,
};

struct SubGhzProtocolDecoderSecPlus_v1 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t packet_accepted;
    uint8_t base_packet_index;
    uint8_t data_array[44];
};

struct SubGhzProtocolEncoderSecPlus_v1 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t data_array[44];
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderSecPlus_v1);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderSecPlus_v1);

typedef enum {
    SecPlus_v1DecoderStepReset = 0,
    SecPlus_v1DecoderStepSearchStartBit,
    SecPlus_v1DecoderStepSaveDuration,
    SecPlus_v1DecoderStepDecoderData,
} SecPlus_v1DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_secplus_v1_decoder = {
    .alloc = subghz_protocol_decoder_secplus_v1_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_secplus_v1_feed,
    .reset = subghz_protocol_decoder_secplus_v1_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_secplus_v1_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

/* host bridge: firmware functions/state the SD encoder uses (handed over at load) */
typedef struct {
    void* secplus_v1_raw_replay;
} enc_secplus_v1_host_t;
static const enc_secplus_v1_host_t enc_secplus_v1_host = {
    .secplus_v1_raw_replay = (void*)&secplus_v1_raw_replay,
};
static const SubGhzEncoderExternal subghz_protocol_secplus_v1_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_secplus_v1", &enc_secplus_v1_host);

const SubGhzProtocol subghz_protocol_secplus_v1 = {
    .name = SUBGHZ_PROTOCOL_SECPLUS_V1_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay | SubGhzProtocolFlag_Save,

    .decoder = &subghz_protocol_secplus_v1_decoder,
    .encoder = &subghz_protocol_secplus_v1_encoder_ext.vtable,
};

void* subghz_protocol_decoder_secplus_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderSecPlus_v1), &subghz_protocol_secplus_v1);
}

void subghz_protocol_decoder_secplus_v1_reset(void* context) {
    furi_assert(context);

}

static void subghz_protocol_secplus_v1_decode(SubGhzProtocolDecoderSecPlus_v1* instance) {
    uint32_t rolling = 0;
    uint32_t fixed = 0;
    uint32_t acc = 0;
    uint8_t digit = 0;

    for(uint8_t i = 1; i < 21; i += 2) {
        digit = instance->data_array[i];
        rolling = (rolling * 3) + digit;
        acc += digit;

        digit = (60 + instance->data_array[i + 1] - acc) % 3;
        fixed = (fixed * 3) + digit;
        acc += digit;
    }

    acc = 0;

    for(uint8_t i = 22; i < 42; i += 2) {
        digit = instance->data_array[i];
        rolling = (rolling * 3) + digit;
        acc += digit;

        digit = (60 + instance->data_array[i + 1] - acc) % 3;
        fixed = (fixed * 3) + digit;
        acc += digit;
    }

    rolling = subghz_protocol_blocks_reverse_key(rolling, 32);
    instance->generic.data = (uint64_t)fixed << 32 | rolling;

    instance->generic.data_count_bit =
        subghz_protocol_secplus_v1_const.min_count_bit_for_found * 2;
}

void subghz_protocol_decoder_secplus_v1_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderSecPlus_v1* instance = context;

    switch(instance->decoder.parser_step) {
    case SecPlus_v1DecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_secplus_v1_const.te_short * 120) <
                        subghz_protocol_secplus_v1_const.te_delta * 120)) {

            instance->decoder.parser_step = SecPlus_v1DecoderStepSearchStartBit;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->packet_accepted = 0;
            memset(instance->data_array, 0, sizeof(instance->data_array));
        }
        break;
    case SecPlus_v1DecoderStepSearchStartBit:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_secplus_v1_const.te_short) <
               subghz_protocol_secplus_v1_const.te_delta) {
                instance->base_packet_index = SECPLUS_V1_PACKET_1_INDEX_BASE;
                instance
                    ->data_array[instance->decoder.decode_count_bit + instance->base_packet_index] =
                    SECPLUS_V1_BIT_0;
                instance->decoder.decode_count_bit++;
                instance->decoder.parser_step = SecPlus_v1DecoderStepSaveDuration;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_secplus_v1_const.te_long) <
                subghz_protocol_secplus_v1_const.te_delta) {
                instance->base_packet_index = SECPLUS_V1_PACKET_2_INDEX_BASE;
                instance
                    ->data_array[instance->decoder.decode_count_bit + instance->base_packet_index] =
                    SECPLUS_V1_BIT_2;
                instance->decoder.decode_count_bit++;
                instance->decoder.parser_step = SecPlus_v1DecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = SecPlus_v1DecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = SecPlus_v1DecoderStepReset;
        }
        break;
    case SecPlus_v1DecoderStepSaveDuration:
        if(!level) {
            if(DURATION_DIFF(duration, subghz_protocol_secplus_v1_const.te_short * 120) <
               subghz_protocol_secplus_v1_const.te_delta * 120) {
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_secplus_v1_const.min_count_bit_for_found) {
                    if(instance->base_packet_index == SECPLUS_V1_PACKET_1_INDEX_BASE)
                        instance->packet_accepted |= SECPLUS_V1_PACKET_1_ACCEPTED;
                    if(instance->base_packet_index == SECPLUS_V1_PACKET_2_INDEX_BASE)
                        instance->packet_accepted |= SECPLUS_V1_PACKET_2_ACCEPTED;

                    if(instance->packet_accepted ==
                       (SECPLUS_V1_PACKET_1_ACCEPTED | SECPLUS_V1_PACKET_2_ACCEPTED)) {
                        subghz_protocol_secplus_v1_decode(instance);

                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                        instance->decoder.parser_step = SecPlus_v1DecoderStepReset;
                    }
                }
                instance->decoder.parser_step = SecPlus_v1DecoderStepSearchStartBit;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
            } else {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = SecPlus_v1DecoderStepDecoderData;
            }
        } else {
            instance->decoder.parser_step = SecPlus_v1DecoderStepReset;
        }
        break;
    case SecPlus_v1DecoderStepDecoderData:
        if(level && (instance->decoder.decode_count_bit <=
                     subghz_protocol_secplus_v1_const.min_count_bit_for_found)) {
            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_secplus_v1_const.te_short * 3) <
                subghz_protocol_secplus_v1_const.te_delta * 3) &&
               (DURATION_DIFF(duration, subghz_protocol_secplus_v1_const.te_short) <
                subghz_protocol_secplus_v1_const.te_delta)) {
                instance
                    ->data_array[instance->decoder.decode_count_bit + instance->base_packet_index] =
                    SECPLUS_V1_BIT_0;
                instance->decoder.decode_count_bit++;
                instance->decoder.parser_step = SecPlus_v1DecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_secplus_v1_const.te_short * 2) <
                 subghz_protocol_secplus_v1_const.te_delta * 2) &&
                (DURATION_DIFF(duration, subghz_protocol_secplus_v1_const.te_short * 2) <
                 subghz_protocol_secplus_v1_const.te_delta * 2)) {
                instance
                    ->data_array[instance->decoder.decode_count_bit + instance->base_packet_index] =
                    SECPLUS_V1_BIT_1;
                instance->decoder.decode_count_bit++;
                instance->decoder.parser_step = SecPlus_v1DecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_secplus_v1_const.te_short) <
                 subghz_protocol_secplus_v1_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_secplus_v1_const.te_short * 3) <
                 subghz_protocol_secplus_v1_const.te_delta * 3)) {
                instance
                    ->data_array[instance->decoder.decode_count_bit + instance->base_packet_index] =
                    SECPLUS_V1_BIT_2;
                instance->decoder.decode_count_bit++;
                instance->decoder.parser_step = SecPlus_v1DecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = SecPlus_v1DecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = SecPlus_v1DecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_secplus_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderSecPlus_v1* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        2 * subghz_protocol_secplus_v1_const.min_count_bit_for_found);
}

bool subghz_protocol_secplus_v1_check_fixed(uint32_t fixed) {

    uint8_t id1 = (fixed / 9) % 3;
    uint8_t btn = fixed % 3;

    do {
        if(id1 == 0) return false;
        if(!(btn == 0 || btn == 1 || btn == 2)) return false;
    } while(false);
    return true;
}

