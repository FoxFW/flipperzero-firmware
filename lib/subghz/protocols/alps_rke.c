#include "alps_rke.h"

#include <furi.h>
#include <string.h>
#include <lib/subghz/types.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include <flipper_format/flipper_format.h>

#ifndef ENABLE_EMULATE_FEATURE
#define ENABLE_EMULATE_FEATURE 1
#endif

#ifndef PP_COMPAT_SHIM_DEFINED
#define PP_COMPAT_SHIM_DEFINED
#define FF_BIT                    "Bit"
#define FF_KEY                    "Key"
#define FF_SERIAL                 "Serial"
#define FF_BTN                    "Btn"
#define FF_CNT                    "Cnt"
#define FF_REPEAT                 "Repeat"
#define FF_PROTOCOL               "Protocol"
#define FF_TYPE                   "Type"
#define PP_FIELD_SERIAL           0x01U
#define PP_FIELD_BTN              0x02U
#define PP_FIELD_CNT              0x04U
#define PP_FIELD_TYPE             0x08U
#define PP_ENCODER_REPEAT_MAX     50U
#define PP_SHARED_UPLOAD_CAPACITY 2048U

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
} ProtoPirateDecoderHeader;

typedef struct {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
} ProtoPirateEncoderHeader;

#endif

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#include <string.h>
#include "common.h"
#include "../encode_plugin.h"
#include "pp_auto_common.h"

#define TAG "AlpsRke"

#define ALPS_RKE_FRAME_BITS   76
#define ALPS_RKE_NEED_BITS    72
#define ALPS_RKE_MIN_PREAMBLE 8
#define ALPS_RKE_MAX_BITS     96
#define ALPS_RKE_RING_DROP    16

static const SubGhzBlockConst subghz_protocol_alps_rke_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 150,
    .min_count_bit_for_found = ALPS_RKE_FRAME_BITS,
};

typedef enum {
    AlpsRkeDecoderStepReset = 0,
    AlpsRkeDecoderStepPreamble,
    AlpsRkeDecoderStepData,
} AlpsRkeDecoderStep;

struct SubGhzProtocolDecoderAlpsRke {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t preamble_count;
    uint16_t bit_count;
    ManchesterState manchester_state;
    uint8_t bits[ALPS_RKE_MAX_BITS];
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderAlpsRke);

struct SubGhzProtocolEncoderAlpsRke {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderAlpsRke);

SubGhzProtocolStatus subghz_protocol_decoder_alps_rke_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderAlpsRke* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint64_t d = instance->generic.data;
    uint32_t id = (uint32_t)(d >> 32);
    uint8_t b5 = (uint8_t)(d >> 24);
    uint8_t b6 = (uint8_t)(d >> 16);
    uint16_t seq = (uint16_t)d;
    uint8_t fdata[10] = {
        0x55,
        (uint8_t)(id >> 24),
        (uint8_t)(id >> 16),
        (uint8_t)(id >> 8),
        (uint8_t)id,
        b5,
        b6,
        (uint8_t)(seq >> 8),
        (uint8_t)seq,
        0};
    if(!flipper_format_write_hex(flipper_format, "Data", fdata, sizeof(fdata))) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

const SubGhzProtocolDecoder subghz_protocol_alps_rke_decoder = {
    .alloc = subghz_protocol_decoder_alps_rke_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_alps_rke_feed,
    .reset = subghz_protocol_decoder_alps_rke_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_alps_rke_serialize,
    .deserialize = subghz_protocol_decoder_alps_rke_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_alps_rke_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_recon_manchester");

const SubGhzProtocol subghz_protocol_alps_rke = {
    .name = ALPS_RKE_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_alps_rke_decoder,
    .encoder = &subghz_protocol_alps_rke_encoder_ext.vtable,
};

void* subghz_protocol_decoder_alps_rke_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderAlpsRke* instance = malloc(sizeof(SubGhzProtocolDecoderAlpsRke));
    instance->base.protocol = &subghz_protocol_alps_rke;
    instance->generic.protocol_name = subghz_protocol_alps_rke.name;
    return instance;
}

void subghz_protocol_decoder_alps_rke_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderAlpsRke* instance = context;
    instance->decoder.parser_step = AlpsRkeDecoderStepReset;
    instance->preamble_count = 0;
    instance->bit_count = 0;
    instance->manchester_state = ManchesterStateMid1;
}

static bool subghz_protocol_alps_rke_try_decode(SubGhzProtocolDecoderAlpsRke* instance, bool invert) {
    const uint16_t count = instance->bit_count;
    if(count < ALPS_RKE_NEED_BITS) return false;

    int best_off = -1;
    uint8_t best_bytes[9] = {0};

    for(int off = 0; off + ALPS_RKE_NEED_BITS <= (int)count; off++) {
        uint8_t bytes[9];
        for(int k = 0; k < 9; k++) {
            uint8_t b = 0;
            for(int j = 0; j < 8; j++) {
                uint8_t bit = instance->bits[off + (k * 8) + j];
                if(invert) bit ^= 1u;
                b = (uint8_t)((b << 1) | (bit & 1u));
            }
            bytes[k] = b;
        }

        if(bytes[0] != 0x55) continue;
        if(bytes[5] != bytes[6]) continue;

        uint32_t id = ((uint32_t)bytes[1] << 24) | ((uint32_t)bytes[2] << 16) |
                      ((uint32_t)bytes[3] << 8) | (uint32_t)bytes[4];
        if(id == 0 || id == 0xFFFFFFFF) continue;

        if(id == 0x55555555 || id == 0xAAAAAAAA) continue;

        best_off = off;
        memcpy(best_bytes, bytes, sizeof(bytes));
    }

    if(best_off < 0) return false;

    uint32_t id = ((uint32_t)best_bytes[1] << 24) | ((uint32_t)best_bytes[2] << 16) |
                  ((uint32_t)best_bytes[3] << 8) | (uint32_t)best_bytes[4];
    uint8_t button = best_bytes[5] >> 4;
    uint16_t sequence = (uint16_t)(((uint16_t)best_bytes[7] << 8) | best_bytes[8]);

    uint64_t data = ((uint64_t)id << 32) | ((uint64_t)best_bytes[5] << 24) |
                    ((uint64_t)best_bytes[6] << 16) | (uint64_t)sequence;

    instance->generic.data = data;
    instance->generic.data_count_bit = ALPS_RKE_FRAME_BITS;
    instance->generic.serial = id;
    instance->generic.btn = button;
    instance->generic.cnt = sequence;

    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = ALPS_RKE_FRAME_BITS;

    FURI_LOG_D(TAG, "Frame: Id:%08lX Btn:%X Cnt:%04X", (unsigned long)id, button, sequence);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
    return true;
}

static void subghz_protocol_alps_rke_finalize(SubGhzProtocolDecoderAlpsRke* instance) {
    if(!subghz_protocol_alps_rke_try_decode(instance, false)) {
        subghz_protocol_alps_rke_try_decode(instance, true);
    }
    instance->decoder.parser_step = AlpsRkeDecoderStepReset;
    instance->preamble_count = 0;
    instance->bit_count = 0;
}

static void subghz_protocol_alps_rke_store_bit(SubGhzProtocolDecoderAlpsRke* instance, bool bit) {
    if(instance->bit_count >= ALPS_RKE_MAX_BITS) {

        memmove(
            instance->bits,
            instance->bits + ALPS_RKE_RING_DROP,
            ALPS_RKE_MAX_BITS - ALPS_RKE_RING_DROP);
        instance->bit_count = ALPS_RKE_MAX_BITS - ALPS_RKE_RING_DROP;
    }
    instance->bits[instance->bit_count++] = bit ? 1u : 0u;
}

static void subghz_protocol_alps_rke_manchester_feed(
    SubGhzProtocolDecoderAlpsRke* instance,
    bool level,
    uint32_t duration) {
    ManchesterEvent event =
        ppa_manchester_event(duration, level, &subghz_protocol_alps_rke_const);
    if(event == ManchesterEventReset) {

        subghz_protocol_alps_rke_finalize(instance);
        return;
    }

    bool data_bit = false;
    if(manchester_advance(
           instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
        subghz_protocol_alps_rke_store_bit(instance, data_bit);
    }
}

void subghz_protocol_decoder_alps_rke_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderAlpsRke* instance = context;

    const SubGhzBlockConst* t = &subghz_protocol_alps_rke_const;
    bool is_short = DURATION_DIFF(duration, t->te_short) < t->te_delta;

    switch(instance->decoder.parser_step) {
    case AlpsRkeDecoderStepReset:
        if(is_short) {
            instance->preamble_count = 1;
            instance->decoder.parser_step = AlpsRkeDecoderStepPreamble;
        }
        break;

    case AlpsRkeDecoderStepPreamble:
        if(is_short) {
            instance->preamble_count++;
            if(instance->preamble_count >= ALPS_RKE_MIN_PREAMBLE) {

                instance->decoder.parser_step = AlpsRkeDecoderStepData;
                instance->bit_count = 0;
                instance->manchester_state = ManchesterStateMid1;
                manchester_advance(
                    instance->manchester_state,
                    ManchesterEventReset,
                    &instance->manchester_state,
                    NULL);
                subghz_protocol_alps_rke_manchester_feed(instance, level, duration);
            }
        } else {

            instance->decoder.parser_step = AlpsRkeDecoderStepReset;
            instance->preamble_count = 0;
        }
        break;

    case AlpsRkeDecoderStepData:
        subghz_protocol_alps_rke_manchester_feed(instance, level, duration);
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_alps_rke_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderAlpsRke* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_alps_rke_const.min_count_bit_for_found);
}

