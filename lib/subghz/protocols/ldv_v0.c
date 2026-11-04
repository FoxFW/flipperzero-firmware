#include "ldv_v0.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include "common.h"

#define TAG "LdvV0"

// LDV / Maxus V0 -- ported from firmware ldv_v0.c via KeeLoq-RX arke_misc.py (LdvV0).
// 250/500 us Manchester at 433.92 MHz ASK (standard Flipper Manchester polarity).
// Preamble = a run of >=48 identical demodulated Manchester bits terminated by a
// value flip (the flip marks data start and is discarded). 128-bit frame (16 bytes):
//   serial(32) | b4 | b5 | byte6 | 0xFF | 0xFF | byte9 | byte10 | hop(40)
// byte6 = 0x80 | (btn<<3) | 0x07. Gate (structural / fixed-field):
//   byte6 MSB set, byte6 low nibble bits == 0x07, bytes7-8 == 0xFFFF separator,
//   and the button in byte6 equals the button duplicated in the counter field.
//   cnt = ((byte9<<8)|byte10) >> 5. Decode-only (rolling code); .encoder = NULL.

#define LDV_V0_TE_SHORT     250
#define LDV_V0_TE_LONG      500
#define LDV_V0_TE_DELTA     120
#define LDV_V0_PREAMBLE_MIN 48
#define LDV_V0_DATA_BITS    128

static const SubGhzBlockConst subghz_protocol_ldv_v0_const = {
    .te_short = LDV_V0_TE_SHORT,
    .te_long = LDV_V0_TE_LONG,
    .te_delta = LDV_V0_TE_DELTA,
    .min_count_bit_for_found = LDV_V0_DATA_BITS,
};

struct SubGhzProtocolDecoderLdvV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    ManchesterState manchester_state;

    uint16_t preamble_count;
    uint8_t last_bit_value;

    uint64_t data_2; // second 64 demodulated bits (bytes 8..15)
    uint16_t bit_count;

    uint32_t serial;
    uint8_t btn;
    uint32_t cnt;
    uint64_t hop; // 40-bit rolling tail
    bool frame_ok;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderLdvV0);


typedef enum {
    LdvV0StepReset = 0,
    LdvV0StepPreamble = 1,
    LdvV0StepData = 2,
} LdvV0Step;

// Mirror of LdvV0._valid(): structural fixed-field gate.
static bool ldv_v0_valid(SubGhzProtocolDecoderLdvV0* instance) {
    uint64_t data = instance->decoder.decode_data; // first 64 bits (bytes 0..7)
    uint8_t byte6 = (uint8_t)((data >> 8) & 0xFFU);
    uint8_t byte7 = (uint8_t)(data & 0xFFU);
    uint8_t byte8 = (uint8_t)((instance->data_2 >> 56) & 0xFFU);
    uint8_t byte9 = (uint8_t)((instance->data_2 >> 48) & 0xFFU);
    uint8_t byte10 = (uint8_t)((instance->data_2 >> 40) & 0xFFU);

    if((byte6 & 0x80U) == 0) return false;
    if((byte6 & 0x07U) != 0x07U) return false;
    if(byte7 != 0xFFU || byte8 != 0xFFU) return false;

    uint8_t byte6_btn = (byte6 >> 3) & 0x03U;
    uint16_t cnt_field = (uint16_t)((byte9 << 8) | byte10);
    uint8_t cnt_btn = (cnt_field >> 3) & 0x03U;
    return byte6_btn == cnt_btn;
}

static void ldv_v0_extract(SubGhzProtocolDecoderLdvV0* instance) {
    uint64_t data = instance->decoder.decode_data;
    uint8_t byte6 = (uint8_t)((data >> 8) & 0xFFU);
    uint8_t byte9 = (uint8_t)((instance->data_2 >> 48) & 0xFFU);
    uint8_t byte10 = (uint8_t)((instance->data_2 >> 40) & 0xFFU);
    uint16_t cnt_field = (uint16_t)((byte9 << 8) | byte10);

    instance->serial = (uint32_t)(data >> 32);
    instance->btn = (byte6 >> 3) & 0x03U;
    instance->cnt = cnt_field >> 5;
    instance->hop = instance->data_2 & 0xFFFFFFFFFFULL;
}

static void ldv_v0_commit(SubGhzProtocolDecoderLdvV0* instance) {
    if(instance->decoder.decode_count_bit < LDV_V0_DATA_BITS) return;
    if(!ldv_v0_valid(instance)) return;

    ldv_v0_extract(instance);
    instance->frame_ok = true;

    instance->generic.data = instance->decoder.decode_data; // top-64 (bytes 0..7)
    instance->generic.data_count_bit = LDV_V0_DATA_BITS;
    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->btn;
    instance->generic.cnt = instance->cnt;
    // decode_data / decode_count_bit already hold the top 64 bits + full count.
    instance->decoder.decode_count_bit = LDV_V0_DATA_BITS;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_ldv_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderLdvV0* instance = calloc(1, sizeof(SubGhzProtocolDecoderLdvV0));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_ldv_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->manchester_state = ManchesterStateMid1;
    return instance;
}



void subghz_protocol_decoder_ldv_v0_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderLdvV0* instance = context;
    instance->parser_step = LdvV0StepReset;
    instance->manchester_state = ManchesterStateMid1;
    instance->preamble_count = 0;
    instance->last_bit_value = 0;
    instance->data_2 = 0;
    instance->bit_count = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_ldv_v0_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderLdvV0* instance = context;

    uint32_t ts = LDV_V0_TE_SHORT;
    uint32_t tl = LDV_V0_TE_LONG;
    uint32_t td = LDV_V0_TE_DELTA;

    // Standard Flipper Manchester event mapping.
    ManchesterEvent event = ManchesterEventReset;
    if(DURATION_DIFF(duration, ts) < td) {
        event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
    } else if(DURATION_DIFF(duration, tl) < td) {
        event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
    }

    switch(instance->parser_step) {
    case LdvV0StepReset:
        if(level && DURATION_DIFF(duration, ts) < td) {
            instance->preamble_count = 0;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->data_2 = 0;
            instance->parser_step = LdvV0StepPreamble;
            instance->manchester_state = ManchesterStateMid1;
        }
        break;

    case LdvV0StepPreamble: {
        if(duration > tl * 4) {
            instance->parser_step = LdvV0StepReset;
            break;
        }
        if(event == ManchesterEventReset) {
            instance->parser_step = LdvV0StepReset;
            break;
        }
        bool data_bit = false;
        if(manchester_advance(
               instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
            uint8_t bval = data_bit ? 1 : 0;
            if(bval == instance->last_bit_value && instance->preamble_count > 0) {
                instance->preamble_count++;
            } else if(instance->preamble_count == 0) {
                instance->last_bit_value = bval;
                instance->preamble_count = 1;
            } else {
                if(instance->preamble_count >= LDV_V0_PREAMBLE_MIN) {
                    instance->decoder.decode_data = 0;
                    instance->decoder.decode_count_bit = 0;
                    instance->data_2 = 0;
                    instance->parser_step = LdvV0StepData;
                } else {
                    instance->last_bit_value = bval;
                    instance->preamble_count = 1;
                }
            }
        }
        break;
    }

    case LdvV0StepData: {
        if(duration > tl * 4) {
            ldv_v0_commit(instance);
            instance->parser_step = LdvV0StepReset;
            break;
        }
        if(event == ManchesterEventReset) {
            instance->parser_step = LdvV0StepReset;
            break;
        }
        bool data_bit = false;
        if(manchester_advance(
               instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
            uint8_t b = data_bit ? 1 : 0;
            if(instance->decoder.decode_count_bit < 64) {
                instance->decoder.decode_data = (instance->decoder.decode_data << 1) | b;
            } else {
                instance->data_2 = (instance->data_2 << 1) | b;
            }
            instance->decoder.decode_count_bit++;
            if(instance->decoder.decode_count_bit == LDV_V0_DATA_BITS) {
                ldv_v0_commit(instance);
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->data_2 = 0;
                instance->parser_step = LdvV0StepReset;
            }
        }
        break;
    }
    default:
        break;
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_ldv_v0_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderLdvV0* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t serial = instance->serial;
    uint32_t btn = instance->btn;
    uint32_t cnt = instance->cnt;
    uint32_t data2_hi = (uint32_t)(instance->data_2 >> 32);
    uint32_t data2_lo = (uint32_t)(instance->data_2 & 0xFFFFFFFFU);
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "Btn", &btn, 1) ||
       !flipper_format_write_uint32(flipper_format, "Cnt", &cnt, 1) ||
       !flipper_format_write_uint32(flipper_format, "Data2Hi", &data2_hi, 1) ||
       !flipper_format_write_uint32(flipper_format, "Data2Lo", &data2_lo, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_ldv_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderLdvV0* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_ldv_v0_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint32_t data2_hi = 0, data2_lo = 0;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Data2Hi", &data2_hi, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Data2Lo", &data2_lo, 1);
    instance->data_2 = ((uint64_t)data2_hi << 32) | data2_lo;

    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;

    ldv_v0_extract(instance);
    instance->frame_ok = ldv_v0_valid(instance);

    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->btn;
    instance->generic.cnt = instance->cnt;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_ldv_v0_decoder = {
    .alloc = subghz_protocol_decoder_ldv_v0_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_ldv_v0_feed,
    .reset = subghz_protocol_decoder_ldv_v0_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_ldv_v0_serialize,
    .deserialize = subghz_protocol_decoder_ldv_v0_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_ldv_v0 = {
    .name = LDV_V0_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_ldv_v0_decoder,
    .encoder = NULL,
};
