#include "ditec_gol4.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"

#include <string.h>

#define TAG "SubGhzProtocolDitecGOL4"

static bool ditec_gol4_raw_replay = false; // #16: TxRaw verbatim replay (freeze counter)

#define DITEC_GOL4_UPLOAD_SIZE 128
#define GOL4_RAW_BYTES 7

static const SubGhzBlockConst subghz_protocol_ditec_gol4_const = {
    .te_short = 400,
    .te_long = 1100,
    .te_delta = 200,
    .min_count_bit_for_found = 54,
};

struct SubGhzProtocolDecoderDitecGOL4 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderDitecGOL4 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderDitecGOL4);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderDitecGOL4);

typedef enum {
    DitecGOL4DecoderStepReset = 0,
    DitecGOL4DecoderStepStartBit,
    DitecGOL4DecoderStepSaveDuration,
    DitecGOL4DecoderStepCheckDuration,
} DitecGOL4DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_ditec_gol4_decoder = {
    .alloc = subghz_protocol_decoder_ditec_gol4_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_ditec_gol4_feed,
    .reset = subghz_protocol_decoder_ditec_gol4_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_ditec_gol4_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

/* host bridge: firmware functions/state the SD encoder uses (handed over at load) */
typedef struct {
    void* ditec_gol4_raw_replay;
} enc_ditec_gol4_host_t;
static const enc_ditec_gol4_host_t enc_ditec_gol4_host = {
    .ditec_gol4_raw_replay = (void*)&ditec_gol4_raw_replay,
};
static const SubGhzEncoderExternal subghz_protocol_ditec_gol4_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_ditec_gol4", &enc_ditec_gol4_host);

const SubGhzProtocol subghz_protocol_ditec_gol4 = {
    .name = SUBGHZ_PROTOCOL_DITEC_GOL4_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,

    .decoder = &subghz_protocol_ditec_gol4_decoder,
    .encoder = &subghz_protocol_ditec_gol4_encoder_ext.vtable,
};

static uint8_t subghz_protocol_ditec_gol4_get_btn_code(void);

static uint8_t gol4_bit_reverse(uint8_t b) {
    b &= 0xFF;
    uint8_t result = 0;
    for(uint8_t i = 0; i < 8; i++) {
        result = (uint8_t)((result << 1) | (b & 1));
        b >>= 1;
    }
    return result;
}

static uint8_t gol4_bit_parity(uint8_t b) {
    uint8_t p = 0;
    for(uint8_t i = 0; i < 8; i++) {
        if((b >> i) & 1u) p ^= 1u;
    }
    return p;
}

static uint8_t gol4_lcg_step(uint8_t seed, uint8_t steps) {
    uint8_t x = seed & 0xFF;
    steps &= 0xFF;
    for(uint8_t i = 0; i < steps; i++) {
        x = (uint8_t)((21 * x + 1) & 0xFF);
    }
    return x;
}

static bool gol4_encode_lcg_xor(uint8_t* raw) {
    uint8_t dec5 = (uint8_t)(raw[5] ^ 0xA7);
    uint8_t dec6 = (uint8_t)(raw[6] ^ 0x69);

    uint8_t enc6 = gol4_lcg_step(dec6, dec5);
    uint8_t enc5 = gol4_lcg_step(dec5, 0xFE);

    if(enc6 & 0x80) enc5 ^= 1;

    raw[5] = enc5;
    raw[6] = enc6;
    return true;
}

static void gol4_encode_bitrev_and_rotate(uint8_t* raw) {
    raw[0] = gol4_bit_reverse(raw[0]);
    raw[1] = gol4_bit_reverse(raw[1]);
    raw[3] = gol4_bit_reverse(raw[3]);
    raw[4] = gol4_bit_reverse(raw[4]);

    if(raw[2] == 0x0) {
        raw[2] = 0xF0;
    }
    uint8_t b2 = gol4_bit_reverse(raw[2]);
    b2 = (uint8_t)(~b2);
    b2 = (uint8_t)(((b2 << 4) | (b2 >> 4)) & 0xFF);
    b2 &= 0xDF;
    raw[2] = b2;

    raw[5] = gol4_bit_reverse(raw[5]);
    raw[6] = gol4_bit_reverse(raw[6]);

    uint8_t p5 = gol4_bit_parity(raw[5]);
    uint8_t p6 = gol4_bit_parity(raw[6]);

    uint8_t carry = 0;
    for(uint8_t r = 0; r < 3; r++) {
        for(int8_t i = 6; i >= 2; i--) {
            uint8_t new_carry = (uint8_t)((raw[i] >> 7) & 1);
            raw[i] = (uint8_t)(((raw[i] << 1) | carry) & 0xFF);
            carry = new_carry;
        }
    }

    raw[6] = (p5 == p6) ? (uint8_t)(raw[6] & 0xFBu) : (uint8_t)(raw[6] | 0x04u);
}

static bool gol4_rolling_encode(uint8_t* raw) {
    if(!raw) return false;
    if(!gol4_encode_lcg_xor(raw)) return false;
    gol4_encode_bitrev_and_rotate(raw);
    return true;
}

static void raw_to_bits(const uint8_t* raw, uint8_t* bits) {
    for(uint8_t i = 0; i < subghz_protocol_ditec_gol4_const.min_count_bit_for_found; i++) {
        uint8_t byte_idx = i / 8;
        uint8_t bit_idx = 7 - (i % 8);
        bits[i] = (uint8_t)((raw[byte_idx] >> bit_idx) & 1);
    }
}

static uint64_t bits_to_data(const uint8_t* bits) {
    uint64_t data = 0;
    for(uint8_t i = 0; i < subghz_protocol_ditec_gol4_const.min_count_bit_for_found; i++) {
        data = (data << 1) | (uint64_t)(bits[i] & 1);
    }
    return data;
}

static void subghz_protocol_ditec_gol4_encode_key(SubGhzBlockGeneric* instance) {
    uint8_t decrypted[GOL4_RAW_BYTES];

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }

    instance->btn = subghz_protocol_ditec_gol4_get_btn_code();

    if(subghz_block_generic_global_button_override_get(&instance->btn)) {
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", instance->btn);
    }

    if(ditec_gol4_raw_replay) {
        /* #16: freeze counter on TxRaw verbatim replay */
    } else if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF) {

        if(!subghz_block_generic_global_counter_override_get(&instance->cnt)) {

            if((instance->cnt + furi_hal_subghz_get_rolling_counter_mult()) > 0xFFFF) {
                instance->cnt = 0;
            } else {
                instance->cnt += furi_hal_subghz_get_rolling_counter_mult();
            }
        }
    } else {
        if((instance->cnt + 0x1) > 0xFFFF) {
            instance->cnt = 0;
        } else if(instance->cnt >= 0x1 && instance->cnt != 0xFFFE) {
            instance->cnt = 0xFFFE;
        } else {
            instance->cnt++;
        }
    }

    decrypted[0] = (uint8_t)((instance->serial >> 24) & 0xFF);
    decrypted[4] = (uint8_t)((instance->serial >> 16) & 0xFF);
    decrypted[1] = (uint8_t)((instance->serial >> 8) & 0xFF);
    decrypted[3] = (uint8_t)(instance->serial & 0xFF);
    decrypted[2] = (uint8_t)(instance->btn & 0x0F);

    uint16_t counter = (uint16_t)(instance->cnt & 0xFFFF);
    decrypted[5] = (uint8_t)(counter & 0xFF);
    decrypted[6] = (uint8_t)((counter >> 8) & 0xFF);

    gol4_rolling_encode(decrypted);

    uint8_t bits[subghz_protocol_ditec_gol4_const.min_count_bit_for_found];
    raw_to_bits(decrypted, bits);
    instance->data = bits_to_data(bits);
}

void* subghz_protocol_decoder_ditec_gol4_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderDitecGOL4), &subghz_protocol_ditec_gol4);
}

void subghz_protocol_decoder_ditec_gol4_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderDitecGOL4* instance = context;
    instance->decoder.parser_step = DitecGOL4DecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_ditec_gol4_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderDitecGOL4* instance = context;

    switch(instance->decoder.parser_step) {
    case DitecGOL4DecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_ditec_gol4_const.te_long * 22) <
                        (subghz_protocol_ditec_gol4_const.te_long * 4))) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = DitecGOL4DecoderStepStartBit;
        }
        break;

    case DitecGOL4DecoderStepStartBit:
        if((level) && (DURATION_DIFF(duration, subghz_protocol_ditec_gol4_const.te_short * 2) <
                       subghz_protocol_ditec_gol4_const.te_delta)) {
            instance->decoder.parser_step = DitecGOL4DecoderStepSaveDuration;
        } else {
            instance->decoder.parser_step = DitecGOL4DecoderStepReset;
        }
        break;

    case DitecGOL4DecoderStepSaveDuration:
        if(!level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = DitecGOL4DecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = DitecGOL4DecoderStepReset;
        }
        break;

    case DitecGOL4DecoderStepCheckDuration:
        if(level) {
            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_ditec_gol4_const.te_short) <
                subghz_protocol_ditec_gol4_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_ditec_gol4_const.te_long) <
                subghz_protocol_ditec_gol4_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = DitecGOL4DecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_ditec_gol4_const.te_long) <
                 subghz_protocol_ditec_gol4_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_ditec_gol4_const.te_short) <
                 subghz_protocol_ditec_gol4_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = DitecGOL4DecoderStepSaveDuration;
            }
        } else {
            if(DURATION_DIFF(
                   instance->decoder.te_last, subghz_protocol_ditec_gol4_const.te_long * 20) <
               (subghz_protocol_ditec_gol4_const.te_long * 3)) {
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_ditec_gol4_const.min_count_bit_for_found) {

                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit =
                        subghz_protocol_ditec_gol4_const.min_count_bit_for_found;

                    if(instance->base.callback) {
                        instance->base.callback(&instance->base, instance->base.context);
                    }
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = DitecGOL4DecoderStepReset;
            } else {
                instance->decoder.parser_step = DitecGOL4DecoderStepReset;
            }
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_ditec_gol4_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderDitecGOL4* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_ditec_gol4_const.min_count_bit_for_found);
}

bool subghz_protocol_ditec_gol4_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolEncoderDitecGOL4* instance = context;
    instance->generic.btn = btn;
    instance->generic.serial = serial;
    instance->generic.cnt = cnt;
    instance->generic.data_count_bit = subghz_protocol_ditec_gol4_const.min_count_bit_for_found;

    subghz_protocol_ditec_gol4_encode_key(&instance->generic);

    return SubGhzProtocolStatusOk ==
           subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

static uint8_t subghz_protocol_ditec_gol4_get_btn_code(void) {
    uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t original_btn_code = subghz_custom_btn_get_original();
    uint8_t btn = original_btn_code;

    if((custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) && (original_btn_code != 0)) {

        btn = original_btn_code;
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_UP) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0x2;
            break;
        case 0x2:
            btn = 0x1;
            break;
        case 0x4:
            btn = 0x1;
            break;
        case 0x8:
            btn = 0x1;
            break;
        case 0x0:
            btn = 0x1;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_DOWN) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0x4;
            break;
        case 0x2:
            btn = 0x4;
            break;
        case 0x4:
            btn = 0x2;
            break;
        case 0x8:
            btn = 0x4;
            break;
        case 0x0:
            btn = 0x4;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_LEFT) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0x8;
            break;
        case 0x2:
            btn = 0x8;
            break;
        case 0x4:
            btn = 0x8;
            break;
        case 0x8:
            btn = 0x2;
            break;
        case 0x0:
            btn = 0x2;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_RIGHT) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0x0;
            break;
        case 0x2:
            btn = 0x0;
            break;
        case 0x4:
            btn = 0x0;
            break;
        case 0x8:
            btn = 0x0;
            break;
        case 0x0:
            btn = 0x8;
            break;

        default:
            break;
        }
    }

    return btn;
}

