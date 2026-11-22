

#include "hitag2.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#include <furi.h>
#include <furi_hal.h>
#include "common.h"
#include <lib/subghz/blocks/bitbuf.h>
#include "../encode_plugin.h"

#define TAG "SubGhzProtocolHitag2"

#define HITAG2_DATA_BITS     103
#define HITAG2_ALL_BITS_MAX  400

#define HITAG2_MIN_BITS      88

#define HITAG2_PREAMBLE_MIN  14

#define HITAG2_MAXDUR        700

#define HITAG2_GAP           1500
#define HITAG2_MIN_PULSE     30

#define HITAG2_TE_MIN        70
#define HITAG2_TE_MAX        320

static const SubGhzBlockConst subghz_protocol_hitag2_const = {
    .te_short = 240,
    .te_long = 480,
    .te_delta = 120,
    .min_count_bit_for_found = HITAG2_MIN_BITS,
};

struct SubGhzProtocolDecoderHitag2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    int16_t durs[HITAG2_MAXDUR];
    uint16_t dur_count;

    uint8_t all_bits[SUBGHZ_BITBUF_BYTES(HITAG2_ALL_BITS_MAX)];

    uint32_t uid;
    uint8_t  btn;
    uint16_t lctr;
    uint32_t ks;
    uint8_t  chk;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderHitag2);

struct SubGhzProtocolEncoderHitag2 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static uint32_t hitag2_bits_to_int(const uint8_t* bits, int lo, int len) {
    uint32_t v = 0;
    for(int i = 0; i < len; i++) v = (v << 1) | (subghz_bitbuf_get(bits, lo + i) & 1);
    return v;
}

static bool subghz_protocol_decoder_hitag2_find_and_emit(
    SubGhzProtocolDecoderHitag2* instance, int n) {
    const uint8_t* b = instance->all_bits;
    if(n < HITAG2_MIN_BITS) return false;

    for(int cs = 0; cs + 88 <= n; cs++) {

        if(subghz_bitbuf_get(b, cs + 79) != 0) continue;

        if(cs < 15) continue;
        uint16_t marker = (uint16_t)hitag2_bits_to_int(b, cs - 15, 16);
        if(marker != 0x0001) continue;

        uint8_t x = 0;
        for(int i = cs; i < cs + 80; i += 8) {
            x ^= (uint8_t)hitag2_bits_to_int(b, i, 8);
        }
        uint8_t chk = (uint8_t)hitag2_bits_to_int(b, cs + 80, 8);
        if(x != chk) continue;

        uint32_t uid = hitag2_bits_to_int(b, cs + 1, 32);
        if(uid == 0 || uid == 0xFFFFFFFF) continue;

        instance->uid = uid;
        instance->btn = (uint8_t)hitag2_bits_to_int(b, cs + 33, 4);
        instance->lctr = (uint16_t)hitag2_bits_to_int(b, cs + 37, 10);
        instance->ks = hitag2_bits_to_int(b, cs + 47, 32);
        instance->chk = chk;

        if((instance->lctr == 0 && instance->ks == 0) ||
           (instance->lctr == 0x3FF && instance->ks == 0xFFFFFFFF)) {
            continue;
        }

        instance->generic.data = ((uint64_t)instance->uid << 32) |
                                 ((uint64_t)(instance->btn & 0xF) << 28) |
                                 ((uint64_t)(instance->lctr & 0x3FF) << 18) |
                                 (uint64_t)(instance->ks >> 14);
        instance->generic.data_count_bit = HITAG2_DATA_BITS;
        instance->generic.serial = instance->uid;
        instance->generic.btn = instance->btn;
        instance->generic.cnt = instance->lctr;

        instance->decoder.decode_data = instance->generic.data;
        instance->decoder.decode_count_bit = HITAG2_DATA_BITS;

        FURI_LOG_I(
            TAG,
            "Hitag2: UID=%08lX btn=%X lctr=%03X ks=%08lX chk=%02X",
            (unsigned long)instance->uid,
            instance->btn,
            instance->lctr,
            (unsigned long)instance->ks,
            instance->chk);

        if(instance->base.callback) {
            instance->base.callback(&instance->base, instance->base.context);
        }
        return true;
    }
    return false;
}

static uint32_t hitag2_estimate_te(const int16_t* durs, int count) {

    enum { BINS = (HITAG2_TE_MAX - HITAG2_TE_MIN) / 10 + 1 };
    uint16_t hist[BINS];
    for(int i = 0; i < BINS; i++) hist[i] = 0;
    for(int i = 0; i < count; i++) {
        int d = durs[i] < 0 ? -durs[i] : durs[i];
        if(d < HITAG2_TE_MIN || d > HITAG2_TE_MAX) continue;
        hist[(d - HITAG2_TE_MIN) / 10]++;
    }
    int best = 0;
    for(int i = 1; i < BINS; i++)
        if(hist[i] > hist[best]) best = i;
    return (uint32_t)(HITAG2_TE_MIN + best * 10 + 5);
}

static int hitag2_demod(
    const int16_t* durs, int count, uint32_t te_short, bool polarity, uint8_t* out) {
    uint32_t te_long = 2 * te_short;
    uint32_t delta = te_short * 45 / 100;
    if(delta < 40) delta = 40;
    int n = 0;
    bool half = false;
    for(int i = 0; i < count && n < HITAG2_ALL_BITS_MAX - 1; i++) {
        bool level = durs[i] > 0;
        uint32_t d = durs[i] < 0 ? (uint32_t)(-durs[i]) : (uint32_t)durs[i];
        bool is_long = DURATION_DIFF(d, te_long) <= delta;
        bool is_short = DURATION_DIFF(d, te_short) <= delta;
        uint8_t bl = polarity ? (level ? 1 : 0) : (level ? 0 : 1);
        if(is_long) {
            subghz_bitbuf_set(out, n++, bl);
            half = false;
        } else if(is_short) {
            if(half) {
                subghz_bitbuf_set(out, n++, bl);
                half = false;
            } else {
                half = true;
            }
        } else {
            half = false;
        }
    }
    return n;
}

static void subghz_protocol_decoder_hitag2_process_burst(SubGhzProtocolDecoderHitag2* instance) {
    if(instance->dur_count < HITAG2_MIN_BITS) return;
    uint32_t te = hitag2_estimate_te(instance->durs, instance->dur_count);
    for(int pol = 0; pol < 2; pol++) {
        int n = hitag2_demod(instance->durs, instance->dur_count, te, pol != 0, instance->all_bits);
        if(subghz_protocol_decoder_hitag2_find_and_emit(instance, n)) return;
    }
    FURI_LOG_D(TAG, "No Hitag2 frame in burst (%d pulses, te≈%lu)",
        instance->dur_count, (unsigned long)te);
}

void subghz_protocol_decoder_hitag2_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderHitag2* instance = context;

    if(duration >= HITAG2_GAP) {

        subghz_protocol_decoder_hitag2_process_burst(instance);
        instance->dur_count = 0;
        return;
    }
    if(duration < HITAG2_MIN_PULSE) {
        return;
    }
    if(instance->dur_count >= HITAG2_MAXDUR) {
        subghz_protocol_decoder_hitag2_process_burst(instance);
        instance->dur_count = 0;
    }
    int32_t v = (int32_t)duration;
    if(v > 32000) v = 32000;
    instance->durs[instance->dur_count++] = (int16_t)(level ? v : -v);
}

const SubGhzProtocolDecoder subghz_protocol_hitag2_decoder = {
    .alloc = subghz_protocol_decoder_hitag2_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_hitag2_feed,
    .reset = subghz_protocol_decoder_hitag2_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_hitag2_serialize,
    .deserialize = subghz_protocol_decoder_hitag2_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_hitag2_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

static const SubGhzEncoderExternal subghz_protocol_hitag2_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_recon_manchester");

const SubGhzProtocol subghz_protocol_hitag2 = {
    .name = SUBGHZ_PROTOCOL_HITAG2_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_hitag2_decoder,
    .encoder = &subghz_protocol_hitag2_encoder_ext.vtable,
};

void* subghz_protocol_decoder_hitag2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHitag2* instance = malloc(sizeof(SubGhzProtocolDecoderHitag2));
    instance->base.protocol = &subghz_protocol_hitag2;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_hitag2_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHitag2* instance = context;
    instance->decoder.parser_step = 0;
    instance->decoder.te_last = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->dur_count = 0;
    instance->generic.data_count_bit = 0;
}

SubGhzProtocolStatus subghz_protocol_decoder_hitag2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderHitag2* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t ks = instance->ks;
        flipper_format_write_uint32(flipper_format, "Ks", &ks, 1);

        uint8_t fr[13];
        memset(fr, 0, sizeof(fr));
        for(int k = 0; k < 16; k++) subghz_bitbuf_set(fr, k, (0x0001 >> (15 - k)) & 1);
        for(int k = 0; k < 32; k++) subghz_bitbuf_set(fr, 16 + k, (instance->uid >> (31 - k)) & 1);
        for(int k = 0; k < 4; k++) subghz_bitbuf_set(fr, 48 + k, (instance->btn >> (3 - k)) & 1);
        for(int k = 0; k < 10; k++) subghz_bitbuf_set(fr, 52 + k, (instance->lctr >> (9 - k)) & 1);
        for(int k = 0; k < 32; k++) subghz_bitbuf_set(fr, 62 + k, (instance->ks >> (31 - k)) & 1);
        subghz_bitbuf_set(fr, 94, 0);
        uint8_t x = 0;
        for(int i = 15; i < 95; i += 8) x ^= (uint8_t)hitag2_bits_to_int(fr, i, 8);
        for(int k = 0; k < 8; k++) subghz_bitbuf_set(fr, 95 + k, (x >> (7 - k)) & 1);
        flipper_format_write_hex(flipper_format, "Data", fr, sizeof(fr));
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_hitag2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderHitag2* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_hitag2_const.min_count_bit_for_found);
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t ks = 0;
        flipper_format_read_uint32(flipper_format, "Ks", &ks, 1);
        instance->ks = ks;
        instance->uid = instance->generic.serial;
        instance->btn = instance->generic.btn;
        instance->lctr = (uint16_t)instance->generic.cnt;
    }
    return ret;
}

