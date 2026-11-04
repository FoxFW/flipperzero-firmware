#include "honda_v7.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"

#define TAG "HondaV7"

// Honda V7 -- ported from firmware honda_v7.c via KeeLoq-RX arke_honda.py (class HondaV7).
// OOK PWM, te 250/480 us, te_delta 100. 88-bit frame, LSB-first per byte; each decoded
// byte is bit-reversed (rev8) to its "natural" form. A bit is one PWM symbol: a high+low
// pair both SHORT -> 0, both LONG -> 1. Fields (natural bytes):
//   button = nat[0]>>4 (4b), serial = (nat[0]&0xF)<<24 | nat[1..3] (28b),
//   counter = nat[4..6] (24b), mode = nat[7]>>4 (4b), checksum = nat[8].
// Integrity: XOR(nat[0..7]) == checksum (nat[8]). Genuine 8-bit XOR checksum gate.
// Decode-only (rolling counter encrypted upstream); .encoder = NULL.

#define HV7_TS          250
#define HV7_TL          480
#define HV7_TD          100
#define HV7_FRAME_BITS  88
#define HV7_MIN_BITS    64
#define HV7_MIN_PRE     20

static const SubGhzBlockConst subghz_protocol_honda_v7_const = {
    .te_short = HV7_TS,
    .te_long = HV7_TL,
    .te_delta = HV7_TD,
    .min_count_bit_for_found = HV7_FRAME_BITS,
};

struct SubGhzProtocolDecoderHondaV7 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    uint16_t pre;
    uint16_t bit_count;
    uint32_t last_high;
    uint8_t buf[16];

    uint32_t serial;
    uint8_t btn;
    uint32_t cnt;
    uint8_t mode;
    uint8_t crc;
    bool crc_ok;
    uint32_t tail; // natural[8..10] (24 bits incl checksum)
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderHondaV7);


typedef enum {
    HondaV7StepReset = 0,
    HondaV7StepPreamble = 1,
    HondaV7StepData = 2,
} HondaV7Step;

// rev8: reverse the 8 bits of a byte (matches arke_core.rev8).
static uint8_t honda_v7_rev8(uint8_t b) {
    uint8_t r = 0;
    for(uint8_t i = 0; i < 8; i++) {
        if(b & (1u << i)) r |= (uint8_t)(1u << (7 - i));
    }
    return r;
}

// classify duration: 1 short, 2 long, 0 invalid.
static uint8_t honda_v7_classify(uint32_t dur) {
    if(DURATION_DIFF(dur, HV7_TS) <= HV7_TD) return 1;
    if(DURATION_DIFF(dur, HV7_TL) <= HV7_TD) return 2;
    return 0;
}

static void honda_v7_reset_acc(SubGhzProtocolDecoderHondaV7* instance) {
    instance->parser_step = HondaV7StepReset;
    instance->pre = 0;
    instance->bit_count = 0;
    instance->last_high = 0;
    memset(instance->buf, 0, sizeof(instance->buf));
}

static void honda_v7_validate_emit(SubGhzProtocolDecoderHondaV7* instance) {
    if(instance->bit_count < HV7_FRAME_BITS) return;
    uint8_t nat[11];
    for(uint8_t i = 0; i < 11; i++) nat[i] = honda_v7_rev8(instance->buf[i]);

    uint8_t button = (nat[0] >> 4) & 0x0F;
    uint32_t serial = ((uint32_t)(nat[0] & 0x0F) << 24) | ((uint32_t)nat[1] << 16) |
                      ((uint32_t)nat[2] << 8) | nat[3];
    uint32_t counter = ((uint32_t)nat[4] << 16) | ((uint32_t)nat[5] << 8) | nat[6];
    uint8_t mode = (nat[7] >> 4) & 0x0F;
    uint8_t checksum = nat[8];

    if(button == 0 || button > 0x9) return;
    if(serial == 0 || serial == 0x0FFFFFFF) return;

    uint8_t xchk = 0;
    for(uint8_t i = 0; i < 8; i++) xchk ^= nat[i];
    if(xchk != checksum) return;

    uint64_t data = 0;
    for(uint8_t i = 0; i < 8; i++) data = (data << 8) | nat[i];

    instance->serial = serial;
    instance->btn = button;
    instance->cnt = counter;
    instance->mode = mode;
    instance->crc = checksum;
    instance->crc_ok = true;
    instance->tail = ((uint32_t)nat[8] << 16) | ((uint32_t)nat[9] << 8) | nat[10];

    instance->generic.data = data;
    instance->generic.data_count_bit = HV7_FRAME_BITS;
    instance->generic.serial = serial;
    instance->generic.btn = button;
    instance->generic.cnt = counter;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = HV7_FRAME_BITS;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_honda_v7_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHondaV7* instance = calloc(1, sizeof(SubGhzProtocolDecoderHondaV7));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_honda_v7;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_honda_v7_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV7* instance = context;
    honda_v7_reset_acc(instance);
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_honda_v7_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV7* instance = context;
    uint8_t c = honda_v7_classify(duration);

    switch(instance->parser_step) {
    case HondaV7StepReset:
        if(level && c == 1) {
            instance->last_high = duration;
            instance->pre = 0;
            instance->parser_step = HondaV7StepPreamble;
        }
        break;

    case HondaV7StepPreamble:
        if(!level) {
            if(honda_v7_classify(instance->last_high) == 1 && c == 1) {
                instance->pre++;
            } else if(c == 0) {
                if(instance->pre >= HV7_MIN_PRE) {
                    instance->bit_count = 0;
                    memset(instance->buf, 0, sizeof(instance->buf));
                    instance->parser_step = HondaV7StepData;
                } else {
                    honda_v7_reset_acc(instance);
                }
            } else {
                if(instance->pre >= HV7_MIN_PRE &&
                   honda_v7_classify(instance->last_high) == 2 && c == 2) {
                    instance->bit_count = 0;
                    memset(instance->buf, 0, sizeof(instance->buf));
                    instance->buf[0] |= 0x01;
                    instance->bit_count = 1;
                    instance->parser_step = HondaV7StepData;
                } else {
                    honda_v7_reset_acc(instance);
                }
            }
        } else {
            instance->last_high = duration;
        }
        break;

    case HondaV7StepData:
        if(level) {
            instance->last_high = duration;
        } else {
            uint8_t hc = honda_v7_classify(instance->last_high);
            if(hc == 1 && c == 1) {
                if(instance->bit_count < HV7_FRAME_BITS) instance->bit_count++;
            } else if(hc == 2 && c == 2) {
                if(instance->bit_count < HV7_FRAME_BITS) {
                    uint16_t bi = instance->bit_count >> 3;
                    uint8_t bit_i = instance->bit_count & 7;
                    if(bi < 16) instance->buf[bi] |= (uint8_t)(1u << bit_i);
                    instance->bit_count++;
                }
            } else {
                if(instance->bit_count >= HV7_MIN_BITS) honda_v7_validate_emit(instance);
                honda_v7_reset_acc(instance);
                return;
            }
            if(instance->bit_count >= HV7_FRAME_BITS) {
                honda_v7_validate_emit(instance);
                honda_v7_reset_acc(instance);
            }
        }
        break;

    default:
        break;
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_honda_v7_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV7* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t serial = instance->serial;
    uint32_t btn = instance->btn;
    uint32_t cnt = instance->cnt;
    uint32_t mode = instance->mode;
    uint32_t crc = instance->crc;
    uint32_t tail = instance->tail;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "Btn", &btn, 1) ||
       !flipper_format_write_uint32(flipper_format, "Cnt", &cnt, 1) ||
       !flipper_format_write_uint32(flipper_format, "Mode", &mode, 1) ||
       !flipper_format_write_uint32(flipper_format, "CRC", &crc, 1) ||
       !flipper_format_write_uint32(flipper_format, "Tail", &tail, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_honda_v7_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV7* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_honda_v7_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint64_t data = instance->generic.data;
    uint8_t nat[8];
    for(int i = 0; i < 8; i++) nat[i] = (uint8_t)(data >> (8 * (7 - i)));
    instance->btn = (nat[0] >> 4) & 0x0F;
    instance->serial = ((uint32_t)(nat[0] & 0x0F) << 24) | ((uint32_t)nat[1] << 16) |
                       ((uint32_t)nat[2] << 8) | nat[3];
    instance->cnt = ((uint32_t)nat[4] << 16) | ((uint32_t)nat[5] << 8) | nat[6];
    instance->mode = (nat[7] >> 4) & 0x0F;

    uint32_t tmp = 0;
    flipper_format_rewind(flipper_format);
    instance->crc = flipper_format_read_uint32(flipper_format, "CRC", &tmp, 1) ? (uint8_t)tmp : 0;
    flipper_format_rewind(flipper_format);
    instance->tail = flipper_format_read_uint32(flipper_format, "Tail", &tmp, 1) ? tmp : 0;

    uint8_t xchk = 0;
    for(int i = 0; i < 8; i++) xchk ^= nat[i];
    instance->crc_ok = (xchk == instance->crc);

    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->btn;
    instance->generic.cnt = instance->cnt;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_honda_v7_decoder = {
    .alloc = subghz_protocol_decoder_honda_v7_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_honda_v7_feed,
    .reset = subghz_protocol_decoder_honda_v7_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_honda_v7_serialize,
    .deserialize = subghz_protocol_decoder_honda_v7_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_honda_v7 = {
    .name = HONDA_V7_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_honda_v7_decoder,
    .encoder = NULL,
};
