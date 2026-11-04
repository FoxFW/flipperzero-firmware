#include "honda_v4.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"

#define TAG "HondaV4"

// Honda V4 -- ported from firmware honda_v4.c via KeeLoq-RX arke_honda.py
// (class HondaV4). PWM: the bit value is carried by the LOW pulse width
// (te_short 750 = 0, te_long 1100 = 1); each bit = one LOW + one clocking HIGH.
// 51-bit frame = serial(19) | button(8) | inv(8) | counter(16). Integrity is an
// inverse-byte invariant: data[23:16] == (0xFF - data[31:24]) (inv == ~button).
// Preamble = >=3 cycles of ~1500 us low/high pairs. Decode-only; .encoder = NULL.

#define HONDA_V4_TE_SHORT 750
#define HONDA_V4_TE_LONG  1100
#define HONDA_V4_TE_DELTA 200
#define HONDA_V4_PRE      1500
#define HONDA_V4_PRE_D    300
#define HONDA_V4_BITS     51
#define HONDA_V4_MIN_BITS 48

static const SubGhzBlockConst subghz_protocol_honda_v4_const = {
    .te_short = HONDA_V4_TE_SHORT,
    .te_long = HONDA_V4_TE_LONG,
    .te_delta = HONDA_V4_TE_DELTA,
    .min_count_bit_for_found = HONDA_V4_BITS,
};

struct SubGhzProtocolDecoderHondaV4 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    uint32_t pre;
    uint32_t te_last;
    uint64_t data;
    uint32_t cnt_bits;

    uint32_t serial;
    uint8_t btn;
    uint16_t cnt;
    bool crc_ok;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderHondaV4);


typedef enum {
    HondaV4StepReset = 0,
    HondaV4StepPreambleHigh = 1,
    HondaV4StepPreambleOrDataLow = 2,
    HondaV4StepDataHigh = 3,
    HondaV4StepDataLow = 4,
} HondaV4Step;

// Mirror HondaV4._validate + _emit integrity check.
static bool honda_v4_validate(uint64_t data, uint32_t nbits) {
    if(nbits < HONDA_V4_MIN_BITS) return false;
    uint8_t btn = (uint8_t)((data >> 24) & 0xFF);
    uint8_t inv = (uint8_t)((data >> 16) & 0xFF);
    return inv == (uint8_t)((0xFF - btn) & 0xFF);
}

static void honda_v4_emit(SubGhzProtocolDecoderHondaV4* inst, uint64_t data, uint32_t nbits) {
    if(!honda_v4_validate(data, nbits)) return;
    inst->serial = (uint32_t)((data >> 32) & 0x7FFFF);
    inst->btn = (uint8_t)((data >> 24) & 0xFF);
    inst->cnt = (uint16_t)(data & 0xFFFF);
    inst->crc_ok = true;

    inst->generic.data = data;
    inst->generic.data_count_bit = HONDA_V4_BITS;
    inst->generic.serial = inst->serial;
    inst->generic.btn = inst->btn;
    inst->generic.cnt = inst->cnt;
    inst->decoder.decode_data = data;
    inst->decoder.decode_count_bit = HONDA_V4_BITS;

    if(inst->base.callback) inst->base.callback(&inst->base, inst->base.context);
}

void* subghz_protocol_decoder_honda_v4_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHondaV4* instance = calloc(1, sizeof(SubGhzProtocolDecoderHondaV4));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_honda_v4;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_honda_v4_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV4* instance = context;
    instance->parser_step = HondaV4StepReset;
    instance->pre = 0;
    instance->te_last = 0;
    instance->data = 0;
    instance->cnt_bits = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_honda_v4_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV4* instance = context;
    uint32_t ts = HONDA_V4_TE_SHORT;
    uint32_t tl = HONDA_V4_TE_LONG;
    uint32_t td = HONDA_V4_TE_DELTA;

    bool is_pre = DURATION_DIFF(duration, HONDA_V4_PRE) < HONDA_V4_PRE_D;
    bool is_s = DURATION_DIFF(duration, ts) < td;
    bool is_l = DURATION_DIFF(duration, tl) < td;

    switch(instance->parser_step) {
    case HondaV4StepReset:
        if((!level) && is_pre) {
            instance->pre = 0;
            instance->data = 0;
            instance->cnt_bits = 0;
            instance->parser_step = HondaV4StepPreambleHigh;
        }
        break;

    case HondaV4StepPreambleHigh:
        if(level && is_pre) {
            instance->pre++;
            instance->parser_step = HondaV4StepPreambleOrDataLow;
        } else {
            instance->parser_step = HondaV4StepReset;
        }
        break;

    case HondaV4StepPreambleOrDataLow:
        if(!level) {
            if(is_pre) {
                instance->parser_step = HondaV4StepPreambleHigh;
            } else if(instance->pre >= 3 && (is_s || is_l)) {
                instance->te_last = duration;
                instance->data = 0;
                instance->cnt_bits = 0;
                instance->parser_step = HondaV4StepDataHigh;
            } else {
                instance->parser_step = HondaV4StepReset;
            }
        } else {
            instance->parser_step = HondaV4StepReset;
        }
        break;

    case HondaV4StepDataHigh:
        if(level) {
            uint8_t bit = (DURATION_DIFF(instance->te_last, tl) < td) ? 1 : 0;
            instance->data = (instance->data << 1) | bit;
            instance->cnt_bits++;
            if(instance->cnt_bits >= HONDA_V4_BITS) {
                honda_v4_emit(instance, instance->data, instance->cnt_bits);
                instance->parser_step = HondaV4StepReset;
            } else {
                instance->parser_step = HondaV4StepDataLow;
            }
        } else {
            instance->parser_step = HondaV4StepReset;
        }
        break;

    case HondaV4StepDataLow:
        if(!level) {
            if(is_s || is_l) {
                instance->te_last = duration;
                instance->parser_step = HondaV4StepDataHigh;
            } else if(is_pre) {
                honda_v4_emit(instance, instance->data, instance->cnt_bits);
                instance->pre = 0;
                instance->data = 0;
                instance->cnt_bits = 0;
                instance->parser_step = HondaV4StepPreambleHigh;
            } else {
                honda_v4_emit(instance, instance->data, instance->cnt_bits);
                instance->parser_step = HondaV4StepReset;
            }
        } else {
            instance->parser_step = HondaV4StepReset;
        }
        break;

    default:
        break;
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_honda_v4_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV4* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t serial = instance->serial;
    uint32_t btn = instance->btn;
    uint32_t cnt = instance->cnt;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "Btn", &btn, 1) ||
       !flipper_format_write_uint32(flipper_format, "Cnt", &cnt, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_honda_v4_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV4* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_honda_v4_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint64_t data = instance->generic.data;
    instance->serial = (uint32_t)((data >> 32) & 0x7FFFF);
    instance->btn = (uint8_t)((data >> 24) & 0xFF);
    instance->cnt = (uint16_t)(data & 0xFFFF);
    instance->crc_ok = honda_v4_validate(data, instance->generic.data_count_bit);

    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->btn;
    instance->generic.cnt = instance->cnt;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_honda_v4_decoder = {
    .alloc = subghz_protocol_decoder_honda_v4_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_honda_v4_feed,
    .reset = subghz_protocol_decoder_honda_v4_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_honda_v4_serialize,
    .deserialize = subghz_protocol_decoder_honda_v4_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_honda_v4 = {
    .name = HONDA_V4_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_honda_v4_decoder,
    .encoder = NULL,
};
