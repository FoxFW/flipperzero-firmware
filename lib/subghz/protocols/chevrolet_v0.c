#include "chevrolet_v0.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include "common.h"

#define TAG "ChevroletV0"

// Chevrolet V0 -- ported from firmware chevrolet_v0.c via KeeLoq-RX arke_mazda_gm.py
// (ChevroletV0 / ChevroletV0Gm). Standard Flipper (IEEE-802.3) Manchester, ts=120
// tl=240 delta=71 (i.e. firmware's "<=70"). Framing: a 32-bit shift register locks
// on the EXACT preamble 0xAAAAAAAB, then 96 data bits are collected. EVERY decoded
// Manchester bit is inverted (IEEE-802.3). The top 64 of the 96 bits ("gdata") hold
// Serial(24) | Button(4) | Hop(36); the low 32 bits are the encrypted "seed". There
// is no cleartext counter (hop is fully encrypted) -> STRUCTURAL gate only:
//   * exact 0xAAAAAAAB preamble, and
//   * a non-degenerate 96-bit payload (gdata != 0 and != all-ones), and
//   * button nibble in {Open 0xA, Close 0xB}.
// Define CHEVROLET_V0_GM_ACCEPT_ANY to generalize to the whole GM 434 MHz remote
// family (Buick/Cadillac/GMC/newer Chevrolet), whose fobs carry a per-remote
// type/toggle nibble instead of Open/Close; that relaxes the button gate to
// accept-any (keeping the exact-preamble + non-degenerate checks). Decode-only
// (encrypted hop -> no clone); .encoder = NULL.

#define CHEVROLET_V0_DATA_BITS     96
#define CHEVROLET_V0_PREAMBLE_WORD 0xAAAAAAABUL
#define CHEVROLET_V0_PREAMBLE_MIN  32

static const SubGhzBlockConst subghz_protocol_chevrolet_v0_const = {
    .te_short = 120,
    .te_long = 240,
    .te_delta = 71, // firmware uses "diff < 71", i.e. <= 70
    .min_count_bit_for_found = CHEVROLET_V0_DATA_BITS,
};

struct SubGhzProtocolDecoderChevroletV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint32_t data_header; // 32-bit preamble / high shift register
    uint16_t bit_count; // preamble bit counter
    uint16_t data_count; // collected data bits

    uint32_t serial; // 24-bit
    uint8_t btn; // 4-bit
    uint64_t hop; // 36-bit
    uint32_t seed; // low 32 bits of payload
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderChevroletV0);


typedef enum {
    ChevroletV0StepReset = 0,
    ChevroletV0StepPreamble = 1,
    ChevroletV0StepCollectData = 2,
} ChevroletV0Step;

static bool chevrolet_v0_gate_ok(uint8_t btn) {
#ifdef CHEVROLET_V0_GM_ACCEPT_ANY
    UNUSED(btn);
    return true; // GM-family generalization: accept any non-degenerate frame
#else
    return (btn == 0xA) || (btn == 0xB); // faithful chevrolet_v0.c: Open/Close only
#endif
}

// Build the Manchester event exactly as the firmware/arke do.
static ManchesterEvent chevrolet_v0_event(bool level, uint32_t duration) {
    uint32_t ts = subghz_protocol_chevrolet_v0_const.te_short;
    uint32_t tl = subghz_protocol_chevrolet_v0_const.te_long;
    uint32_t d = subghz_protocol_chevrolet_v0_const.te_delta;
    if(DURATION_DIFF(duration, ts) < d) {
        return level ? ManchesterEventShortLow : ManchesterEventShortHigh;
    }
    if(DURATION_DIFF(duration, tl) < d) {
        return level ? ManchesterEventLongLow : ManchesterEventLongHigh;
    }
    return ManchesterEventReset;
}

void* subghz_protocol_decoder_chevrolet_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderChevroletV0* instance =
        calloc(1, sizeof(SubGhzProtocolDecoderChevroletV0));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_chevrolet_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->manchester_state = ManchesterStateMid1;
    return instance;
}



void subghz_protocol_decoder_chevrolet_v0_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderChevroletV0* instance = context;
    instance->decoder.parser_step = ChevroletV0StepReset;
    instance->manchester_state = ManchesterStateMid1;
    instance->data_header = 0;
    instance->bit_count = 0;
    instance->data_count = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_chevrolet_v0_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderChevroletV0* instance = context;

    ManchesterEvent event = chevrolet_v0_event(level, duration);
    uint8_t st = instance->decoder.parser_step;

    if(st == ChevroletV0StepReset) {
        if(event == ManchesterEventReset) return;
        instance->manchester_state = ManchesterStateMid1;
        instance->data_header = 0;
        instance->bit_count = 0;
        instance->decoder.parser_step = ChevroletV0StepPreamble;
        st = ChevroletV0StepPreamble; // fall through, process this event as preamble
    }

    if(st == ChevroletV0StepPreamble) {
        if(event == ManchesterEventReset) {
            instance->decoder.parser_step = ChevroletV0StepReset;
            return;
        }
        bool mbit = false;
        if(manchester_advance(
               instance->manchester_state, event, &instance->manchester_state, &mbit)) {
            uint8_t data = mbit ? 0 : 1; // invert (IEEE 802.3)
            instance->data_header = ((instance->data_header << 1) | data);
            instance->bit_count++;
            if(instance->bit_count >= CHEVROLET_V0_PREAMBLE_MIN &&
               instance->data_header == CHEVROLET_V0_PREAMBLE_WORD) {
                instance->data_header = 0;
                instance->decoder.decode_data = 0;
                instance->data_count = 0;
                instance->decoder.parser_step = ChevroletV0StepCollectData;
            }
        }
    } else if(st == ChevroletV0StepCollectData) {
        if(event == ManchesterEventReset) {
            instance->decoder.parser_step = ChevroletV0StepReset;
            return;
        }
        bool mbit = false;
        if(manchester_advance(
               instance->manchester_state, event, &instance->manchester_state, &mbit)) {
            uint8_t data = mbit ? 0 : 1; // invert
            instance->data_header =
                ((instance->data_header << 1) | ((instance->decoder.decode_data >> 63) & 1));
            instance->decoder.decode_data = (instance->decoder.decode_data << 1) | data;
            instance->data_count++;
            if(instance->data_count >= CHEVROLET_V0_DATA_BITS) {
                uint64_t gdata = ((uint64_t)instance->data_header << 32) |
                                 (instance->decoder.decode_data >> 32);
                uint32_t seed = (uint32_t)(instance->decoder.decode_data & 0xFFFFFFFFUL);
                uint32_t serial = (uint32_t)((gdata >> 40) & 0xFFFFFFUL);
                uint8_t btn = (uint8_t)((gdata >> 36) & 0x0FU);

                if(gdata == 0 || gdata == 0xFFFFFFFFFFFFFFFFULL ||
                   !chevrolet_v0_gate_ok(btn)) {
                    instance->decoder.parser_step = ChevroletV0StepReset;
                    return;
                }
                uint64_t hop = gdata & 0xFFFFFFFFFULL; // 36-bit encrypted hop

                instance->serial = serial;
                instance->btn = btn;
                instance->hop = hop;
                instance->seed = seed;

                instance->generic.data = gdata;
                instance->generic.data_count_bit = CHEVROLET_V0_DATA_BITS;
                instance->generic.serial = serial;
                instance->generic.btn = btn;
                instance->generic.cnt = 0; // hop encrypted, no cleartext counter
                instance->decoder.decode_count_bit = CHEVROLET_V0_DATA_BITS;

                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);

                instance->decoder.parser_step = ChevroletV0StepReset;
            }
        }
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_chevrolet_v0_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderChevroletV0* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t serial = instance->serial;
    uint32_t btn = instance->btn;
    uint32_t hop_hi = (uint32_t)(instance->hop >> 32); // 4 high bits of 36-bit hop
    uint32_t hop_lo = (uint32_t)(instance->hop & 0xFFFFFFFFUL);
    uint32_t seed = instance->seed;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "Btn", &btn, 1) ||
       !flipper_format_write_uint32(flipper_format, "HopHi", &hop_hi, 1) ||
       !flipper_format_write_uint32(flipper_format, "HopLo", &hop_lo, 1) ||
       !flipper_format_write_uint32(flipper_format, "Seed", &seed, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_chevrolet_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderChevroletV0* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_chevrolet_v0_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint64_t gdata = instance->generic.data;
    instance->serial = (uint32_t)((gdata >> 40) & 0xFFFFFFUL);
    instance->btn = (uint8_t)((gdata >> 36) & 0x0FU);
    instance->hop = gdata & 0xFFFFFFFFFULL;

    uint32_t seed_u = 0;
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Seed", &seed_u, 1)) {
        instance->seed = seed_u;
    } else {
        instance->seed = 0;
    }

    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->btn;
    instance->generic.cnt = 0;
    instance->decoder.decode_data = gdata;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_chevrolet_v0_decoder = {
    .alloc = subghz_protocol_decoder_chevrolet_v0_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_chevrolet_v0_feed,
    .reset = subghz_protocol_decoder_chevrolet_v0_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_chevrolet_v0_serialize,
    .deserialize = subghz_protocol_decoder_chevrolet_v0_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_chevrolet_v0 = {
    .name = CHEVROLET_V0_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_chevrolet_v0_decoder,
    .encoder = NULL,
};
