#include "renault_v2.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"

#define TAG "RenaultV2"

#define RENAULT_V2_DATA_BITS   82
#define RENAULT_V2_MIN_PROCESS 60
#define RENAULT_V2_ARR_SIZE    200
#define RENAULT_V2_OVERFLOW    190
#define RENAULT_V2_LEN_TOL     4
#define RENAULT_V2_SYNC        0x71EAU

static const SubGhzBlockConst subghz_protocol_renault_v2_const = {
    .te_short = 230,
    .te_long = 460,
    .te_delta = 60,
    .min_count_bit_for_found = RENAULT_V2_DATA_BITS,
};

static const uint8_t renault_v2_sync_bits[16] =
    {0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 0, 1, 0, 1, 0};

typedef enum {
    RenaultV2StepReset = 0,
    RenaultV2StepCollect = 1,
} RenaultV2Step;

struct SubGhzProtocolDecoderRenaultV2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t step;
    bool half_bit;
    uint8_t all_bits[RENAULT_V2_ARR_SIZE];
    uint16_t all_bit_count;

    uint32_t data_tail;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderRenaultV2);

static void renault_v2_process(SubGhzProtocolDecoderRenaultV2* instance) {
    const uint8_t* bits = instance->all_bits;
    int n = (int)instance->all_bit_count;
    if(n < RENAULT_V2_MIN_PROCESS) return;

    int search_end = n - RENAULT_V2_MIN_PROCESS;
    int sync_pos = -1;
    for(int i = 0; i <= search_end; i++) {
        bool match = true;
        for(int j = 0; j < 16; j++) {
            if(bits[i + j] != renault_v2_sync_bits[j]) {
                match = false;
                break;
            }
        }
        if(match) {
            sync_pos = i;
            break;
        }
    }
    if(sync_pos < 0) return;

    int data_after = n - sync_pos;

    if(data_after < RENAULT_V2_DATA_BITS || data_after > RENAULT_V2_DATA_BITS + RENAULT_V2_LEN_TOL)
        return;

    int data_len = data_after < RENAULT_V2_DATA_BITS ? data_after : RENAULT_V2_DATA_BITS;

    uint64_t gdata = 0;
    int top = data_len < 64 ? data_len : 64;
    for(int i = 0; i < top; i++) gdata = (gdata << 1) | (bits[sync_pos + i] & 1);
    uint32_t tail = 0;
    for(int i = 64; i < data_len; i++) tail = (tail << 1) | (bits[sync_pos + i] & 1);

    instance->data_tail = tail;

    instance->generic.data = gdata;
    instance->generic.data_count_bit = RENAULT_V2_DATA_BITS;
    instance->generic.serial = 0;
    instance->generic.btn = 0;
    instance->generic.cnt = 0;
    instance->decoder.decode_data = gdata;
    instance->decoder.decode_count_bit = RENAULT_V2_DATA_BITS;

    if(instance->base.callback)
        instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_renault_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderRenaultV2* instance = calloc(1, sizeof(SubGhzProtocolDecoderRenaultV2));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_renault_v2;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_renault_v2_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV2* instance = context;
    instance->step = RenaultV2StepReset;
    instance->half_bit = false;
    instance->all_bit_count = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_renault_v2_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV2* instance = context;
    uint32_t ts = subghz_protocol_renault_v2_const.te_short;
    uint32_t tl = subghz_protocol_renault_v2_const.te_long;
    uint32_t d = subghz_protocol_renault_v2_const.te_delta;

    if(instance->step == RenaultV2StepReset) {
        if(DURATION_DIFF(duration, ts) <= d) {
            instance->step = RenaultV2StepCollect;
            instance->all_bit_count = 0;
            instance->half_bit = true;
        }
        return;
    }

    if(instance->all_bit_count >= RENAULT_V2_OVERFLOW) {
        renault_v2_process(instance);
        instance->step = RenaultV2StepReset;
        return;
    }

    bool is_short = DURATION_DIFF(duration, ts) <= d;
    bool is_long = DURATION_DIFF(duration, tl) <= d;

    if(is_long) {
        if(instance->half_bit) {

            renault_v2_process(instance);
            instance->step = RenaultV2StepReset;
        } else {

            if(instance->all_bit_count < RENAULT_V2_ARR_SIZE)
                instance->all_bits[instance->all_bit_count++] = level ? 1 : 0;
        }
    } else if(is_short) {
        if(instance->half_bit) {

            if(instance->all_bit_count < RENAULT_V2_ARR_SIZE)
                instance->all_bits[instance->all_bit_count++] = level ? 1 : 0;
            instance->half_bit = false;
        } else {
            instance->half_bit = true;
        }
    } else {

        renault_v2_process(instance);
        instance->step = RenaultV2StepReset;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_renault_v2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV2* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t tail = instance->data_tail;
    if(!flipper_format_write_uint32(flipper_format, "Data2", &tail, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint8_t fdata[11];
    uint64_t g = instance->generic.data;
    for(uint8_t b = 0; b < 8; b++) fdata[b] = (uint8_t)(g >> (56 - b * 8));
    uint32_t t = instance->data_tail;
    fdata[8] = (uint8_t)((t >> 10) & 0xFF);
    fdata[9] = (uint8_t)((t >> 2) & 0xFF);
    fdata[10] = (uint8_t)((t & 0x3) << 6);
    if(!flipper_format_write_hex(flipper_format, "Data", fdata, sizeof(fdata))) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_renault_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV2* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_renault_v2_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint32_t tail = 0;
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Data2", &tail, 1)) {
        instance->data_tail = tail & 0x0003FFFFU;
    } else {
        instance->data_tail = 0;
    }

    instance->generic.serial = 0;
    instance->generic.btn = 0;
    instance->generic.cnt = 0;
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_renault_v2_decoder = {
    .alloc = subghz_protocol_decoder_renault_v2_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_renault_v2_feed,
    .reset = subghz_protocol_decoder_renault_v2_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_renault_v2_serialize,
    .deserialize = subghz_protocol_decoder_renault_v2_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_renault_v2_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_recon_manchester");

const SubGhzProtocol subghz_protocol_renault_v2 = {
    .name = RENAULT_V2_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_renault_v2_decoder,
    .encoder = &subghz_protocol_renault_v2_encoder_ext.vtable,
};
