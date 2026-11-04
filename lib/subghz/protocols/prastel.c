#include "prastel.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "common.h"

#define TAG "SubGhzProtocolPrastel"

#define PRASTEL_42_COUNT_BIT 42

static const SubGhzBlockConst subghz_protocol_prastel_const = {
    .te_short = 320,
    .te_long = 640,
    .te_delta = 150,
    .min_count_bit_for_found = PRASTEL_42_COUNT_BIT,
};

struct SubGhzProtocolDecoderPrastel {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderPrastel);

typedef enum {
    PrastelDecoderStepReset = 0,
    PrastelDecoderStepFoundStartBit,
    PrastelDecoderStepSaveDuration,
    PrastelDecoderStepCheckDuration,
} PrastelDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_prastel_decoder = {
    .alloc = subghz_protocol_decoder_prastel_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_prastel_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_prastel_deserialize,
    .get_string = subghz_protocol_decoder_prastel_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_prastel_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol subghz_protocol_prastel = {
    .name = SUBGHZ_PROTOCOL_PRASTEL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,

    .decoder = &subghz_protocol_prastel_decoder,
    .encoder = &subghz_protocol_prastel_encoder,
};

static uint8_t subghz_protocol_prastel_reverse_byte(uint8_t v) {
    v = (uint8_t)((v >> 4) | (v << 4));
    v = (uint8_t)(((v & 0xCC) >> 2) | ((v & 0x33) << 2));
    v = (uint8_t)(((v & 0xAA) >> 1) | ((v & 0x55) << 1));
    return v;
}

static void subghz_protocol_prastel_unscramble(uint8_t p[7]) {
    uint8_t carry = 0;
    for(uint8_t round = 0; round < 3; round++) {
        for(uint8_t i = 2; i <= 6; i++) {
            uint8_t cur = p[i];
            p[i] = (uint8_t)((carry << 7) | (cur >> 1));
            carry = cur & 1;
        }
    }

    p[0] = subghz_protocol_prastel_reverse_byte(p[0]);
    uint8_t t2 = p[2] & 0xDF;
    p[1] = subghz_protocol_prastel_reverse_byte(p[1]);
    p[3] = subghz_protocol_prastel_reverse_byte(p[3]);
    p[4] = subghz_protocol_prastel_reverse_byte(p[4]);
    uint8_t swapped = (uint8_t)((t2 << 4) | (t2 >> 4));
    p[2] = subghz_protocol_prastel_reverse_byte((uint8_t)~swapped);

    uint8_t p6 = p[6];
    p[5] = subghz_protocol_prastel_reverse_byte(p[5]);
    if(p6 & 1) p[5] ^= 1;

    uint8_t t = (uint8_t)(((uint8_t)(p[5] * 21) + 1) * 21);
    uint8_t x = subghz_protocol_prastel_reverse_byte(p6);
    uint8_t n = (uint8_t)(t + 1);
    if(n != 0) {
        uint8_t limit = (uint8_t)~t;
        uint8_t i = 0;
        do {
            x = (uint8_t)(x * 21 + 1);
            i++;
        } while(limit > i);
    }
    p[5] = n ^ 0xA7;
    p[6] = x ^ 0x69;
}

static void subghz_protocol_prastel_unpack(uint64_t data, uint8_t p[7]) {
    uint64_t payload = ((uint64_t)((data >> 30) & 0xFFF) << 32) | (uint32_t)(data << 2);
    for(uint8_t i = 0; i < 7; i++) {
        p[i] = (uint8_t)(payload >> (0x30u - 8u * i));
    }
}

static uint8_t subghz_protocol_prastel_btn_from_raw(uint8_t raw) {
    static const uint8_t btn_map[8] = {0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x02, 0x01};

    uint8_t idx = (uint8_t)((raw & 0x0F) - 7);
    return (idx <= 7) ? btn_map[idx] : 0;
}

static void subghz_protocol_prastel_remote_controller(SubGhzBlockGeneric* instance) {
    uint8_t p[7];
    subghz_protocol_prastel_unpack(instance->data, p);
    subghz_protocol_prastel_unscramble(p);

    instance->serial = (((uint32_t)p[1] & 0xF0) << 8) | p[3] |
                       (((uint32_t)p[4] << 12) & 0xF0000) | (((uint32_t)p[2] & 0x0F) << 8) |
                       (((uint32_t)p[2] & 0x80) << 13);
    instance->btn = subghz_protocol_prastel_btn_from_raw(p[4]);
    instance->cnt = (uint16_t)(p[5] | (p[6] << 8));
}

void* subghz_protocol_decoder_prastel_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderPrastel), &subghz_protocol_prastel);
}

void subghz_protocol_decoder_prastel_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderPrastel* instance = context;
    switch(instance->decoder.parser_step) {
    case PrastelDecoderStepReset:
        if((!level) && (duration > subghz_protocol_prastel_const.te_short * 12) &&
           (duration < subghz_protocol_prastel_const.te_short * 86)) {
            instance->decoder.parser_step = PrastelDecoderStepFoundStartBit;
        }
        break;
    case PrastelDecoderStepFoundStartBit:
        if(!level) {
            break;
        } else if(
            DURATION_DIFF(duration, subghz_protocol_prastel_const.te_short) <
            subghz_protocol_prastel_const.te_delta) {
            instance->decoder.parser_step = PrastelDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        } else {
            instance->decoder.parser_step = PrastelDecoderStepReset;
        }
        break;
    case PrastelDecoderStepSaveDuration:
        if(!level) {
            if(duration >= (subghz_protocol_prastel_const.te_short * 4)) {
                instance->decoder.parser_step = PrastelDecoderStepFoundStartBit;
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_prastel_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                break;
            }
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = PrastelDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = PrastelDecoderStepReset;
        }
        break;
    case PrastelDecoderStepCheckDuration:
        if(level) {
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_prastel_const.te_short) <
                subghz_protocol_prastel_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_prastel_const.te_long) <
                subghz_protocol_prastel_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = PrastelDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_prastel_const.te_long) <
                 subghz_protocol_prastel_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_prastel_const.te_short) <
                 subghz_protocol_prastel_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = PrastelDecoderStepSaveDuration;
            } else
                instance->decoder.parser_step = PrastelDecoderStepReset;
        } else {
            instance->decoder.parser_step = PrastelDecoderStepReset;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_prastel_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderPrastel* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_prastel_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_prastel_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderPrastel* instance = context;

    subghz_protocol_prastel_remote_controller(&instance->generic);

    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = 16;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%011llX\r\n"
        "Sn:%08lX\r\n"
        "Btn:%01X\r\n"
        "Cnt:%04lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        instance->generic.data & 0x3FFFFFFFFFFULL,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt);
}
