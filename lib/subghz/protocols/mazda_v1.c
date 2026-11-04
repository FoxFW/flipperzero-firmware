#include "mazda_v1.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include <lib/subghz/blocks/bitbuf.h>

#define TAG "MazdaV1"

// Mazda V1 -- ported from firmware mazda_v1.c via KeeLoq-RX arke_mazda_gm.py (MazdaV1).
// Half-bit Manchester, ts=505 tl=1010 delta=200 (<=). 71-bit frame, usually sent
// twice back-to-back. 8-bit 0xEC sync, then 8-bit button, serial, counter and two
// "format" markers (v1 / v3). No in-frame CRC -> STRUCTURAL gate:
//   * 0xEC sync (tried at both polarities),
//   * button whitelist {Lock 0x06, Unlock 0x0A, Trunk 0x0E, Btn5/Trunk 0x26} on &0x7F,
//   * bit70 == 1 marker,
//   * v1 marker (b18=1,b19=0,b20=0,b21=0,b37=0) OR v3 marker (bits18..21 != 1000).
// Serial is a 6-bit per-remote constant; cnt is a 5-bit cleartext field. The top 64
// bits are stored in generic.data; the 7 tail bits (frame bits 64..70) in `tail`.
// Decode-only (rolling code); .encoder = NULL.

#define MAZDA_V1_BITS       71
#define MAZDA_V1_CAP        400
#define MAZDA_V1_PROCESS_AT 380

static const SubGhzBlockConst subghz_protocol_mazda_v1_const = {
    .te_short = 505,
    .te_long = 1010,
    .te_delta = 200,
    .min_count_bit_for_found = MAZDA_V1_BITS,
};

// 0xEC = 1110 1100, MSB first.
static const uint8_t mazda_v1_sync[8] = {1, 1, 1, 0, 1, 1, 0, 0};

struct SubGhzProtocolDecoderMazdaV1 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    bool half_bit;
    uint16_t all_count;
    int32_t last_pos;
    uint8_t all_bits[SUBGHZ_BITBUF_BYTES(MAZDA_V1_CAP)];

    uint8_t serial; // 6-bit
    uint8_t btn; // 8-bit
    uint8_t cnt; // 5-bit
    uint8_t tail; // frame bits 64..70 (7-bit remainder)
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderMazdaV1);


typedef enum {
    MazdaV1StepReset = 0,
    MazdaV1StepCollect = 1,
} MazdaV1Step;

// cnt = 5-bit cleartext field derived from frame bits 49..53 (MSB-indexed within
// the 64-bit `data`): b4 = fb53 ^ fb49.
static uint8_t mazda_v1_extract_cnt(uint64_t data) {
    uint8_t fb53 = (data >> (63 - 53)) & 1;
    uint8_t fb52 = (data >> (63 - 52)) & 1;
    uint8_t fb51 = (data >> (63 - 51)) & 1;
    uint8_t fb50 = (data >> (63 - 50)) & 1;
    uint8_t fb49 = (data >> (63 - 49)) & 1;
    uint8_t b4 = fb53 ^ fb49;
    return (uint8_t)((b4 << 4) | (fb50 << 3) | (fb51 << 2) | (fb52 << 1) | fb53);
}

// Try to lock a 71-bit frame at all_bits[sync_pos] under the given polarity (0/1).
// On success, normalizes the window in place (if polarity==1), extracts fields and
// fires the callback. Returns true on a successful emit.
static bool mazda_v1_try_emit_at(
    SubGhzProtocolDecoderMazdaV1* instance,
    uint16_t sync_pos,
    uint8_t polarity) {
    uint8_t* ab = instance->all_bits;
    uint16_t n = instance->all_count;
    if((uint32_t)sync_pos + MAZDA_V1_BITS > n) return false;

    for(uint8_t j = 0; j < 8; j++) {
        uint8_t b = subghz_bitbuf_get(ab, sync_pos + j) ^ polarity;
        if(b != mazda_v1_sync[j]) return false;
    }

    uint8_t btn = 0;
    for(uint8_t j = 0; j < 8; j++) {
        uint8_t b = subghz_bitbuf_get(ab, sync_pos + 8 + j) ^ polarity;
        btn = (uint8_t)((btn << 1) | b);
    }
    uint8_t btn7 = btn & 0x7F;
    if(btn7 != 0x06 && btn7 != 0x0A && btn7 != 0x0E && btn7 != 0x26) return false;

    uint8_t bit70 = subghz_bitbuf_get(ab, sync_pos + 70) ^ polarity;
    if(bit70 != 1) return false;

    uint8_t b18 = subghz_bitbuf_get(ab, sync_pos + 18) ^ polarity;
    uint8_t b19 = subghz_bitbuf_get(ab, sync_pos + 19) ^ polarity;
    uint8_t b20 = subghz_bitbuf_get(ab, sync_pos + 20) ^ polarity;
    uint8_t b21 = subghz_bitbuf_get(ab, sync_pos + 21) ^ polarity;
    uint8_t b37 = subghz_bitbuf_get(ab, sync_pos + 37) ^ polarity;
    bool v1_marker = (b18 == 1 && b19 == 0 && b20 == 0 && b21 == 0 && b37 == 0);
    bool v3_marker = !(b18 == 1 && b19 == 0 && b20 == 0 && b21 == 0);
    if(!v1_marker && !v3_marker) return false;

    // passed the gate: normalize polarity in place, then extract
    if(polarity == 1) {
        for(uint16_t k = sync_pos; k < sync_pos + MAZDA_V1_BITS; k++) subghz_bitbuf_set(ab, k, subghz_bitbuf_get(ab, k) ^ (1));
    }

    uint64_t data = 0;
    for(uint8_t i = 0; i < 64; i++) {
        data = (data << 1) | (subghz_bitbuf_get(ab, sync_pos + i) & 1);
    }
    uint8_t tail = 0;
    for(uint8_t i = 64; i < MAZDA_V1_BITS; i++) {
        tail = (uint8_t)((tail << 1) | (subghz_bitbuf_get(ab, sync_pos + i) & 1));
    }

    uint8_t btn_out = (uint8_t)(data >> 48);
    uint8_t b2 = (uint8_t)(data >> 40);
    uint8_t serial = (uint8_t)((b2 >> 2) & 0x3F);
    uint8_t cnt = mazda_v1_extract_cnt(data);

    instance->serial = serial;
    instance->btn = btn_out;
    instance->cnt = cnt;
    instance->tail = tail;

    instance->generic.data = data;
    instance->generic.data_count_bit = MAZDA_V1_BITS;
    instance->generic.serial = serial;
    instance->generic.btn = btn_out;
    instance->generic.cnt = cnt;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = MAZDA_V1_BITS;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
    return true;
}

// Check only the newest 71-bit window as bits stream in.
static void mazda_v1_streaming_check(SubGhzProtocolDecoderMazdaV1* instance) {
    uint16_t n = instance->all_count;
    if(n < MAZDA_V1_BITS) return;
    uint16_t sync_pos = (uint16_t)(n - MAZDA_V1_BITS);
    if(instance->last_pos >= 0 && sync_pos < instance->last_pos + 2 * MAZDA_V1_BITS) return;
    for(uint8_t polarity = 0; polarity <= 1; polarity++) {
        if(mazda_v1_try_emit_at(instance, sync_pos, polarity)) {
            instance->last_pos = (int32_t)sync_pos;
            return;
        }
    }
}

// Full scan over the whole buffer (end-of-burst / buffer-full fallback).
static void mazda_v1_process_frame(SubGhzProtocolDecoderMazdaV1* instance) {
    uint16_t n = instance->all_count;
    if(n < MAZDA_V1_BITS) return;
    uint16_t search_end = (uint16_t)(n - MAZDA_V1_BITS);
    for(uint16_t i = 0; i <= search_end; i++) {
        if(instance->last_pos >= 0 && i < instance->last_pos + 2 * MAZDA_V1_BITS) continue;
        for(uint8_t polarity = 0; polarity <= 1; polarity++) {
            if(mazda_v1_try_emit_at(instance, i, polarity)) {
                instance->last_pos = (int32_t)i;
                break;
            }
        }
    }
}

void* subghz_protocol_decoder_mazda_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMazdaV1* instance = calloc(1, sizeof(SubGhzProtocolDecoderMazdaV1));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_mazda_v1;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->last_pos = -32767;
    return instance;
}



void subghz_protocol_decoder_mazda_v1_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaV1* instance = context;
    instance->parser_step = MazdaV1StepReset;
    instance->half_bit = false;
    instance->all_count = 0;
    instance->last_pos = -32767;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_mazda_v1_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaV1* instance = context;

    uint32_t ts = subghz_protocol_mazda_v1_const.te_short;
    uint32_t tl = subghz_protocol_mazda_v1_const.te_long;
    uint32_t d = subghz_protocol_mazda_v1_const.te_delta;
    bool is_short = DURATION_DIFF(duration, ts) <= d;
    bool is_long = DURATION_DIFF(duration, tl) <= d;

    switch(instance->parser_step) {
    case MazdaV1StepReset:
        if(is_short) {
            instance->parser_step = MazdaV1StepCollect;
            instance->all_count = 0;
            instance->last_pos = -32767;
            instance->half_bit = true;
        }
        break;

    case MazdaV1StepCollect: {
        if(instance->all_count >= MAZDA_V1_PROCESS_AT) {
            mazda_v1_process_frame(instance);
            instance->parser_step = MazdaV1StepReset;
            return;
        }
        bool bit_added = false;
        if(is_long) {
            if(instance->all_count < MAZDA_V1_CAP) {
                subghz_bitbuf_set(instance->all_bits, instance->all_count++, level ? 1 : 0);
                bit_added = true;
            }
            instance->half_bit = false;
        } else if(is_short) {
            if(instance->half_bit) {
                if(instance->all_count < MAZDA_V1_CAP) {
                    subghz_bitbuf_set(instance->all_bits, instance->all_count++, level ? 1 : 0);
                    bit_added = true;
                }
                instance->half_bit = false;
            } else {
                instance->half_bit = true;
            }
        } else {
            mazda_v1_process_frame(instance);
            instance->parser_step = MazdaV1StepReset;
        }
        if(bit_added) mazda_v1_streaming_check(instance);
        break;
    }

    default:
        break;
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_mazda_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaV1* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t serial = instance->serial;
    uint32_t btn = instance->btn;
    uint32_t cnt = instance->cnt;
    uint32_t tail = instance->tail;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "Btn", &btn, 1) ||
       !flipper_format_write_uint32(flipper_format, "Cnt", &cnt, 1) ||
       !flipper_format_write_uint32(flipper_format, "Tail", &tail, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_mazda_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaV1* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_mazda_v1_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint64_t data = instance->generic.data;
    instance->btn = (uint8_t)(data >> 48);
    instance->serial = (uint8_t)(((uint8_t)(data >> 40) >> 2) & 0x3F);
    instance->cnt = mazda_v1_extract_cnt(data);

    uint32_t tail_u = 0;
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Tail", &tail_u, 1)) {
        instance->tail = (uint8_t)(tail_u & 0x7F);
    } else {
        instance->tail = 0;
    }

    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->btn;
    instance->generic.cnt = instance->cnt;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_mazda_v1_decoder = {
    .alloc = subghz_protocol_decoder_mazda_v1_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_mazda_v1_feed,
    .reset = subghz_protocol_decoder_mazda_v1_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_mazda_v1_serialize,
    .deserialize = subghz_protocol_decoder_mazda_v1_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_mazda_v1 = {
    .name = MAZDA_V1_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_mazda_v1_decoder,
    .encoder = NULL,
};
