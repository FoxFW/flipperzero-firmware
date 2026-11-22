#include "compustar.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"

#define TAG "Compustar"

#define COMPUSTAR_TE_SHORT 708
#define COMPUSTAR_TE_LONG  1076
#define COMPUSTAR_TE_DELTA 250
#define COMPUSTAR_PULSE_MIN 400
#define COMPUSTAR_PULSE_MAX 1330
#define COMPUSTAR_GAP_END  1400
#define COMPUSTAR_MID      ((COMPUSTAR_TE_SHORT + COMPUSTAR_TE_LONG) / 2)
#define COMPUSTAR_BIT_COUNT 36
#define COMPUSTAR_MAX_BITS 40

static const SubGhzBlockConst subghz_protocol_compustar_const = {
    .te_short = COMPUSTAR_TE_SHORT,
    .te_long = COMPUSTAR_TE_LONG,
    .te_delta = COMPUSTAR_TE_DELTA,
    .min_count_bit_for_found = COMPUSTAR_BIT_COUNT,
};

typedef enum {
    CompustarDecoderStepReset = 0,
    CompustarDecoderStepData,
} CompustarDecoderStep;

struct SubGhzProtocolDecoderCompustar {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderCompustar);

struct SubGhzProtocolEncoderCompustar {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderCompustar);

const SubGhzProtocolDecoder subghz_protocol_compustar_decoder = {
    .alloc = subghz_protocol_decoder_compustar_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_compustar_feed,
    .reset = subghz_protocol_decoder_compustar_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_compustar_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_compustar_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_recon_manchester");

const SubGhzProtocol subghz_protocol_compustar = {
    .name = COMPUSTAR_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_compustar_decoder,
    .encoder = &subghz_protocol_compustar_encoder_ext.vtable,
};

typedef struct {
    const char* name;
    uint8_t len;
    const uint8_t* vals;
} CompustarButton;

static bool subghz_protocol_compustar_check(SubGhzBlockGeneric* instance) {
    if(instance->data_count_bit != COMPUSTAR_BIT_COUNT) return false;
    uint64_t data = instance->data & 0xFFFFFFFFFULL;

    uint16_t id = (uint16_t)((data >> 20) & 0xFFFF);
    if(id == 0x0000 || id == 0xFFFF) return false;

    if(((data >> 17) & 0x7) != 0x7) return false;

    uint8_t button_inverse = (uint8_t)((data >> 9) & 0xFF);
    uint8_t button = (uint8_t)((data >> 1) & 0xFF);

    if((uint8_t)(~button_inverse) != button) return false;

    instance->serial = id;
    instance->btn = button;
    instance->cnt = 0;
    return true;
}

static void subghz_protocol_compustar_process_frame(SubGhzProtocolDecoderCompustar* instance) {
    uint16_t count = instance->decoder.decode_count_bit;

    if(count == 35) {
        instance->decoder.decode_data <<= 1;
        count = 36;
    }
    if(count != COMPUSTAR_BIT_COUNT) return;

    instance->generic.data = instance->decoder.decode_data & 0xFFFFFFFFFULL;
    instance->generic.data_count_bit = COMPUSTAR_BIT_COUNT;

    if(!subghz_protocol_compustar_check(&instance->generic)) return;

    FURI_LOG_D(
        TAG,
        "Id:%04lX Btn:%02X",
        (unsigned long)instance->generic.serial,
        instance->generic.btn);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void* subghz_protocol_decoder_compustar_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderCompustar* instance = malloc(sizeof(SubGhzProtocolDecoderCompustar));
    instance->base.protocol = &subghz_protocol_compustar;
    instance->generic.protocol_name = subghz_protocol_compustar.name;
    return instance;
}

void subghz_protocol_decoder_compustar_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderCompustar* instance = context;
    instance->decoder.parser_step = CompustarDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_compustar_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderCompustar* instance = context;

    switch(instance->decoder.parser_step) {
    case CompustarDecoderStepReset:
        if(level && duration >= COMPUSTAR_PULSE_MIN && duration <= COMPUSTAR_PULSE_MAX) {

            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            subghz_protocol_blocks_add_bit(
                &instance->decoder, (duration < COMPUSTAR_MID) ? 1 : 0);
            instance->decoder.parser_step = CompustarDecoderStepData;
        }
        break;

    case CompustarDecoderStepData:
        if(level) {

            if(duration < COMPUSTAR_PULSE_MIN || duration > COMPUSTAR_PULSE_MAX) {

                instance->decoder.parser_step = CompustarDecoderStepReset;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                break;
            }
            if(instance->decoder.decode_count_bit >= COMPUSTAR_MAX_BITS) {

                instance->decoder.parser_step = CompustarDecoderStepReset;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                break;
            }
            subghz_protocol_blocks_add_bit(
                &instance->decoder, (duration < COMPUSTAR_MID) ? 1 : 0);
        } else {

            if(duration >= COMPUSTAR_GAP_END) {
                subghz_protocol_compustar_process_frame(instance);
                instance->decoder.parser_step = CompustarDecoderStepReset;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
            }

        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_compustar_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderCompustar* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_compustar_const.min_count_bit_for_found);
}

