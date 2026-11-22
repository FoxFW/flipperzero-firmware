#include "subaru_liberty_gt.h"
#include "common.h"
#include "decode_plugin.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include <lib/toolbox/manchester_decoder.h>
#include <string.h>

#define SLGT_TE_SHORT     700U
#define SLGT_TE_LONG      1400U
#define SLGT_TE_DELTA     100U
#define SLGT_FRAME_BITS   128U
#define SLGT_PREAMBLE_BITS 9U

static const SubGhzBlockConst subghz_protocol_subaru_liberty_gt_const = {
    .te_short = SLGT_TE_SHORT,
    .te_long = SLGT_TE_LONG,
    .te_delta = SLGT_TE_DELTA,
    .min_count_bit_for_found = SLGT_FRAME_BITS,
};

struct SubGhzProtocolDecoderSubaruLibertyGt {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint64_t data_hi;
    uint64_t data_lo;
    uint16_t bit_count;
    uint64_t last_hi;
    uint64_t last_lo;
    bool have_last;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderSubaruLibertyGt);

const SubGhzProtocolDecoder subghz_protocol_subaru_liberty_gt_decoder = {
    .alloc = subghz_protocol_decoder_subaru_liberty_gt_alloc,
    .free = subghz_protocol_decoder_subaru_liberty_gt_free,
    .feed = subghz_protocol_decoder_subaru_liberty_gt_feed,
    .reset = subghz_protocol_decoder_subaru_liberty_gt_reset,
    .get_hash_data = subghz_protocol_decoder_subaru_liberty_gt_get_hash_data,
    .serialize = subghz_protocol_decoder_subaru_liberty_gt_serialize,
    .deserialize = subghz_protocol_decoder_subaru_liberty_gt_deserialize,
    .get_string = subghz_protocol_decoder_subaru_liberty_gt_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_subaru_liberty_gt_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_subaru_liberty_gt");

const SubGhzProtocol subghz_protocol_subaru_liberty_gt = {
    .name = SUBGHZ_PROTOCOL_SUBARU_LIBERTY_GT_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_subaru_liberty_gt_decoder,
    .encoder = &subghz_protocol_subaru_liberty_gt_encoder_ext.vtable,
};

void* subghz_protocol_decoder_subaru_liberty_gt_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderSubaruLibertyGt* instance =
        malloc(sizeof(SubGhzProtocolDecoderSubaruLibertyGt));
    memset(instance, 0, sizeof(SubGhzProtocolDecoderSubaruLibertyGt));
    instance->base.protocol = &subghz_protocol_subaru_liberty_gt;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->manchester_state = ManchesterStateMid1;
    return instance;
}

void subghz_protocol_decoder_subaru_liberty_gt_free(void* context) {
    furi_assert(context);
    free(context);
}

static void slgt_accum_reset(SubGhzProtocolDecoderSubaruLibertyGt* instance) {
    instance->data_hi = 0;
    instance->data_lo = 0;
    instance->bit_count = 0;
    instance->manchester_state = ManchesterStateMid1;
}

void subghz_protocol_decoder_subaru_liberty_gt_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderSubaruLibertyGt* instance = context;
    slgt_accum_reset(instance);
    instance->last_hi = 0;
    instance->last_lo = 0;
    instance->have_last = false;
}

static void slgt_add_bit(SubGhzProtocolDecoderSubaruLibertyGt* instance, bool bit) {
    instance->data_hi = (instance->data_hi << 1) | ((instance->data_lo >> 63) & 1U);
    instance->data_lo = (instance->data_lo << 1) | (bit ? 1U : 0U);
    if(instance->bit_count < 0xFFFFU) instance->bit_count++;
}

static bool slgt_frame_ok(SubGhzProtocolDecoderSubaruLibertyGt* instance) {

    uint64_t top = instance->data_hi >> (64U - SLGT_PREAMBLE_BITS);
    uint64_t mask = (1ULL << SLGT_PREAMBLE_BITS) - 1U;
    if((top & mask) != mask) return false;
    if((instance->data_hi >> (64U - SLGT_PREAMBLE_BITS - 1U)) & 1U) return false;
    if(instance->data_hi == 0xFFFFFFFFFFFFFFFFULL && instance->data_lo == 0xFFFFFFFFFFFFFFFFULL)
        return false;
    return true;
}

void subghz_protocol_decoder_subaru_liberty_gt_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderSubaruLibertyGt* instance = context;
    const uint32_t te_short = subghz_protocol_subaru_liberty_gt_const.te_short;
    const uint32_t te_long = subghz_protocol_subaru_liberty_gt_const.te_long;
    const uint32_t te_delta = subghz_protocol_subaru_liberty_gt_const.te_delta;

    ManchesterEvent event;
    if(DURATION_DIFF(duration, te_short) < te_delta) {
        event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
    } else if(DURATION_DIFF(duration, te_long) < te_delta) {
        event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
    } else {

        slgt_accum_reset(instance);
        instance->decoder.te_last = duration;
        return;
    }

    bool data_bit = false;
    if(manchester_advance(
           instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
        slgt_add_bit(instance, data_bit);

        if(instance->bit_count >= SLGT_FRAME_BITS) {
            instance->bit_count = SLGT_FRAME_BITS;
            if(slgt_frame_ok(instance)) {
                instance->generic.data = instance->data_lo;
                instance->generic.data_count_bit = SLGT_FRAME_BITS;
                instance->generic.serial =
                    (uint32_t)((instance->data_lo >> 16) & 0xFFFFFFFFULL);
                instance->generic.btn = 0;
                instance->generic.cnt = 0;
                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);

                slgt_accum_reset(instance);
            }
        }
    }
    instance->decoder.te_last = duration;
}

uint8_t subghz_protocol_decoder_subaru_liberty_gt_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderSubaruLibertyGt* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_subaru_liberty_gt_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderSubaruLibertyGt* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;
    uint8_t frame[16];
    for(int i = 0; i < 8; i++) frame[i] = (uint8_t)(instance->data_hi >> (56 - i * 8));
    for(int i = 0; i < 8; i++) frame[8 + i] = (uint8_t)(instance->data_lo >> (56 - i * 8));
    if(!flipper_format_write_hex(flipper_format, "Frame", frame, 16))
        return SubGhzProtocolStatusErrorParserOthers;
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus subghz_protocol_decoder_subaru_liberty_gt_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderSubaruLibertyGt* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, SLGT_FRAME_BITS);
    if(ret != SubGhzProtocolStatusOk) return ret;
    uint8_t frame[16] = {0};
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_hex(flipper_format, "Frame", frame, 16)) {
        instance->data_hi = 0;
        instance->data_lo = 0;
        for(int i = 0; i < 8; i++) instance->data_hi = (instance->data_hi << 8) | frame[i];
        for(int i = 0; i < 8; i++) instance->data_lo = (instance->data_lo << 8) | frame[8 + i];
    }
    return ret;
}

void subghz_protocol_decoder_subaru_liberty_gt_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderSubaruLibertyGt* instance = context;
    const SubGhzDecodeInput input = {
        .generic = &instance->generic,
    };
    subghz_decode_plugin_get_string("Subaru Liberty GT", &input, output);
}
