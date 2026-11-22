#include "belkorn.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "common.h"
#include "decode_plugin.h"
#include "../encode_plugin.h"

#include "../blocks/custom_btn_i.h"

#define TAG "BelKorn"

static const SubGhzBlockConst subghz_protocol_belkorn_const = {
    .te_short = 481,
    .te_long = 962,
    .te_delta = 180,
    .min_count_bit_for_found = 118,
};

#define BELKORN_GAP_MIN 2500
#define BELKORN_SYNC    0x801ULL

#define BELKORN_L24_MASK   0xFFFFFFUL
#define BELKORN_STEP_A     0x810UL
#define BELKORN_STEP_B     0x1028UL
#define BELKORN_WRAP_A     0x1C282CUL
#define BELKORN_WRAP_B     0x80800UL
#define BELKORN_B3_STEP_A  0x10
#define BELKORN_B34_WRAP_B 0x108
#define BELKORN_B6_BIAS    0xBA
#define BELKORN_B4_BASE    0x5A
#define BELKORN_L24_AB     0x0748CCUL
#define BELKORN_B3_AB      0x30

typedef enum {
    BelKornDecoderStepReset = 0,
    BelKornDecoderStepData,
} BelKornDecoderStep;

typedef struct {
    uint8_t serial_hi;
    uint32_t l24;
    uint8_t b3;
} BelKornOrigin;

struct SubGhzProtocolDecoderBelKorn {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;

    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderBelKorn);

const SubGhzProtocolDecoder subghz_protocol_belkorn_decoder = {
    .alloc = subghz_protocol_decoder_belkorn_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_belkorn_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_belkorn_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize_data_2,
    .deserialize = subghz_protocol_decoder_belkorn_deserialize,
    .get_string = subghz_protocol_decoder_belkorn_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_belkorn_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_belkorn");

const SubGhzProtocol subghz_protocol_belkorn = {
    .name = SUBGHZ_PROTOCOL_BELKORN_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_belkorn_decoder,
    .encoder = &subghz_protocol_belkorn_encoder_ext.vtable,
};

static uint32_t belkorn_get(uint64_t data, uint64_t data_2, uint8_t pos, uint8_t len) {
    const uint64_t mask = ((uint64_t)1 << len) - 1;
    return (pos + len <= 64) ? (uint32_t)((data >> (64 - pos - len)) & mask) :
                               (uint32_t)((data_2 >> (118 - pos - len)) & mask);
}

static uint16_t subghz_protocol_belkorn_b6_wraps(uint16_t n) {
    return (n >> 8) + (((n & 0xFF) > 0x45) ? 1 : 0);
}

static uint32_t subghz_protocol_belkorn_jitter(uint8_t lap, uint8_t c) {
    const uint16_t v = (uint16_t)((c >> 1) - 3);
    uint32_t jitter = (uint32_t)lap << 21;
    for(uint8_t i = 0; i < 8; i++) {
        if(!((lap >> i) & 1)) {
            continue;
        }
        if(!((v >> (i + 1)) & 1)) {
            jitter += 257UL << (5 + i);
        }
        if(i && !((v >> (i - 1)) & 1)) {
            jitter += 1UL << (3 + i);
        }
    }
    return jitter;
}

static void subghz_protocol_belkorn_check_remote_controller(
    SubGhzBlockGeneric* instance,
    BelKornOrigin* origin) {
    const uint64_t data = instance->data;
    const uint64_t data_2 = instance->data_2;

    origin->serial_hi = belkorn_get(data, data_2, 12, 4);
    instance->serial = belkorn_get(data, data_2, 16, 32);
    instance->btn = belkorn_get(data, data_2, 112, 6);

    const uint32_t l24 = belkorn_get(data, data_2, 48, 8) |
                         (belkorn_get(data, data_2, 56, 8) << 8) |
                         (belkorn_get(data, data_2, 64, 8) << 16);
    const uint8_t b3 = belkorn_get(data, data_2, 72, 8);
    const uint8_t b4 = belkorn_get(data, data_2, 80, 8);
    const uint8_t b5 = belkorn_get(data, data_2, 88, 8);
    const uint8_t b6 = belkorn_get(data, data_2, 96, 8);
    const uint8_t c = belkorn_get(data, data_2, 104, 8);

    const uint16_t n = (uint16_t)(((uint16_t)((c & 1) ? b5 : b6) << 8) | c);
    instance->cnt = n;

    if(c & 1) {
        const uint16_t wraps = subghz_protocol_belkorn_b6_wraps(n);
        origin->l24 =
            (l24 - (uint32_t)(n >> 1) * BELKORN_STEP_B + (uint32_t)wraps * BELKORN_WRAP_B -
             subghz_protocol_belkorn_jitter(n >> 8, c) - BELKORN_L24_AB) &
            BELKORN_L24_MASK;
        origin->b3 = (uint8_t)((((uint16_t)b3 | (uint16_t)b4 << 8) - wraps * BELKORN_B34_WRAP_B) +
                               BELKORN_B3_AB);
    } else {
        origin->l24 =
            (l24 - (uint32_t)(n >> 1) * BELKORN_STEP_A - (uint32_t)(n >> 8) * BELKORN_WRAP_A) &
            BELKORN_L24_MASK;
        origin->b3 = (uint8_t)(b3 - (n >> 1) * BELKORN_B3_STEP_A);
    }

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(3);
}

void* subghz_protocol_decoder_belkorn_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderBelKorn* instance = subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderBelKorn), &subghz_protocol_belkorn);
    instance->decoder.parser_step = BelKornDecoderStepReset;
    return instance;
}

uint8_t subghz_protocol_decoder_belkorn_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderBelKorn* instance = context;
    BelKornOrigin origin;
    subghz_protocol_belkorn_check_remote_controller(&instance->generic, &origin);
    return subghz_protocol_decoder_common_get_hash_data_serial(context);
}

static bool subghz_protocol_belkorn_frame_valid(uint64_t data, uint64_t data_2) {
    const uint8_t btn = data_2 & 0x3F;
    return ((data >> 52) == BELKORN_SYNC) && (btn != 0) && ((btn & (btn - 1)) == 0);
}

void subghz_protocol_decoder_belkorn_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderBelKorn* instance = context;

    switch(instance->decoder.parser_step) {
    case BelKornDecoderStepReset:
        if((!level) && (duration >= BELKORN_GAP_MIN)) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = BelKornDecoderStepData;
        }
        break;
    case BelKornDecoderStepData:
        if(level != ((instance->decoder.decode_count_bit & 1) == 0)) {
            instance->decoder.parser_step = BelKornDecoderStepReset;
            break;
        }
        if(DURATION_DIFF(duration, subghz_protocol_belkorn_const.te_short) <
           subghz_protocol_belkorn_const.te_delta) {
            subghz_protocol_blocks_add_bit(&instance->decoder, 0);
        } else if(
            DURATION_DIFF(duration, subghz_protocol_belkorn_const.te_long) <
            subghz_protocol_belkorn_const.te_delta) {
            subghz_protocol_blocks_add_bit(&instance->decoder, 1);
        } else {
            instance->decoder.parser_step = BelKornDecoderStepReset;
            break;
        }

        if(instance->decoder.decode_count_bit == 64) {
            instance->generic.data = instance->decoder.decode_data;
            instance->decoder.decode_data = 0;
        } else if(
            instance->decoder.decode_count_bit ==
            subghz_protocol_belkorn_const.min_count_bit_for_found) {
            instance->generic.data_2 = instance->decoder.decode_data;
            instance->generic.data_count_bit = instance->decoder.decode_count_bit;
            instance->decoder.parser_step = BelKornDecoderStepReset;

            if(subghz_protocol_belkorn_frame_valid(
                   instance->generic.data, instance->generic.data_2) &&
               instance->base.callback) {
                instance->base.callback(&instance->base, instance->base.context);
            }
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_belkorn_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderBelKorn* instance = context;

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_belkorn_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(!flipper_format_read_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Missing Data");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }

        instance->generic.data_2 = 0;
        for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
            instance->generic.data_2 = instance->generic.data_2 << 8 | key_data[i];
        }
    } while(false);
    return ret;
}

void subghz_protocol_decoder_belkorn_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderBelKorn* instance = context;

    BelKornOrigin origin;
    subghz_protocol_belkorn_check_remote_controller(&instance->generic, &origin);

    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = 16;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 6;

    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}
