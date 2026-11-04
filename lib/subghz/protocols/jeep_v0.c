#include "jeep_v0.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include "common.h"

#define TAG "JeepV0"

// Jeep V0 -- ported from firmware jeep_v0.c via KeeLoq-RX arke_misc.py (JeepV0).
// 200/400 us Manchester at 433.92 MHz ASK. INVERTED polarity: the duration->event
// mapping is level-flipped vs the standard Flipper Manchester decoder, and the
// whole demodulated frame is then bit-inverted. 96-bit frame (12 bytes):
//   serial(32) | button(16) | 0x07(8) | cnt(8) | hop(32)
// Gate (structural / fixed-field): byte6 == 0x07 AND the 16-bit button code is one
// of the four known values. No cleartext counter in the fixed part; `cnt` is the
// low byte of the fixed portion, hop is the 32-bit rolling tail. Decode-only
// (rolling code -> no clone); .encoder = NULL.

#define JEEP_V0_TE_SHORT    200
#define JEEP_V0_TE_LONG     400
#define JEEP_V0_TE_DELTA    100
#define JEEP_V0_PREAMBLE_MIN 16
#define JEEP_V0_DATA_BITS   96

static const SubGhzBlockConst subghz_protocol_jeep_v0_const = {
    .te_short = JEEP_V0_TE_SHORT,
    .te_long = JEEP_V0_TE_LONG,
    .te_delta = JEEP_V0_TE_DELTA,
    .min_count_bit_for_found = JEEP_V0_DATA_BITS,
};

struct SubGhzProtocolDecoderJeepV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    uint32_t te_last;
    uint16_t preamble_count;
    ManchesterState manchester_state;

    uint64_t data_lo; // first 64 demodulated bits (serial|btn|0x07|cnt), post-invert
    uint32_t data_32; // last 32 demodulated bits (hop), post-invert
    uint16_t bit_count;

    uint32_t serial;
    uint16_t btn_code;
    uint8_t b6;
    uint8_t cnt;
    uint32_t hop;
    bool frame_ok;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderJeepV0);


typedef enum {
    JeepV0StepReset = 0,
    JeepV0StepPreamble = 1,
    JeepV0StepData = 2,
} JeepV0Step;

static bool jeep_v0_button_known(uint16_t code) {
    return code == 0x8104 || code == 0x8044 || code == 0x8804 || code == 0x8024;
}

static void jeep_v0_finish(SubGhzProtocolDecoderJeepV0* instance) {
    // Whole frame is bit-inverted after demodulation (firmware behaviour).
    uint64_t data = ~instance->data_lo; // 64-bit logical fixed portion
    uint32_t hop = (uint32_t)(~instance->data_32);

    uint32_t serial = (uint32_t)(data >> 32);
    uint16_t btn_code = (uint16_t)((data >> 16) & 0xFFFFU);
    uint8_t b6 = (uint8_t)((data >> 8) & 0xFFU);
    uint8_t cnt = (uint8_t)(data & 0xFFU);

    bool frame_ok = (b6 == 0x07U) && jeep_v0_button_known(btn_code);

    // reset accumulator for next frame
    instance->parser_step = JeepV0StepReset;
    instance->preamble_count = 0;
    instance->data_lo = 0;
    instance->data_32 = 0;
    instance->bit_count = 0;

    if(!frame_ok) return;

    instance->serial = serial;
    instance->btn_code = btn_code;
    instance->b6 = b6;
    instance->cnt = cnt;
    instance->hop = hop;
    instance->frame_ok = true;

    instance->generic.data = data; // top-64 logical frame
    instance->generic.data_count_bit = JEEP_V0_DATA_BITS;
    instance->generic.serial = serial;
    instance->generic.btn = (uint8_t)(btn_code & 0xFFU);
    instance->generic.cnt = cnt;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = JEEP_V0_DATA_BITS;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_jeep_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderJeepV0* instance = calloc(1, sizeof(SubGhzProtocolDecoderJeepV0));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_jeep_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->manchester_state = ManchesterStateMid1;
    return instance;
}



void subghz_protocol_decoder_jeep_v0_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderJeepV0* instance = context;
    instance->parser_step = JeepV0StepReset;
    instance->te_last = 0;
    instance->preamble_count = 0;
    instance->manchester_state = ManchesterStateMid1;
    instance->data_lo = 0;
    instance->data_32 = 0;
    instance->bit_count = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_jeep_v0_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderJeepV0* instance = context;

    bool is_short = DURATION_DIFF(duration, JEEP_V0_TE_SHORT) <= JEEP_V0_TE_DELTA;
    bool is_long = DURATION_DIFF(duration, JEEP_V0_TE_LONG) <= JEEP_V0_TE_DELTA;

    switch(instance->parser_step) {
    case JeepV0StepReset:
        // NB: firmware does not inspect level here, only the short-timing gate.
        if(is_short) {
            instance->preamble_count = 1;
            instance->te_last = duration;
            instance->parser_step = JeepV0StepPreamble;
        }
        break;

    case JeepV0StepPreamble:
        if(is_short) {
            instance->preamble_count++;
        } else if(is_long && instance->preamble_count >= JEEP_V0_PREAMBLE_MIN) {
            // Prime Manchester with the long sync edge (INVERTED polarity).
            instance->manchester_state = ManchesterStateMid1;
            ManchesterEvent prime =
                level ? ManchesterEventLongHigh : ManchesterEventLongLow;
            bool tmp = false;
            manchester_advance(
                instance->manchester_state, prime, &instance->manchester_state, &tmp);
            instance->data_lo = 0;
            instance->data_32 = 0;
            instance->bit_count = 0;
            instance->parser_step = JeepV0StepData;
        } else {
            instance->parser_step = JeepV0StepReset;
            instance->preamble_count = 0;
        }
        break;

    case JeepV0StepData: {
        // INVERTED polarity event mapping (opposite of standard Manchester):
        //   short -> level ? ShortHigh : ShortLow
        //   long  -> level ? LongHigh  : LongLow
        ManchesterEvent event = ManchesterEventReset;
        if(is_short) {
            event = level ? ManchesterEventShortHigh : ManchesterEventShortLow;
        } else if(is_long) {
            event = level ? ManchesterEventLongHigh : ManchesterEventLongLow;
        }

        if(event == ManchesterEventReset) {
            instance->parser_step = JeepV0StepReset;
            instance->preamble_count = 0;
            break;
        }

        bool data_bit = false;
        if(manchester_advance(
               instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
            uint8_t b = data_bit ? 1 : 0;
            if(instance->bit_count < 64) {
                instance->data_lo = (instance->data_lo << 1) | b;
            } else {
                instance->data_32 = ((instance->data_32 << 1) | b) & 0xFFFFFFFFU;
            }
            instance->bit_count++;
            if(instance->bit_count == JEEP_V0_DATA_BITS) {
                jeep_v0_finish(instance);
            }
        }
        break;
    }
    default:
        break;
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_jeep_v0_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderJeepV0* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t serial = instance->serial;
    uint32_t btn = instance->btn_code;
    uint32_t cnt = instance->cnt;
    uint32_t hop = instance->hop;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "Btn", &btn, 1) ||
       !flipper_format_write_uint32(flipper_format, "Cnt", &cnt, 1) ||
       !flipper_format_write_uint32(flipper_format, "Hop", &hop, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_jeep_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderJeepV0* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_jeep_v0_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint64_t data = instance->generic.data;
    instance->serial = (uint32_t)(data >> 32);
    instance->btn_code = (uint16_t)((data >> 16) & 0xFFFFU);
    instance->b6 = (uint8_t)((data >> 8) & 0xFFU);
    instance->cnt = (uint8_t)(data & 0xFFU);

    uint32_t hop_u = 0;
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Hop", &hop_u, 1)) {
        instance->hop = hop_u;
    } else {
        instance->hop = 0;
    }
    instance->frame_ok = (instance->b6 == 0x07U) && jeep_v0_button_known(instance->btn_code);

    instance->generic.serial = instance->serial;
    instance->generic.btn = (uint8_t)(instance->btn_code & 0xFFU);
    instance->generic.cnt = instance->cnt;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_jeep_v0_decoder = {
    .alloc = subghz_protocol_decoder_jeep_v0_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_jeep_v0_feed,
    .reset = subghz_protocol_decoder_jeep_v0_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_jeep_v0_serialize,
    .deserialize = subghz_protocol_decoder_jeep_v0_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_jeep_v0 = {
    .name = JEEP_V0_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_jeep_v0_decoder,
    .encoder = NULL,
};
