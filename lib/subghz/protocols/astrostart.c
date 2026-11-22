#include "astrostart.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"

#define TAG "Astrostart"

#define ASTROSTART_TE_SHORT   326
#define ASTROSTART_TE_LONG    526
#define ASTROSTART_GAP_MID    426
#define ASTROSTART_GAP_MIN    150
#define ASTROSTART_GAP_MAX    750
#define ASTROSTART_PULSE_MIN  120
#define ASTROSTART_PULSE_MAX  900

#define ASTROSTART_FRAME_BITS 52

static const SubGhzBlockConst subghz_protocol_astrostart_const = {
    .te_short = ASTROSTART_TE_SHORT,
    .te_long = ASTROSTART_TE_LONG,
    .te_delta = 120,
    .min_count_bit_for_found = ASTROSTART_FRAME_BITS,
};

typedef enum {
    AstrostartDecoderStepReset = 0,
    AstrostartDecoderStepCheckGap,
} AstrostartDecoderStep;

struct SubGhzProtocolDecoderAstrostart {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderAstrostart);

struct SubGhzProtocolEncoderAstrostart {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderAstrostart);

const SubGhzProtocolDecoder subghz_protocol_astrostart_decoder = {
    .alloc = subghz_protocol_decoder_astrostart_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_astrostart_feed,
    .reset = subghz_protocol_decoder_astrostart_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_astrostart_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_astrostart_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_recon_manchester");

const SubGhzProtocol subghz_protocol_astrostart = {
    .name = ASTROSTART_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_astrostart_decoder,
    .encoder = &subghz_protocol_astrostart_encoder_ext.vtable,
};

static bool subghz_protocol_astrostart_check(
    uint64_t data,
    uint32_t* serial,
    uint8_t* btn,
    uint8_t* crc) {
    uint8_t bytes[7];
    bytes[0] = (uint8_t)(data >> 44);
    bytes[1] = (uint8_t)(data >> 36);
    bytes[2] = (uint8_t)(data >> 28);
    bytes[3] = (uint8_t)(data >> 20);
    bytes[4] = (uint8_t)(data >> 12);
    bytes[5] = (uint8_t)(data >> 4);
    uint8_t rx_checksum = (uint8_t)(data & 0x0F);

    if(bytes[0] != (uint8_t)(~bytes[1] & 0xFF)) {
        return false;
    }

    uint8_t expected = 0;
    for(int i = 2; i < 6; i++) {
        expected = (expected + (bytes[i] >> 4)) & 0x0F;
        expected = (expected + bytes[i]) & 0x0F;
    }
    if(expected != rx_checksum) {
        return false;
    }

    *serial = ((uint32_t)bytes[2] << 24) | ((uint32_t)bytes[3] << 16) |
              ((uint32_t)bytes[4] << 8) | (uint32_t)bytes[5];
    *btn = bytes[0];
    *crc = rx_checksum;
    return true;
}

static void subghz_protocol_astrostart_finalize(SubGhzProtocolDecoderAstrostart* instance) {
    if(instance->decoder.decode_count_bit != ASTROSTART_FRAME_BITS) return;

    uint32_t serial = 0;
    uint8_t btn = 0;
    uint8_t crc = 0;
    if(!subghz_protocol_astrostart_check(
           instance->decoder.decode_data, &serial, &btn, &crc)) {
        return;
    }

    instance->generic.data = instance->decoder.decode_data;
    instance->generic.data_count_bit = ASTROSTART_FRAME_BITS;
    instance->generic.serial = serial;
    instance->generic.btn = btn;
    instance->generic.cnt = 0;
    instance->generic.crc = crc;

    FURI_LOG_D(
        TAG, "Frame ID:%08lX Btn:%02X CRC:%X", (unsigned long)serial, btn, crc);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void* subghz_protocol_decoder_astrostart_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderAstrostart* instance =
        malloc(sizeof(SubGhzProtocolDecoderAstrostart));
    instance->base.protocol = &subghz_protocol_astrostart;
    instance->generic.protocol_name = subghz_protocol_astrostart.name;
    return instance;
}

void subghz_protocol_decoder_astrostart_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderAstrostart* instance = context;
    instance->decoder.parser_step = AstrostartDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_astrostart_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderAstrostart* instance = context;

    switch(instance->decoder.parser_step) {
    case AstrostartDecoderStepReset:
        if(level && duration >= ASTROSTART_PULSE_MIN && duration <= ASTROSTART_PULSE_MAX) {

            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = AstrostartDecoderStepCheckGap;
        }
        break;

    case AstrostartDecoderStepCheckGap:
        if(level) {

            if(duration < ASTROSTART_PULSE_MIN || duration > ASTROSTART_PULSE_MAX) {
                subghz_protocol_astrostart_finalize(instance);
                instance->decoder.parser_step = AstrostartDecoderStepReset;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
            }
        } else {

            if(duration < ASTROSTART_GAP_MIN) {

                instance->decoder.parser_step = AstrostartDecoderStepReset;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
            } else if(duration >= ASTROSTART_GAP_MAX) {

                subghz_protocol_astrostart_finalize(instance);
                instance->decoder.parser_step = AstrostartDecoderStepReset;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
            } else {

                uint8_t bit = (duration < ASTROSTART_GAP_MID) ? 0 : 1;
                if(instance->decoder.decode_count_bit >= 64) {

                    instance->decoder.parser_step = AstrostartDecoderStepReset;
                    instance->decoder.decode_data = 0;
                    instance->decoder.decode_count_bit = 0;
                } else {
                    subghz_protocol_blocks_add_bit(&instance->decoder, bit);
                }
            }
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_astrostart_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderAstrostart* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_astrostart_const.min_count_bit_for_found);
}

