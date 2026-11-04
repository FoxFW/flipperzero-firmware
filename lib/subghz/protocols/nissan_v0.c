#include "nissan_v0.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include <lib/subghz/blocks/bitbuf.h>

#define TAG "NissanV0"

// Nissan V0 -- ported from firmware nissan_v0.c via KeeLoq-RX arke_vag.py (class
// NissanV0). OOK/ASK, Manchester te_short=500 / te_long=1000. The demod is run-length
// based: each pulse is expanded into 1..4 half-bit samples (duration / ~500us), the
// half-bit stream is paired into bits by a stateless differential Manchester (differing
// pair -> bit, equal pair -> slip), and the decoder scans for a 6-bit sync '110001'
// with a 6-bit marker '000010' at +48. On a match a 64-bit payload is taken from the
// sync offset. Payload encrypted -> structural lock only (no CRC), with same-frame
// de-dup. Decode-only; .encoder = NULL.

#define NISSAN_V0_DATA_BITS 64
#define NISSAN_V0_HB_MAX    512
#define NISSAN_V0_HB_CAP    380
#define NISSAN_V0_HB_MIN    140
#define NISSAN_V0_BITS_MAX  300

static const SubGhzBlockConst subghz_protocol_nissan_v0_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 200,
    .min_count_bit_for_found = NISSAN_V0_DATA_BITS,
};

static const uint8_t nissan_v0_sync[6] = {1, 1, 0, 0, 0, 1};
static const uint8_t nissan_v0_mark[6] = {0, 0, 0, 0, 1, 0};

struct SubGhzProtocolDecoderNissanV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t hb[SUBGHZ_BITBUF_BYTES(NISSAN_V0_HB_MAX)];
    uint16_t hb_len;
    uint64_t last_data;
    bool has_last;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderNissanV0);


static void nissan_v0_process(SubGhzProtocolDecoderNissanV0* instance) {
    if(instance->hb_len < NISSAN_V0_HB_MIN) {
        instance->hb_len = 0;
        return;
    }

    // stateless Manchester: pair differing half-bits, slip on equal
    uint8_t bits[NISSAN_V0_BITS_MAX];
    uint16_t nbits = 0;
    uint16_t i = 0;
    uint16_t n = instance->hb_len;
    while(i + 1 < n && nbits < NISSAN_V0_BITS_MAX) {
        uint8_t a = subghz_bitbuf_get(instance->hb, i);
        uint8_t b = subghz_bitbuf_get(instance->hb, i + 1);
        if(a == b) {
            i += 1;
            continue;
        }
        bits[nbits++] = (a == 0 && b == 1) ? 1 : 0;
        i += 2;
    }

    int found = -1;
    for(uint16_t p = 0; p + NISSAN_V0_DATA_BITS <= nbits; p++) {
        bool ok = true;
        for(uint8_t j = 0; j < 6; j++) {
            if(bits[p + j] != nissan_v0_sync[j]) {
                ok = false;
                break;
            }
        }
        if(ok) {
            for(uint8_t j = 0; j < 6; j++) {
                if(bits[p + 48 + j] != nissan_v0_mark[j]) {
                    ok = false;
                    break;
                }
            }
        }
        if(ok) {
            found = (int)p;
            break;
        }
    }

    instance->hb_len = 0;
    if(found < 0) return;

    uint64_t data = 0;
    for(uint16_t j = 0; j < NISSAN_V0_DATA_BITS; j++) {
        data = (data << 1) | (bits[found + j] & 1);
    }
    if(instance->has_last && data == instance->last_data) return;
    instance->last_data = data;
    instance->has_last = true;

    instance->generic.data = data;
    instance->generic.data_count_bit = NISSAN_V0_DATA_BITS;
    instance->generic.serial = 0;
    instance->generic.btn = 0;
    instance->generic.cnt = 0;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = NISSAN_V0_DATA_BITS;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_nissan_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderNissanV0* instance = calloc(1, sizeof(SubGhzProtocolDecoderNissanV0));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_nissan_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_nissan_v0_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderNissanV0* instance = context;
    instance->hb_len = 0;
    instance->last_data = 0;
    instance->has_last = false;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_nissan_v0_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderNissanV0* instance = context;

    uint8_t nh;
    if(duration >= 320 && duration <= 700) {
        nh = 1;
    } else if(duration >= 800 && duration <= 1230) {
        nh = 2;
    } else if(duration >= 1300 && duration <= 1730) {
        nh = 3;
    } else if(duration >= 1800 && duration <= 2350) {
        nh = 4;
    } else {
        nissan_v0_process(instance);
        return;
    }

    uint8_t v = level ? 1 : 0;
    for(uint8_t k = 0; k < nh; k++) {
        if(instance->hb_len < NISSAN_V0_HB_MAX) subghz_bitbuf_set(instance->hb, instance->hb_len++, v);
    }
    if(instance->hb_len >= NISSAN_V0_HB_CAP) nissan_v0_process(instance);
}





SubGhzProtocolStatus
    subghz_protocol_decoder_nissan_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderNissanV0* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_nissan_v0_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    instance->generic.serial = 0;
    instance->generic.btn = 0;
    instance->generic.cnt = 0;
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_nissan_v0_decoder = {
    .alloc = subghz_protocol_decoder_nissan_v0_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_nissan_v0_feed,
    .reset = subghz_protocol_decoder_nissan_v0_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_nissan_v0_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_nissan_v0 = {
    .name = NISSAN_V0_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_nissan_v0_decoder,
    .encoder = NULL,
};
