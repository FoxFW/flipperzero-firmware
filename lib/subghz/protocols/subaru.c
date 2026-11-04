#include "subaru.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocolSubaru"

#define SUBARU_GAP_US  2800
#define SUBARU_SYNC_US 2800

static const SubGhzBlockConst subghz_protocol_subaru_const = {
    .te_short = 800,
    .te_long = 1600,
    .te_delta = 200,
    .min_count_bit_for_found = 64,
};

struct SubGhzProtocolDecoderSubaru {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;

    uint64_t key;
    uint32_t serial;
    uint8_t btn;
    uint16_t cnt;
};

struct SubGhzProtocolEncoderSubaru {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint64_t key;
    uint32_t serial;
    uint8_t button;
    uint16_t count;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderSubaru);

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderSubaru);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderSubaru);

typedef enum {
    SubaruDecoderStepReset = 0,
    SubaruDecoderStepCheckPreamble,
    SubaruDecoderStepFoundGap,
    SubaruDecoderStepFoundSync,
    SubaruDecoderStepSaveDuration,
} SubaruDecoderStep;

static const char* subaru_get_button_name(uint8_t btn) {
    static const char* const names[4] = {"Lock", "Unlock", "Trunk", "Panic"};
    return names[btn & 0x03];
}

static void subaru_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]) {
    for(size_t i = 0; i < 8; i++) {
        bytes[i] = (uint8_t)((data >> ((7U - i) * 8U)) & 0xFFU);
    }
}

static void subaru_rotate_left_3bytes(uint8_t* b0, uint8_t* b1, uint8_t* b2, uint8_t count) {
    for(uint8_t i = 0; i < count; i++) {
        uint8_t t = *b0;
        *b0 = (uint8_t)((*b0 << 1) | (*b1 >> 7));
        *b1 = (uint8_t)((*b1 << 1) | (*b2 >> 7));
        *b2 = (uint8_t)((*b2 << 1) | (t >> 7));
    }
}

static bool subaru_decode_fields_exact(
    const uint8_t* kb,
    uint32_t* out_serial,
    uint8_t* out_btn,
    uint16_t* out_cnt) {
    const uint8_t b0 = kb[0];
    const uint8_t b1 = kb[1];
    const uint8_t b2 = kb[2];
    const uint8_t b3 = kb[3];
    const uint8_t b4 = kb[4];
    const uint8_t b5 = kb[5];
    const uint8_t b6 = kb[6];
    const uint8_t b7 = kb[7];

    *out_btn = b0 & 0x0F;
    *out_serial = ((uint32_t)b1 << 16) | ((uint32_t)b2 << 8) | b3;

    uint8_t lo = 0;
    if((b4 & 0x40) == 0) lo |= 0x01;
    if((b4 & 0x80) == 0) lo |= 0x02;
    if((b5 & 0x01) == 0) lo |= 0x04;
    if((b5 & 0x02) == 0) lo |= 0x08;
    if((b6 & 0x01) == 0) lo |= 0x10;
    if((b6 & 0x02) == 0) lo |= 0x20;
    if((b5 & 0x40) == 0) lo |= 0x40;
    if((b5 & 0x80) == 0) lo |= 0x80;
    uint8_t reg_sh1 = (uint8_t)(((b7 & 0x0F) << 4) | (b5 & 0x0C) | ((b6 >> 6) & 0x03));
    uint8_t reg_sh2 = (uint8_t)(((b6 & 0x3C) << 2) | ((b7 >> 4) & 0x0F));

    uint8_t ser0 = b3;
    uint8_t ser1 = b1;
    uint8_t ser2 = b2;
    subaru_rotate_left_3bytes(&ser0, &ser1, &ser2, (uint8_t)(4U + lo));

    uint8_t t1 = (uint8_t)(ser1 ^ reg_sh1);
    uint8_t t2 = (uint8_t)(ser2 ^ reg_sh2);

    uint8_t hi =
        (uint8_t)((((t1 & 0x10) == 0) ? 0x04 : 0x00) | (((t1 & 0x20) == 0) ? 0x08 : 0x00) |
                  (((t2 & 0x80) == 0) ? 0x02 : 0x00) | (((t2 & 0x40) == 0) ? 0x01 : 0x00) |
                  (((t1 & 0x01) == 0) ? 0x40 : 0x00) | (((t1 & 0x02) == 0) ? 0x80 : 0x00) |
                  (((t2 & 0x08) == 0) ? 0x20 : 0x00) | (((t2 & 0x04) == 0) ? 0x10 : 0x00));

    const uint8_t local34 = ser0;

    const uint8_t x1 = (uint8_t)(b1 ^ reg_sh1);
    const uint8_t x2 = (uint8_t)(b2 ^ reg_sh2);
    const uint8_t reg_hi_inv = (uint8_t)(~hi);
    const uint8_t expect1 = (uint8_t)(((x1 ^ (uint8_t)(reg_hi_inv << 2)) & 0x30) |
                                      ((x1 ^ (uint8_t)(reg_hi_inv >> 6)) & 0x03) |
                                      ((x1 ^ (uint8_t)(~lo << 2)) & 0xCC));
    const uint8_t expect2 = (uint8_t)(((x2 ^ (uint8_t)(reg_hi_inv >> 2)) & 0x0C) |
                                      ((x2 ^ (uint8_t)(reg_hi_inv << 6)) & 0xC0) |
                                      ((x2 ^ (uint8_t)(~lo >> 2)) & 0x33));

    const bool valid = (((uint8_t)(b1 ^ expect1) | (uint8_t)(b2 ^ expect2)) == 0) &&
                       ((uint8_t)(b0 ^ local34) == lo);

    *out_cnt = (uint16_t)((uint16_t)hi << 8 | lo);

    return valid;
}

static bool subaru_process_data(SubGhzProtocolDecoderSubaru* instance) {
    if(instance->decoder.decode_count_bit < 64) {
        return false;
    }

    uint8_t b[8];
    subaru_u64_to_bytes_be(instance->decoder.decode_data, b);

    instance->key = ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) | ((uint64_t)b[2] << 40) |
                    ((uint64_t)b[3] << 32) | ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
                    ((uint64_t)b[6] << 8) | ((uint64_t)b[7]);

    (void)subaru_decode_fields_exact(b, &instance->serial, &instance->btn, &instance->cnt);

    return true;
}

const SubGhzProtocolDecoder subghz_protocol_subaru_decoder = {
    .alloc = subghz_protocol_decoder_subaru_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_subaru_feed,
    .reset = subghz_protocol_decoder_subaru_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_subaru_deserialize,
    .get_string = subghz_protocol_decoder_subaru_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_subaru_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_subaru");

const SubGhzProtocol subghz_protocol_subaru = {
    .name = SUBGHZ_PROTOCOL_SUBARU_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = &subghz_protocol_subaru_decoder,
    .encoder = &subghz_protocol_subaru_encoder_ext.vtable,
};

void* subghz_protocol_decoder_subaru_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderSubaru), &subghz_protocol_subaru);
}

void subghz_protocol_decoder_subaru_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderSubaru* instance = context;
    instance->decoder.parser_step = SubaruDecoderStepReset;
}

void subghz_protocol_decoder_subaru_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderSubaru* instance = context;

    const uint32_t te_short = subghz_protocol_subaru_const.te_short;
    const uint32_t te_long = subghz_protocol_subaru_const.te_long;
    const uint32_t te_delta = subghz_protocol_subaru_const.te_delta;

    switch(instance->decoder.parser_step) {
    case SubaruDecoderStepReset:
        if(level && (DURATION_DIFF(duration, te_long) < te_delta)) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->header_count = 0;
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = SubaruDecoderStepCheckPreamble;
        }
        break;

    case SubaruDecoderStepCheckPreamble:
        if(level) {
            break;
        }

        if((DURATION_DIFF(duration, te_long) < te_delta) &&
           (DURATION_DIFF(instance->decoder.te_last, te_long) < te_delta)) {
            instance->header_count++;
            break;
        }

        if((instance->header_count >= 0x15) && (DURATION_DIFF(duration, SUBARU_GAP_US) < 800)) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = SubaruDecoderStepFoundGap;
        } else {
            instance->decoder.parser_step = SubaruDecoderStepReset;
        }
        break;

    case SubaruDecoderStepFoundGap:
        if(level && (DURATION_DIFF(duration, SUBARU_SYNC_US) < 800) &&
           (DURATION_DIFF(instance->decoder.te_last, SUBARU_GAP_US) < 800)) {
            instance->decoder.parser_step = SubaruDecoderStepFoundSync;
        } else {
            instance->decoder.parser_step = SubaruDecoderStepReset;
        }
        break;

    case SubaruDecoderStepFoundSync:
        if(!level && ((DURATION_DIFF(duration, te_long) < te_delta) ||
                      (DURATION_DIFF(duration, te_short) < te_delta))) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = SubaruDecoderStepSaveDuration;
        } else {
            instance->decoder.parser_step = SubaruDecoderStepReset;
        }
        break;

    case SubaruDecoderStepSaveDuration: {
        uint8_t next_step = SubaruDecoderStepReset;
        bool bit = false;
        bool valid = false;

        if(level) {
            if(DURATION_DIFF(duration, te_long) < te_delta) {
                if(DURATION_DIFF(instance->decoder.te_last, te_short) < te_delta) {
                    bit = false;
                    valid = true;
                }
            } else if(DURATION_DIFF(duration, te_short) < te_delta) {
                if(DURATION_DIFF(instance->decoder.te_last, te_long) < te_delta) {
                    bit = true;
                    valid = true;
                }
            }

            if(valid) {
                subghz_protocol_blocks_add_bit(&instance->decoder, bit);
                next_step = SubaruDecoderStepFoundSync;
            }
        }

        instance->decoder.parser_step = next_step;

        if((instance->decoder.decode_count_bit >= 64) &&
           (instance->decoder.decode_count_bit <= 80)) {
            instance->generic.data_count_bit = instance->decoder.decode_count_bit;
            instance->generic.data = instance->decoder.decode_data;
            if(subaru_process_data(instance)) {
                instance->generic.serial = instance->serial;
                instance->generic.btn = instance->btn;
                instance->generic.cnt = instance->cnt;
            }

            if(instance->base.callback) {
                instance->base.callback(&instance->base, instance->base.context);
            }

            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = SubaruDecoderStepReset;
        }
        break;
    }
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_subaru_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderSubaru* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_subaru_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_subaru_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderSubaru* instance = context;

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.btn_length_bit = 4;
    subghz_block_generic_global.cnt_length_bit = 16;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->generic.btn);
    }
    subghz_custom_btn_set_max(4);

    uint8_t b[8];
    subaru_u64_to_bytes_be(instance->generic.data, b);
    (void)subaru_decode_fields_exact(b, &instance->serial, &instance->btn, &instance->cnt);
    instance->key = instance->generic.data;
    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->btn;
    instance->generic.cnt = instance->cnt;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%016llX\r\n"
        "Sn:%06lX Btn:%02X [%s]\r\nCnt:%04lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long long)instance->generic.data,
        (unsigned long)instance->serial,
        instance->btn,
        subaru_get_button_name(instance->btn),
        (unsigned long)instance->cnt);
}
