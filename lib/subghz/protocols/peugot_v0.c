

#include "peugot_v0.h"

#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#include <furi.h>
#include <furi_hal.h>
#include "common.h"
#include "../encode_plugin.h"
#include <lib/subghz/blocks/bitbuf.h>

#define TAG "SubGhzProtocolPeugotV0"

#define PEUGOT_V0_DATA_BITS         80
#define PEUGOT_V0_MAX_BITS          256
#define PEUGOT_V0_MIN_PREAMBLE_BITS 30
#define PEUGOT_V0_SYNC_LEN          8
#define PEUGOT_V0_MIN_DATA_BITS     70

static const uint8_t peugot_v0_sync_pattern_full[] = {
    1,1,1,1,1,1,1,0,
    1,1,1,1,1,1,1,1,
    1,0,1,0,1,1,0,1
};
#define PEUGOT_V0_SYNC_SEARCH_LEN 24

static const SubGhzBlockConst subghz_protocol_peugot_v0_const = {
    .te_short = 220,
    .te_long = 440,
    .te_delta = 100,
    .min_count_bit_for_found = PEUGOT_V0_DATA_BITS,
};

struct SubGhzProtocolDecoderPeugotV0 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t all_bits[SUBGHZ_BITBUF_BYTES(PEUGOT_V0_MAX_BITS)];
    uint16_t bit_count;
    bool half_bit;

    uint16_t data_16;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderPeugotV0);

struct SubGhzProtocolEncoderPeugotV0 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint16_t data_16;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderPeugotV0);

const SubGhzProtocolDecoder subghz_protocol_peugot_v0_decoder = {
    .alloc = subghz_protocol_decoder_peugot_v0_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_peugot_v0_feed,
    .reset = subghz_protocol_decoder_peugot_v0_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_peugot_v0_serialize,
    .deserialize = subghz_protocol_decoder_peugot_v0_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_peugot_v0_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_peugot_v0");

const SubGhzProtocol subghz_protocol_peugot_v0 = {
    .name = SUBGHZ_PROTOCOL_PEUGOT_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_peugot_v0_decoder,
    .encoder = &subghz_protocol_peugot_v0_encoder_ext.vtable,
};

void* subghz_protocol_decoder_peugot_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderPeugotV0* instance = malloc(sizeof(SubGhzProtocolDecoderPeugotV0));
    instance->base.protocol = &subghz_protocol_peugot_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_peugot_v0_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderPeugotV0* instance = context;
    instance->decoder.parser_step = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->bit_count = 0;
    instance->half_bit = false;
    instance->data_16 = 0;
}

static void peugot_v0_process_frame(SubGhzProtocolDecoderPeugotV0* instance) {
    if(instance->bit_count < (PEUGOT_V0_SYNC_LEN + PEUGOT_V0_MIN_DATA_BITS)) return;

    int sync_pos = -1;
    int run = 0;
    for(int i = 0; i + PEUGOT_V0_SYNC_SEARCH_LEN <= (int)instance->bit_count; i++) {
        if(subghz_bitbuf_get(instance->all_bits, i) == 1) {
            run++;
            continue;
        }

        if(run >= (PEUGOT_V0_MIN_PREAMBLE_BITS + 7) && i >= 7) {
            int cand = i - 7;
            bool match = true;
            for(int j = 0; j < PEUGOT_V0_SYNC_SEARCH_LEN; j++) {
                if(subghz_bitbuf_get(instance->all_bits, cand + j) != peugot_v0_sync_pattern_full[j]) {
                    match = false;
                    break;
                }
            }
            if(match) {
                sync_pos = cand;
                break;
            }
        }
        run = 0;
    }

    if(sync_pos < 0) {
        FURI_LOG_D(TAG, "No preamble-anchored sync in %u bits", instance->bit_count);
        return;
    }

    int preamble_ones = run - 7;

    int data_start = sync_pos + PEUGOT_V0_SYNC_LEN;
    int data_available = instance->bit_count - data_start;
    if(data_available < PEUGOT_V0_DATA_BITS) {
        FURI_LOG_D(TAG, "Data too short: %d bits (need %d)", data_available, PEUGOT_V0_DATA_BITS);
        return;
    }
    int data_bits = PEUGOT_V0_DATA_BITS;

    uint64_t data = 0;
    int bits_for_data = (data_bits > 64) ? 64 : data_bits;
    for(int i = 0; i < bits_for_data; i++) {
        data = (data << 1) | subghz_bitbuf_get(instance->all_bits, data_start + i);
    }
    if(bits_for_data < 64) {
        data <<= (64 - bits_for_data);
    }

    uint16_t data_16 = 0;
    if(data_bits > 64) {
        int extra_bits = data_bits - 64;
        if(extra_bits > 16) extra_bits = 16;
        for(int i = 0; i < extra_bits; i++) {
            data_16 = (data_16 << 1) | subghz_bitbuf_get(instance->all_bits, data_start + 64 + i);
        }
        if(extra_bits < 16) {
            data_16 <<= (16 - extra_bits);
        }
    }

    if((data == 0 && data_16 == 0) ||
       (data == 0xFFFFFFFFFFFFFFFFULL && data_16 == 0xFFFF)) {
        FURI_LOG_D(TAG, "Degenerate payload rejected");
        return;
    }

    instance->generic.data = data;
    instance->generic.data_count_bit = PEUGOT_V0_DATA_BITS;
    instance->data_16 = data_16;

    uint32_t key_hi = (uint32_t)(data >> 32);
    uint32_t key_lo = (uint32_t)(data & 0xFFFFFFFF);

    FURI_LOG_I(
        TAG,
        "Decoded %dbit: Key=%08lX%08lX Key2=%04X (preamble=%d, sync@%d)",
        data_bits,
        (unsigned long)key_hi,
        (unsigned long)key_lo,
        data_16,
        preamble_ones,
        sync_pos);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void subghz_protocol_decoder_peugot_v0_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderPeugotV0* instance = context;

    bool is_short = DURATION_DIFF(duration, subghz_protocol_peugot_v0_const.te_short) <=
                    subghz_protocol_peugot_v0_const.te_delta;
    bool is_long = DURATION_DIFF(duration, subghz_protocol_peugot_v0_const.te_long) <=
                   subghz_protocol_peugot_v0_const.te_delta;

    if(is_short) {
        if(instance->half_bit) {

            if(instance->bit_count < PEUGOT_V0_MAX_BITS) {
                subghz_bitbuf_set(instance->all_bits, instance->bit_count++, level ? 1 : 0);
            }
            instance->half_bit = false;
        } else {

            instance->half_bit = true;
        }
    } else if(is_long) {

        if(instance->bit_count < PEUGOT_V0_MAX_BITS) {
            subghz_bitbuf_set(instance->all_bits, instance->bit_count++, level ? 1 : 0);
        }
        instance->half_bit = false;
    } else {

        if(instance->bit_count > 0) {
            peugot_v0_process_frame(instance);
            instance->bit_count = 0;
            instance->half_bit = false;
        }
    }

    if(instance->bit_count >= PEUGOT_V0_MAX_BITS) {
        peugot_v0_process_frame(instance);
        instance->bit_count = 0;
        instance->half_bit = false;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_peugot_v0_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderPeugotV0* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t key2 = instance->data_16;
    if(!flipper_format_write_uint32(flipper_format, "Key2", &key2, 1)) {
        FURI_LOG_E(TAG, "Failed to write Key2");
        return SubGhzProtocolStatusError;
    }

    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_peugot_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderPeugotV0* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_deserialize(&instance->generic, flipper_format);

    if(ret != SubGhzProtocolStatusOk) {
        FURI_LOG_E(TAG, "Failed to deserialize base fields");
        return ret;
    }

    if(instance->generic.data_count_bit != PEUGOT_V0_DATA_BITS) {
        FURI_LOG_E(TAG, "Invalid bit count: %lu", (unsigned long)instance->generic.data_count_bit);
        return SubGhzProtocolStatusErrorValueBitCount;
    }

    uint32_t key2 = 0;
    flipper_format_read_uint32(flipper_format, "Key2", &key2, 1);
    instance->data_16 = (uint16_t)(key2 & 0xFFFF);

    instance->generic.protocol_name = instance->base.protocol->name;
    return SubGhzProtocolStatusOk;
}

