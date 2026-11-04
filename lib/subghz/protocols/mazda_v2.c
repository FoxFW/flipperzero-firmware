#include "mazda_v2.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include <lib/subghz/blocks/bitbuf.h>

#define TAG "MazdaV2"

// Mazda V2 -- ported from firmware mazda_v2.c via KeeLoq-RX arke_mazda_gm.py (MazdaV2).
// Half-bit Manchester, ts=500 tl=1000 delta=150 (<=). Durations < 250us are a noise
// filter and dropped. Frame = 8-bit 0xEC sync + 63 data bits. The 63 data bits are
// left-shifted one (LSB padded 0) into a 64-bit word. No in-frame CRC -> STRUCTURAL
// gate: hop0 top-2 bits == 01, hop3 bit2 == 0, and button-whitelist {Lock/Unlock/
// Trunk}. Serial is an 8-bit per-remote constant; cnt is a cleartext 8-bit field
// spread across hop2/hop3. Decode-only (rolling code); .encoder = NULL.

#define MAZDA_V2_SYNC_BITS   8
#define MAZDA_V2_DATA_BITS   63
#define MAZDA_V2_FRAME_BITS  (MAZDA_V2_SYNC_BITS + MAZDA_V2_DATA_BITS) // 71
#define MAZDA_V2_CAP         400
#define MAZDA_V2_PROCESS_AT  380
#define MAZDA_V2_NOISE_MIN   250

static const SubGhzBlockConst subghz_protocol_mazda_v2_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 150,
    .min_count_bit_for_found = MAZDA_V2_DATA_BITS,
};

// 0xEC = 1110 1100, MSB first.
static const uint8_t mazda_v2_sync[MAZDA_V2_SYNC_BITS] = {1, 1, 1, 0, 1, 1, 0, 0};

struct SubGhzProtocolDecoderMazdaV2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    bool half_bit;
    uint16_t all_count;
    uint8_t all_bits[SUBGHZ_BITBUF_BYTES(MAZDA_V2_CAP)];

    uint8_t serial;
    uint8_t btn;
    uint8_t cnt;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderMazdaV2);


typedef enum {
    MazdaV2StepReset = 0,
    MazdaV2StepCollect = 1,
} MazdaV2Step;

// Scan the collected half-bit stream for the 0xEC sync, then validate + extract.
static void mazda_v2_process_frame(SubGhzProtocolDecoderMazdaV2* instance) {
    uint16_t n = instance->all_count;
    if(n < MAZDA_V2_FRAME_BITS) return;

    int sync_pos = -1;
    uint16_t search_end = (uint16_t)(n - MAZDA_V2_FRAME_BITS);
    for(uint16_t i = 0; i <= search_end; i++) {
        bool match = true;
        for(uint8_t j = 0; j < MAZDA_V2_SYNC_BITS; j++) {
            if(subghz_bitbuf_get(instance->all_bits, i + j) != mazda_v2_sync[j]) {
                match = false;
                break;
            }
        }
        if(match) {
            sync_pos = (int)i;
            break;
        }
    }
    if(sync_pos < 0) return;

    uint16_t data_start = (uint16_t)(sync_pos + MAZDA_V2_SYNC_BITS);
    if((uint16_t)(n - data_start) < MAZDA_V2_DATA_BITS) return;

    uint64_t data = 0;
    for(uint8_t i = 0; i < MAZDA_V2_DATA_BITS; i++) {
        data = (data << 1) | (subghz_bitbuf_get(instance->all_bits, data_start + i) & 1);
    }
    data = (data << 1); // pad LSB with 0 to fill 64 bits

    uint8_t hop0 = (uint8_t)(data >> 40);
    if((hop0 & 0xC0) != 0x40) return; // top-2 bits must be 01
    uint8_t hop3 = (uint8_t)(data >> 16);
    if((hop3 & 0x04) != 0) return; // bit2 must be 0
    uint8_t btn = (uint8_t)(data >> 56);
    uint8_t btn7 = btn & 0x7F;
    if(btn7 != 0x06 && btn7 != 0x0A && btn7 != 0x0E) return; // button whitelist

    uint8_t serial = (uint8_t)(data >> 48);
    uint8_t hop2 = (uint8_t)(data >> 24);
    uint8_t cnt_lo = hop3 >> 3;
    uint8_t cnt_hi = hop2 & 0x07;
    uint8_t cnt = (uint8_t)((cnt_hi << 5) | cnt_lo);

    instance->serial = serial;
    instance->btn = btn;
    instance->cnt = cnt;

    instance->generic.data = data;
    instance->generic.data_count_bit = MAZDA_V2_DATA_BITS;
    instance->generic.serial = serial;
    instance->generic.btn = btn;
    instance->generic.cnt = cnt;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = MAZDA_V2_DATA_BITS;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_mazda_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMazdaV2* instance = calloc(1, sizeof(SubGhzProtocolDecoderMazdaV2));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_mazda_v2;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_mazda_v2_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaV2* instance = context;
    instance->parser_step = MazdaV2StepReset;
    instance->half_bit = false;
    instance->all_count = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_mazda_v2_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaV2* instance = context;

    if(duration < MAZDA_V2_NOISE_MIN) return; // noise filter

    uint32_t ts = subghz_protocol_mazda_v2_const.te_short;
    uint32_t tl = subghz_protocol_mazda_v2_const.te_long;
    uint32_t d = subghz_protocol_mazda_v2_const.te_delta;
    bool is_short = DURATION_DIFF(duration, ts) <= d;
    bool is_long = DURATION_DIFF(duration, tl) <= d;

    switch(instance->parser_step) {
    case MazdaV2StepReset:
        if(is_short) {
            instance->parser_step = MazdaV2StepCollect;
            instance->all_count = 0;
            instance->half_bit = true;
        }
        break;

    case MazdaV2StepCollect:
        if(instance->all_count >= MAZDA_V2_PROCESS_AT) {
            mazda_v2_process_frame(instance);
            instance->parser_step = MazdaV2StepReset;
            return;
        }
        if(is_long) {
            if(instance->all_count < MAZDA_V2_CAP)
                subghz_bitbuf_set(instance->all_bits, instance->all_count++, level ? 1 : 0);
            instance->half_bit = false;
        } else if(is_short) {
            if(instance->half_bit) {
                if(instance->all_count < MAZDA_V2_CAP)
                    subghz_bitbuf_set(instance->all_bits, instance->all_count++, level ? 1 : 0);
                instance->half_bit = false;
            } else {
                instance->half_bit = true;
            }
        } else {
            mazda_v2_process_frame(instance);
            instance->parser_step = MazdaV2StepReset;
        }
        break;

    default:
        break;
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_mazda_v2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaV2* instance = context;
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
    subghz_protocol_decoder_mazda_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaV2* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_mazda_v2_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint64_t data = instance->generic.data;
    instance->btn = (uint8_t)(data >> 56);
    instance->serial = (uint8_t)(data >> 48);
    uint8_t hop2 = (uint8_t)(data >> 24);
    uint8_t hop3 = (uint8_t)(data >> 16);
    instance->cnt = (uint8_t)(((hop2 & 0x07) << 5) | (hop3 >> 3));

    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->btn;
    instance->generic.cnt = instance->cnt;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_mazda_v2_decoder = {
    .alloc = subghz_protocol_decoder_mazda_v2_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_mazda_v2_feed,
    .reset = subghz_protocol_decoder_mazda_v2_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_mazda_v2_serialize,
    .deserialize = subghz_protocol_decoder_mazda_v2_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_mazda_v2 = {
    .name = MAZDA_V2_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_mazda_v2_decoder,
    .encoder = NULL,
};
