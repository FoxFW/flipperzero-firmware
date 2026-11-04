#include "audi_v3.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"

#define TAG "AudiV3"

// Audi V3 -- ported from firmware audi_v3.c via KeeLoq-RX arke_vag.py (class AudiV3).
// 2-FSK (FM), te_short~=383 / te_long~=766. Custom half-bit demod (NOT the Manchester
// table): a preamble of >=20 short pulses, then one long pulse that opens the data
// window; thereafter one long pulse = one bit, two consecutive short pulses = one bit,
// and the bit value is (level ? 0 : 1) of the completing pulse. 96-bit frame =
// Button(8) | Hop(56) | Counter(24) | Chk(8). Accept gate: button in {0x0C Unlock,
// 0x0D Lock} and the 56-bit hop is neither all-0x00 nor all-0xFF. Rolling hop
// encrypted -> no clone. Decode-only, 2-FSK; .encoder = NULL.

#define AUDI_V3_PRE_LO    220
#define AUDI_V3_PRE_HI    470
#define AUDI_V3_LONG_LO   580
#define AUDI_V3_LONG_HI   980
#define AUDI_V3_GAP       980
#define AUDI_V3_MIN_PRE   20
#define AUDI_V3_DATA_BITS 96

static const SubGhzBlockConst subghz_protocol_audi_v3_const = {
    .te_short = 383,
    .te_long = 766,
    .te_delta = 180,
    .min_count_bit_for_found = AUDI_V3_DATA_BITS,
};

struct SubGhzProtocolDecoderAudiV3 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    uint16_t pre;
    bool half;
    uint8_t bits[AUDI_V3_DATA_BITS];
    uint16_t nbits;

    uint8_t button;
    uint8_t hop[7];
    uint32_t counter; // 24-bit
    uint8_t chk;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderAudiV3);


typedef enum {
    AudiV3StepReset = 0,
    AudiV3StepPreamble = 1,
    AudiV3StepData = 2,
} AudiV3Step;

static void audi_v3_clear(SubGhzProtocolDecoderAudiV3* instance) {
    instance->pre = 0;
    instance->half = false;
    instance->nbits = 0;
}

static void audi_v3_process(SubGhzProtocolDecoderAudiV3* instance) {
    if(instance->nbits < AUDI_V3_DATA_BITS) return;
    const uint8_t* b = instance->bits;

    uint8_t button = 0;
    for(uint8_t i = 0; i < 8; i++) button = (uint8_t)((button << 1) | (b[i] & 1));

    uint8_t hop[7];
    for(uint8_t by = 0; by < 7; by++) {
        uint8_t v = 0;
        for(uint8_t k = 0; k < 8; k++) v = (uint8_t)((v << 1) | (b[8 + by * 8 + k] & 1));
        hop[by] = v;
    }

    uint32_t counter = 0;
    for(uint8_t i = 0; i < 24; i++) counter = (counter << 1) | (b[64 + i] & 1);

    uint8_t chk = 0;
    for(uint8_t i = 0; i < 8; i++) chk = (uint8_t)((chk << 1) | (b[88 + i] & 1));

    // gate: reject degenerate hop (all 0xFF / all 0x00) and non-whitelisted button
    bool all_ff = true, all_00 = true;
    for(uint8_t i = 0; i < 7; i++) {
        if(hop[i] != 0xFF) all_ff = false;
        if(hop[i] != 0x00) all_00 = false;
    }
    if(all_ff || all_00) return;
    if(button != 0x0C && button != 0x0D) return;

    // data = button:hop (64 bits), matches arke `data`
    uint64_t data = button;
    for(uint8_t i = 0; i < 7; i++) data = (data << 8) | hop[i];

    instance->button = button;
    for(uint8_t i = 0; i < 7; i++) instance->hop[i] = hop[i];
    instance->counter = counter;
    instance->chk = chk;

    instance->generic.data = data; // top 64 bits (button | hop)
    instance->generic.data_count_bit = AUDI_V3_DATA_BITS;
    instance->generic.serial = 0;
    instance->generic.btn = button;
    instance->generic.cnt = counter;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = AUDI_V3_DATA_BITS;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_audi_v3_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderAudiV3* instance = calloc(1, sizeof(SubGhzProtocolDecoderAudiV3));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_audi_v3;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_audi_v3_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderAudiV3* instance = context;
    instance->parser_step = AudiV3StepReset;
    audi_v3_clear(instance);
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

static void audi_v3_append(SubGhzProtocolDecoderAudiV3* instance, bool level) {
    if(instance->nbits < AUDI_V3_DATA_BITS) {
        instance->bits[instance->nbits] = level ? 0 : 1; // bit = (level ? 0 : 1)
        instance->nbits++;
    }
}

void subghz_protocol_decoder_audi_v3_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderAudiV3* instance = context;

    bool in_pre = (duration >= AUDI_V3_PRE_LO && duration <= AUDI_V3_PRE_HI);
    bool is_long = (duration >= AUDI_V3_LONG_LO && duration <= AUDI_V3_LONG_HI);
    bool is_short = (duration < AUDI_V3_LONG_LO);

    switch(instance->parser_step) {
    case AudiV3StepReset:
        if(in_pre) {
            instance->pre = 1;
            instance->parser_step = AudiV3StepPreamble;
        }
        break;

    case AudiV3StepPreamble:
        if(in_pre) {
            instance->pre++;
        } else if(is_long && instance->pre >= AUDI_V3_MIN_PRE) {
            instance->parser_step = AudiV3StepData;
            instance->nbits = 0;
            audi_v3_append(instance, level); // first bit from the opening long
            instance->half = false;
        } else {
            audi_v3_clear(instance);
            instance->parser_step = AudiV3StepReset;
        }
        break;

    case AudiV3StepData:
        if(duration >= AUDI_V3_GAP) {
            audi_v3_process(instance);
            audi_v3_clear(instance);
            instance->parser_step = AudiV3StepReset;
            return;
        }
        if(is_long) {
            if(instance->half) {
                audi_v3_process(instance);
                audi_v3_clear(instance);
                instance->parser_step = AudiV3StepReset;
                return;
            } else {
                audi_v3_append(instance, level);
            }
        } else if(is_short) {
            if(instance->half) {
                audi_v3_append(instance, level);
                instance->half = false;
            } else {
                instance->half = true;
            }
        }
        if(instance->nbits >= AUDI_V3_DATA_BITS) {
            audi_v3_process(instance);
            audi_v3_clear(instance);
            instance->parser_step = AudiV3StepReset;
        }
        break;

    default:
        break;
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_audi_v3_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderAudiV3* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t btn = instance->button;
    uint32_t cnt = instance->counter;
    uint32_t chk = instance->chk;
    if(!flipper_format_write_uint32(flipper_format, "Btn", &btn, 1) ||
       !flipper_format_write_uint32(flipper_format, "Cnt", &cnt, 1) ||
       !flipper_format_write_uint32(flipper_format, "Chk", &chk, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_audi_v3_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderAudiV3* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_audi_v3_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint64_t data = instance->generic.data;
    instance->button = (uint8_t)((data >> 56) & 0xFFU);
    for(uint8_t i = 0; i < 7; i++) instance->hop[i] = (uint8_t)((data >> (48 - i * 8)) & 0xFFU);

    uint32_t cnt = 0, chk = 0;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Cnt", &cnt, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Chk", &chk, 1);
    instance->counter = cnt & 0xFFFFFFU;
    instance->chk = (uint8_t)(chk & 0xFFU);

    instance->generic.serial = 0;
    instance->generic.btn = instance->button;
    instance->generic.cnt = instance->counter;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_audi_v3_decoder = {
    .alloc = subghz_protocol_decoder_audi_v3_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_audi_v3_feed,
    .reset = subghz_protocol_decoder_audi_v3_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_audi_v3_serialize,
    .deserialize = subghz_protocol_decoder_audi_v3_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_audi_v3 = {
    .name = AUDI_V3_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_audi_v3_decoder,
    .encoder = NULL,
};
