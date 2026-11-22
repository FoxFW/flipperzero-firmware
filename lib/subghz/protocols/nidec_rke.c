#include "nidec_rke.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"

#define TAG "NidecRke"

#define NIDEC_TE_SHORT 250
#define NIDEC_TE_LONG  500

#define NIDEC_MARK_MIN     130
#define NIDEC_MARK_MID     375
#define NIDEC_MARK_MAX     700
#define NIDEC_GAP_RESET    700

#define NIDEC_SYNC_WORD    0xFFFFFFF0UL

#define NIDEC_PAYLOAD_BITS 64
#define NIDEC_MIN_BITS     56
#define NIDEC_HEAD_BITS    48

static const SubGhzBlockConst subghz_protocol_nidec_rke_const = {
    .te_short = NIDEC_TE_SHORT,
    .te_long = NIDEC_TE_LONG,
    .te_delta = 120,
    .min_count_bit_for_found = NIDEC_MIN_BITS,
};

typedef enum {
    NidecRkeDecoderStepReset = 0,
    NidecRkeDecoderStepData,
} NidecRkeDecoderStep;

struct SubGhzProtocolDecoderNidecRke {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t sync_reg;
    uint64_t payload;
    uint16_t payload_bits;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderNidecRke);

struct SubGhzProtocolEncoderNidecRke {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderNidecRke);

const SubGhzProtocolDecoder subghz_protocol_nidec_rke_decoder = {
    .alloc = subghz_protocol_decoder_nidec_rke_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_nidec_rke_feed,
    .reset = subghz_protocol_decoder_nidec_rke_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_nidec_rke_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_nidec_rke_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_recon_manchester");

const SubGhzProtocol subghz_protocol_nidec_rke = {
    .name = NIDEC_RKE_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_nidec_rke_decoder,
    .encoder = &subghz_protocol_nidec_rke_encoder_ext.vtable,
};

static inline uint8_t nidec_byte(uint64_t data, uint8_t index) {

    return (uint8_t)((data >> (56 - 8 * index)) & 0xFF);
}

static bool nidec_button_is_valid(uint8_t button) {

    return button == 0x3 || button == 0x4 || button == 0x5 || button == 0x6 || button == 0xF;
}

static void subghz_protocol_nidec_rke_process(SubGhzProtocolDecoderNidecRke* instance) {
    uint16_t bits = instance->payload_bits;
    if(bits < NIDEC_MIN_BITS) return;
    if(bits > NIDEC_PAYLOAD_BITS) bits = NIDEC_PAYLOAD_BITS;

    uint64_t data = instance->payload;
    if(bits < NIDEC_PAYLOAD_BITS) data <<= (NIDEC_PAYLOAD_BITS - bits);

    uint8_t byte5 = nidec_byte(data, 5);

    if((byte5 & 0xF0) != 0x50) return;

    uint32_t sequence = ((uint32_t)nidec_byte(data, 0) << 8) | nidec_byte(data, 1);
    uint32_t id = ((uint32_t)nidec_byte(data, 2) << 16) | ((uint32_t)nidec_byte(data, 3) << 8) |
                  nidec_byte(data, 4);
    uint8_t button = byte5 & 0x0F;
    uint32_t security = ((uint32_t)nidec_byte(data, 6) << 8) | nidec_byte(data, 7);

    if(id == 0 || id == 0xFFFFFF) return;
    if(sequence == 0 || sequence == 0xFFFF) return;
    if(security == 0 || security == 0xFFFF) return;
    if(!nidec_button_is_valid(button)) return;

    instance->generic.data = data;
    instance->generic.data_count_bit = bits;
    instance->generic.serial = id;
    instance->generic.btn = button;
    instance->generic.cnt = sequence;
    instance->generic.seed = security;

    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = bits;

    FURI_LOG_D(
        TAG, "ID:%06lX Seq:%04lX Btn:%X Sec:%04lX (%dbit)", id, sequence, button, security, bits);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void* subghz_protocol_decoder_nidec_rke_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderNidecRke* instance = malloc(sizeof(SubGhzProtocolDecoderNidecRke));
    instance->base.protocol = &subghz_protocol_nidec_rke;
    instance->generic.protocol_name = subghz_protocol_nidec_rke.name;
    return instance;
}

void subghz_protocol_decoder_nidec_rke_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderNidecRke* instance = context;
    instance->decoder.parser_step = NidecRkeDecoderStepReset;
    instance->sync_reg = 0;
    instance->payload = 0;
    instance->payload_bits = 0;
}

void subghz_protocol_decoder_nidec_rke_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderNidecRke* instance = context;

    if(!level) {

        if(duration >= NIDEC_GAP_RESET) {
            if(instance->decoder.parser_step == NidecRkeDecoderStepData) {
                subghz_protocol_nidec_rke_process(instance);
            }
            instance->decoder.parser_step = NidecRkeDecoderStepReset;
            instance->sync_reg = 0;
            instance->payload = 0;
            instance->payload_bits = 0;
        }
        return;
    }

    uint8_t raw_bit;
    if(duration >= NIDEC_MARK_MIN && duration < NIDEC_MARK_MID) {
        raw_bit = 1;
    } else if(duration >= NIDEC_MARK_MID && duration <= NIDEC_MARK_MAX) {
        raw_bit = 0;
    } else {

        instance->decoder.parser_step = NidecRkeDecoderStepReset;
        instance->sync_reg = 0;
        instance->payload = 0;
        instance->payload_bits = 0;
        return;
    }

    switch(instance->decoder.parser_step) {
    case NidecRkeDecoderStepReset:
        instance->sync_reg = (instance->sync_reg << 1) | raw_bit;
        if(instance->sync_reg == NIDEC_SYNC_WORD) {
            instance->decoder.parser_step = NidecRkeDecoderStepData;
            instance->payload = 0;
            instance->payload_bits = 0;
        }
        break;

    case NidecRkeDecoderStepData:

        instance->payload = (instance->payload << 1) | (uint8_t)(raw_bit ^ 1);
        instance->payload_bits++;
        if(instance->payload_bits >= NIDEC_PAYLOAD_BITS) {
            subghz_protocol_nidec_rke_process(instance);
            instance->decoder.parser_step = NidecRkeDecoderStepReset;
            instance->sync_reg = 0;
            instance->payload = 0;
            instance->payload_bits = 0;
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_nidec_rke_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderNidecRke* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_nidec_rke_const.min_count_bit_for_found);
}

