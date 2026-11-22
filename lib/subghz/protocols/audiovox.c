#include "audiovox.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"

#define TAG "Audiovox"

#define AUDIOVOX_TE_SHORT 445
#define AUDIOVOX_TE_LONG  895
#define AUDIOVOX_TE_SYNC  1368
#define AUDIOVOX_TE_RESET 1790

#define AUDIOVOX_MARK_MIN      150
#define AUDIOVOX_SHORT_LONG_MID 670
#define AUDIOVOX_LONG_SYNC_MID 1131
#define AUDIOVOX_SYNC_MAX      1600
#define AUDIOVOX_GAP_RESET     1300

#define AUDIOVOX_FRAME_BITS 25
#define AUDIOVOX_MAX_BITS   40

static const SubGhzBlockConst subghz_protocol_audiovox_const = {
    .te_short = AUDIOVOX_TE_SHORT,
    .te_long = AUDIOVOX_TE_LONG,
    .te_delta = 180,
    .min_count_bit_for_found = AUDIOVOX_FRAME_BITS,
};

typedef enum {
    AudiovoxDecoderStepReset = 0,
    AudiovoxDecoderStepData,
} AudiovoxDecoderStep;

struct SubGhzProtocolDecoderAudiovox {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderAudiovox {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef struct SubGhzProtocolDecoderAudiovox SubGhzProtocolDecoderAudiovox;
typedef struct SubGhzProtocolEncoderAudiovox SubGhzProtocolEncoderAudiovox;

static void* subghz_protocol_decoder_audiovox_alloc(SubGhzEnvironment* environment);
static void subghz_protocol_decoder_audiovox_reset(void* context);
static void subghz_protocol_decoder_audiovox_feed(void* context, bool level, uint32_t duration);
static SubGhzProtocolStatus
    subghz_protocol_decoder_audiovox_deserialize(void* context, FlipperFormat* flipper_format);

const SubGhzProtocolDecoder subghz_protocol_audiovox_decoder = {
    .alloc = subghz_protocol_decoder_audiovox_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_audiovox_feed,
    .reset = subghz_protocol_decoder_audiovox_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_audiovox_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_audiovox_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_recon_manchester");

const SubGhzProtocol subghz_protocol_audiovox = {
    .name = AUDIOVOX_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_audiovox_decoder,
    .encoder = &subghz_protocol_audiovox_encoder_ext.vtable,
};

typedef struct {
    uint16_t id;
    uint8_t btn_bits;
} AudiovoxFrame;

static bool subghz_protocol_audiovox_parse(uint64_t data, AudiovoxFrame* out) {

    uint8_t b0 = (uint8_t)((data >> 17) & 0xFF);
    uint8_t b1 = (uint8_t)((data >> 9) & 0xFF);
    uint8_t b2 = (uint8_t)((data >> 1) & 0xFF);

    if((b2 & 0xAA) || (b2 == 0x55)) {
        return false;
    }

    uint8_t i0 = (uint8_t)~b0;
    uint8_t i1 = (uint8_t)~b1;
    uint8_t i2 = (uint8_t)~b2;

    uint16_t id = (uint16_t)((i0 << 8) | i1);
    if(id == 0 || id == 0xFFFF) {
        return false;
    }

    int matches = 0;
    uint8_t mask = 0x01;
    for(int i = 0; i < 4; i++) {
        if(i2 & mask) matches++;
        mask <<= 2;
    }
    if(!matches) {
        return false;
    }

    out->id = id;
    out->btn_bits = i2 & 0x55;
    return true;
}

static void subghz_protocol_audiovox_process(SubGhzProtocolDecoderAudiovox* instance) {
    if(instance->decoder.decode_count_bit != AUDIOVOX_FRAME_BITS) {
        return;
    }

    AudiovoxFrame frame;
    if(!subghz_protocol_audiovox_parse(instance->decoder.decode_data, &frame)) {
        return;
    }

    instance->generic.data = instance->decoder.decode_data;
    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
    instance->generic.serial = frame.id;
    instance->generic.btn = frame.btn_bits;
    instance->generic.cnt = 0;

    FURI_LOG_D(TAG, "Frame: ID:%04X Btn:%02X", frame.id, frame.btn_bits);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

static void* subghz_protocol_decoder_audiovox_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderAudiovox* instance = malloc(sizeof(SubGhzProtocolDecoderAudiovox));
    instance->base.protocol = &subghz_protocol_audiovox;
    instance->generic.protocol_name = subghz_protocol_audiovox.name;
    return instance;
}

static void subghz_protocol_decoder_audiovox_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderAudiovox* instance = context;
    instance->decoder.parser_step = AudiovoxDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

static void subghz_protocol_decoder_audiovox_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderAudiovox* instance = context;

    switch(instance->decoder.parser_step) {
    case AudiovoxDecoderStepReset:

        if(level && duration >= AUDIOVOX_LONG_SYNC_MID && duration < AUDIOVOX_SYNC_MAX) {
            instance->decoder.parser_step = AudiovoxDecoderStepData;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        }
        break;

    case AudiovoxDecoderStepData:
        if(level) {

            if(duration < AUDIOVOX_MARK_MIN) {

                instance->decoder.parser_step = AudiovoxDecoderStepReset;
            } else if(duration < AUDIOVOX_SHORT_LONG_MID) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
            } else if(duration < AUDIOVOX_LONG_SYNC_MID) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
            } else if(duration < AUDIOVOX_SYNC_MAX) {

                subghz_protocol_audiovox_process(instance);
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
            } else {

                subghz_protocol_audiovox_process(instance);
                instance->decoder.parser_step = AudiovoxDecoderStepReset;
            }

            if(instance->decoder.decode_count_bit > AUDIOVOX_MAX_BITS) {
                instance->decoder.parser_step = AudiovoxDecoderStepReset;
            }
        } else {

            if(duration >= AUDIOVOX_GAP_RESET) {
                subghz_protocol_audiovox_process(instance);
                instance->decoder.parser_step = AudiovoxDecoderStepReset;
            }

        }
        break;
    }
}

static SubGhzProtocolStatus
    subghz_protocol_decoder_audiovox_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderAudiovox* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_audiovox_const.min_count_bit_for_found);
}

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderAudiovox);
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderAudiovox);
