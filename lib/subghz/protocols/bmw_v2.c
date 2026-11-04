#include "bmw_v2.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"

#define TAG "BmwV2"

// BMW V2 -- ported from firmware bmw_v2.c via KeeLoq-RX arke_mitsu_bmw.py.
// OOK/AM Manchester, te 500/1000 us, delta 150, inter-frame gap > 1700 us.
// 56-bit frame: [header8 = 0xCF][rolling40][trailer8 = 0x3A].
// te_last half-period accumulator (a LONG = one bit; two SHORTs = one bit; the
// bit value is the INVERSE of the completing pulse level). Header + trailer are
// FIXED FRAMING BYTES, NOT a checksum -- they gate the frame but there is no CRC
// (crc_ok is never asserted). The "serial" is carved from the ENCRYPTED rolling
// code, so it changes every press and is not a stable identifier; it is surfaced
// only to mirror the firmware. Decode-only; .encoder = NULL.

#define BMW_V2_DATA_BITS 56
#define BMW_V2_GAP_MIN 1700
#define BMW_V2_HEADER 0xCFU
#define BMW_V2_TRAILER 0x3AU

static const SubGhzBlockConst subghz_protocol_bmw_v2_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 150,
    .min_count_bit_for_found = BMW_V2_DATA_BITS,
};

struct SubGhzProtocolDecoderBmwV2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder; // parser_step / te_last / decode_data / decode_count_bit
    SubGhzBlockGeneric generic;

    uint8_t header;
    uint8_t trailer;
    uint32_t serial; // 24-bit, carved from the encrypted rolling code
    uint16_t cnt; // 16-bit, low part of the rolling code
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderBmwV2);


typedef enum {
    BmwV2StepReset = 0,
    BmwV2StepWaitGap = 1,
    BmwV2StepManchester = 2,
} BmwV2Step;

static const uint32_t BMW_V2_DEGENERATE[4] = {0x000000U, 0xFFFFFFU, 0x555555U, 0xAAAAAAU};

static bool bmw_v2_is_degenerate(uint32_t serial) {
    for(int i = 0; i < 4; i++)
        if(serial == BMW_V2_DEGENERATE[i]) return true;
    return false;
}

// Mirror of arke BmwV2._try_emit: require >= 56 bits, right-align to 56, gate on
// the fixed 0xCF header + 0x3A trailer, carve the serial/cnt, reject degenerates.
static void bmw_v2_try_emit(SubGhzProtocolDecoderBmwV2* instance) {
    if(instance->decoder.decode_count_bit < BMW_V2_DATA_BITS) return;
    uint64_t data = instance->decoder.decode_data;
    if(instance->decoder.decode_count_bit > BMW_V2_DATA_BITS) {
        data >>= (instance->decoder.decode_count_bit - BMW_V2_DATA_BITS);
    }
    uint8_t header = (uint8_t)((data >> 48) & 0xFFU);
    uint8_t trailer = (uint8_t)(data & 0xFFU);
    if(header != BMW_V2_HEADER || trailer != BMW_V2_TRAILER) return;

    uint64_t rolling = (data >> 8) & 0xFFFFFFFFFFULL; // 40-bit encrypted payload
    uint32_t serial = (uint32_t)((rolling >> 16) & 0xFFFFFFU); // upper 24 bits
    uint16_t cnt = (uint16_t)(rolling & 0xFFFFU); // lower 16 bits
    if(bmw_v2_is_degenerate(serial)) return;

    instance->header = header;
    instance->trailer = trailer;
    instance->serial = serial;
    instance->cnt = cnt;

    instance->generic.data = data & 0x00FFFFFFFFFFFFFFULL; // 56-bit frame
    instance->generic.data_count_bit = BMW_V2_DATA_BITS;
    instance->generic.serial = serial;
    instance->generic.btn = 0;
    instance->generic.cnt = cnt;
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = BMW_V2_DATA_BITS;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_bmw_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderBmwV2* instance = calloc(1, sizeof(SubGhzProtocolDecoderBmwV2));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_bmw_v2;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_bmw_v2_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderBmwV2* instance = context;
    instance->decoder.parser_step = BmwV2StepReset;
    instance->decoder.te_last = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_bmw_v2_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderBmwV2* instance = context;
    uint32_t te_s = subghz_protocol_bmw_v2_const.te_short;
    uint32_t te_l = subghz_protocol_bmw_v2_const.te_long;
    uint32_t te_d = subghz_protocol_bmw_v2_const.te_delta;

    switch(instance->decoder.parser_step) {
    case BmwV2StepReset:
        if((!level) && duration > BMW_V2_GAP_MIN) {
            instance->decoder.parser_step = BmwV2StepWaitGap;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.te_last = 0;
        }
        return;

    case BmwV2StepWaitGap:
        if(level) {
            bool is_short = DURATION_DIFF(duration, te_s) <= te_d;
            bool is_long = DURATION_DIFF(duration, te_l) <= te_d;
            if(is_short || is_long) {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = BmwV2StepManchester;
            } else {
                instance->decoder.parser_step = BmwV2StepReset;
            }
        } else {
            if(duration > BMW_V2_GAP_MIN) {
                // another gap, keep waiting
            } else {
                instance->decoder.parser_step = BmwV2StepReset;
            }
        }
        return;

    case BmwV2StepManchester: {
        bool is_short = DURATION_DIFF(duration, te_s) <= te_d;
        bool is_long = DURATION_DIFF(duration, te_l) <= te_d;
        if(!is_short && !is_long) {
            bmw_v2_try_emit(instance);
            instance->decoder.parser_step = BmwV2StepReset;
            if((!level) && duration > BMW_V2_GAP_MIN) {
                instance->decoder.parser_step = BmwV2StepWaitGap;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.te_last = 0;
            }
            return;
        }
        if(is_short) {
            if(instance->decoder.te_last == 0) {
                instance->decoder.te_last = duration;
            } else {
                uint8_t bit = level ? 0 : 1;
                instance->decoder.decode_data = (instance->decoder.decode_data << 1) | bit;
                instance->decoder.decode_count_bit++;
                instance->decoder.te_last = 0;
            }
        } else { // is_long
            uint8_t bit = level ? 0 : 1;
            instance->decoder.decode_data = (instance->decoder.decode_data << 1) | bit;
            instance->decoder.decode_count_bit++;
            if(instance->decoder.te_last != 0) {
                instance->decoder.te_last = te_s; // S+L: third half stays pending
            } else {
                instance->decoder.te_last = 0;
            }
        }
        if(instance->decoder.decode_count_bit > 64) {
            bmw_v2_try_emit(instance);
            instance->decoder.parser_step = BmwV2StepReset;
        }
        return;
    }
    default:
        return;
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_bmw_v2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderBmwV2* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t serial = instance->serial;
    uint32_t cnt = instance->cnt;
    uint32_t header = instance->header;
    uint32_t trailer = instance->trailer;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "Cnt", &cnt, 1) ||
       !flipper_format_write_uint32(flipper_format, "Hdr", &header, 1) ||
       !flipper_format_write_uint32(flipper_format, "Trl", &trailer, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_bmw_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderBmwV2* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_bmw_v2_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint64_t data = instance->generic.data;
    instance->header = (uint8_t)((data >> 48) & 0xFFU);
    instance->trailer = (uint8_t)(data & 0xFFU);
    uint64_t rolling = (data >> 8) & 0xFFFFFFFFFFULL;
    instance->serial = (uint32_t)((rolling >> 16) & 0xFFFFFFU);
    instance->cnt = (uint16_t)(rolling & 0xFFFFU);

    instance->generic.serial = instance->serial;
    instance->generic.btn = 0;
    instance->generic.cnt = instance->cnt;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_bmw_v2_decoder = {
    .alloc = subghz_protocol_decoder_bmw_v2_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_bmw_v2_feed,
    .reset = subghz_protocol_decoder_bmw_v2_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_bmw_v2_serialize,
    .deserialize = subghz_protocol_decoder_bmw_v2_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_bmw_v2 = {
    .name = BMW_V2_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    // OOK/AM -> no FM flag.
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_bmw_v2_decoder,
    .encoder = NULL,
};
