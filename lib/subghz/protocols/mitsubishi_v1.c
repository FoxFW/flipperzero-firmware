#include "mitsubishi_v1.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"

#define TAG "MitsubishiV1"

#define MITSUBISHI_V1_DATA_BITS 96
#define MITSUBISHI_V1_ALL_BITS_MAX 200
#define MITSUBISHI_V1_FIX_INVARIANT 0xE0U

static const SubGhzBlockConst subghz_protocol_mitsubishi_v1_const = {
    .te_short = 200,
    .te_long = 400,
    .te_delta = 100,
    .min_count_bit_for_found = MITSUBISHI_V1_DATA_BITS,
};

static const uint8_t mitsubishi_v1_sync[32] = {0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0,
                                               0, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0,
                                               0, 1, 1, 0, 1, 0, 0, 1, 1, 1};

struct SubGhzProtocolDecoderMitsubishiV1 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t step;
    bool half;
    uint8_t all_bits[MITSUBISHI_V1_ALL_BITS_MAX];
    uint16_t bit_count;

    uint8_t fix;
    uint8_t btn;
    uint16_t cnt;
    uint64_t hop;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderMitsubishiV1);

static int32_t mitsubishi_v1_find_sync(const uint8_t* bits, uint16_t n) {
    const int32_t need = 56;
    int32_t search_end = (int32_t)n - 32 - need;
    for(int32_t i = 0; i <= search_end; i++) {
        bool match = true;
        for(int32_t j = 0; j < 32; j++) {
            if(bits[i + j] != mitsubishi_v1_sync[j]) {
                match = false;
                break;
            }
        }
        if(match) return i;
    }
    return -1;
}

static void mitsubishi_v1_reset_accum(SubGhzProtocolDecoderMitsubishiV1* instance) {
    instance->step = 0;
    instance->half = false;
    instance->bit_count = 0;
}

static void mitsubishi_v1_process_frame(SubGhzProtocolDecoderMitsubishiV1* instance) {
    uint16_t n = instance->bit_count;
    if(n < 88) return;
    int32_t sync_pos = mitsubishi_v1_find_sync(instance->all_bits, n);
    if(sync_pos < 0) return;

    int32_t data_start = sync_pos;
    int32_t data_len = (int32_t)n - data_start;
    if(data_len > 96) data_len = 96;
    if(data_len < 88) return;

    uint64_t data = 0;
    int32_t take = data_len < 64 ? data_len : 64;
    for(int32_t i = 0; i < take; i++) {
        data = (data << 1) | (instance->all_bits[data_start + i] & 1U);
    }

    uint64_t hop = 0;
    int32_t hop_bits = 0;
    int32_t hop_end = data_len < 96 ? data_len : 96;
    for(int32_t i = 54; i < hop_end; i++) {
        hop = (hop << 1) | (instance->all_bits[data_start + i] & 1U);
        hop_bits++;
    }
    if(hop_bits < 42) hop <<= (42 - hop_bits);

    uint8_t fix = (uint8_t)((data >> 24) & 0xFFU);
    uint8_t btn = (uint8_t)((data >> 20) & 0x0FU);
    uint16_t cnt = (uint16_t)((data >> 10) & 0x3FFU);

    mitsubishi_v1_reset_accum(instance);

    if(fix != MITSUBISHI_V1_FIX_INVARIANT) return;

    instance->fix = fix;
    instance->btn = btn;
    instance->cnt = cnt;
    instance->hop = hop;

    instance->generic.data = data;
    instance->generic.data_count_bit = MITSUBISHI_V1_DATA_BITS;
    instance->generic.serial = fix;
    instance->generic.btn = btn;
    instance->generic.cnt = cnt;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = MITSUBISHI_V1_DATA_BITS;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_mitsubishi_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMitsubishiV1* instance =
        calloc(1, sizeof(SubGhzProtocolDecoderMitsubishiV1));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_mitsubishi_v1;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_mitsubishi_v1_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMitsubishiV1* instance = context;
    mitsubishi_v1_reset_accum(instance);
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_mitsubishi_v1_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderMitsubishiV1* instance = context;
    uint32_t te_s = subghz_protocol_mitsubishi_v1_const.te_short;
    uint32_t te_l = subghz_protocol_mitsubishi_v1_const.te_long;
    uint32_t te_d = subghz_protocol_mitsubishi_v1_const.te_delta;

    if(instance->step == 0) {

        if(duration >= (te_s - te_d) && duration <= (te_l + te_d)) {
            instance->step = 1;
            instance->bit_count = 0;
            instance->half = false;

        } else {
            return;
        }
    }

    if(instance->bit_count >= (MITSUBISHI_V1_ALL_BITS_MAX - 1)) {
        mitsubishi_v1_process_frame(instance);
        return;
    }
    if(duration < (te_s / 2)) return;

    bool is_long = (duration > ((te_s + te_l) / 2)) && (duration <= (te_l + te_d));
    bool is_short = (duration >= (te_s - te_d)) && (duration <= ((te_s + te_l) / 2));

    if(duration > (te_l + te_d)) {
        mitsubishi_v1_process_frame(instance);
        return;
    }

    if(is_long) {
        instance->all_bits[instance->bit_count++] = level ? 1 : 0;
        instance->half = false;
    } else if(is_short) {
        if(instance->half) {
            instance->all_bits[instance->bit_count++] = level ? 1 : 0;
            instance->half = false;
        } else {
            instance->half = true;
        }
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_mitsubishi_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderMitsubishiV1* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t fix = instance->fix;
    uint32_t btn = instance->btn;
    uint32_t cnt = instance->cnt;
    uint32_t hop_hi = (uint32_t)(instance->hop >> 32);
    uint32_t hop_lo = (uint32_t)(instance->hop & 0xFFFFFFFFU);
    if(!flipper_format_write_uint32(flipper_format, "Fix", &fix, 1) ||
       !flipper_format_write_uint32(flipper_format, "Btn", &btn, 1) ||
       !flipper_format_write_uint32(flipper_format, "Cnt", &cnt, 1) ||
       !flipper_format_write_uint32(flipper_format, "HopHi", &hop_hi, 1) ||
       !flipper_format_write_uint32(flipper_format, "HopLo", &hop_lo, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint8_t fdata[12];
    uint64_t g = instance->generic.data;
    for(uint8_t b = 0; b < 8; b++) fdata[b] = (uint8_t)(g >> (56 - b * 8));
    uint32_t hoplo = (uint32_t)(instance->hop & 0xFFFFFFFFUL);
    fdata[8] = (uint8_t)(hoplo >> 24);
    fdata[9] = (uint8_t)(hoplo >> 16);
    fdata[10] = (uint8_t)(hoplo >> 8);
    fdata[11] = (uint8_t)(hoplo);
    if(!flipper_format_write_hex(flipper_format, "Data", fdata, sizeof(fdata))) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus subghz_protocol_decoder_mitsubishi_v1_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderMitsubishiV1* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_mitsubishi_v1_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint64_t data = instance->generic.data;
    instance->fix = (uint8_t)((data >> 24) & 0xFFU);
    instance->btn = (uint8_t)((data >> 20) & 0x0FU);
    instance->cnt = (uint16_t)((data >> 10) & 0x3FFU);

    uint32_t hop_hi = 0, hop_lo = 0;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "HopHi", &hop_hi, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "HopLo", &hop_lo, 1);
    instance->hop = ((uint64_t)hop_hi << 32) | hop_lo;

    instance->generic.serial = instance->fix;
    instance->generic.btn = instance->btn;
    instance->generic.cnt = instance->cnt;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_mitsubishi_v1_decoder = {
    .alloc = subghz_protocol_decoder_mitsubishi_v1_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_mitsubishi_v1_feed,
    .reset = subghz_protocol_decoder_mitsubishi_v1_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_mitsubishi_v1_serialize,
    .deserialize = subghz_protocol_decoder_mitsubishi_v1_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_mitsubishi_v1_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_recon_manchester");

const SubGhzProtocol subghz_protocol_mitsubishi_v1 = {
    .name = MITSUBISHI_V1_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,

    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_mitsubishi_v1_decoder,
    .encoder = &subghz_protocol_mitsubishi_v1_encoder_ext.vtable,
};
