#include "continental_rke.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"

#define TAG "ContinentalRKE"

#define CONTINENTAL_RKE_TE_SHORT   100
#define CONTINENTAL_RKE_TE_LONG    200
#define CONTINENTAL_RKE_TE_DELTA   40
#define CONTINENTAL_RKE_TE_RESET   1500

#define CONTINENTAL_RKE_TE_THRESH  (CONTINENTAL_RKE_TE_SHORT + (CONTINENTAL_RKE_TE_SHORT / 2))

#define CONTINENTAL_RKE_TE_MAX     (CONTINENTAL_RKE_TE_LONG + CONTINENTAL_RKE_TE_DELTA)

#define CONTINENTAL_RKE_PREAMBLE_20 0xF0F06UL
#define CONTINENTAL_RKE_PREAMBLE_MASK 0xFFFFFUL

#define CONTINENTAL_RKE_PAYLOAD_BYTES 14
#define CONTINENTAL_RKE_PAYLOAD_BITS  112
#define CONTINENTAL_RKE_KEY_BITS      64

static const SubGhzBlockConst subghz_protocol_continental_rke_const = {
    .te_short = CONTINENTAL_RKE_TE_SHORT,
    .te_long = CONTINENTAL_RKE_TE_LONG,
    .te_delta = CONTINENTAL_RKE_TE_DELTA,
    .min_count_bit_for_found = CONTINENTAL_RKE_KEY_BITS,
};

struct SubGhzProtocolDecoderContinentalRke {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t te_accum;
    uint32_t preamble_shift;
    bool collecting;
    uint16_t frame_bit;
    uint8_t frame[CONTINENTAL_RKE_PAYLOAD_BYTES];
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderContinentalRke);

struct SubGhzProtocolEncoderContinentalRke {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderContinentalRke);

SubGhzProtocolStatus subghz_protocol_decoder_continental_rke_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderContinentalRke* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    if(!flipper_format_write_hex(
           flipper_format, "Data", instance->frame, CONTINENTAL_RKE_PAYLOAD_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

const SubGhzProtocolDecoder subghz_protocol_continental_rke_decoder = {
    .alloc = subghz_protocol_decoder_continental_rke_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_continental_rke_feed,
    .reset = subghz_protocol_decoder_continental_rke_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_continental_rke_serialize,
    .deserialize = subghz_protocol_decoder_continental_rke_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_continental_rke_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_recon_manchester");

const SubGhzProtocol subghz_protocol_continental_rke = {
    .name = CONTINENTAL_RKE_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_continental_rke_decoder,
    .encoder = &subghz_protocol_continental_rke_encoder_ext.vtable,
};

static void
    subghz_protocol_continental_rke_process_frame(SubGhzProtocolDecoderContinentalRke* instance) {

    uint8_t xor_sum = 0;
    for(size_t i = 0; i < CONTINENTAL_RKE_PAYLOAD_BYTES; i++) {
        xor_sum ^= instance->frame[i];
    }
    if(xor_sum != 0) {
        return;
    }

    const uint8_t* b = instance->frame;

    uint32_t id = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) |
                  (uint32_t)b[3];
    uint8_t button = b[5] >> 4;
    uint32_t sequence = ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 8) | (uint32_t)b[8];
    uint32_t encrypted = ((uint32_t)b[9] << 24) | ((uint32_t)b[10] << 16) |
                         ((uint32_t)b[11] << 8) | (uint32_t)b[12];

    uint64_t data = 0;
    for(size_t i = 0; i < 8; i++) {
        data = (data << 8) | (uint64_t)b[i];
    }

    instance->generic.data = data;
    instance->generic.data_count_bit = CONTINENTAL_RKE_PAYLOAD_BITS;
    instance->generic.serial = id;
    instance->generic.btn = button;
    instance->generic.cnt = sequence;
    instance->generic.seed = encrypted;

    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = CONTINENTAL_RKE_KEY_BITS;

    FURI_LOG_D(
        TAG,
        "ID:%08lX Btn:%X Cnt:%06lX Enc:%08lX",
        (unsigned long)id,
        button,
        (unsigned long)sequence,
        (unsigned long)encrypted);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

static void
    subghz_protocol_continental_rke_add_bit(SubGhzProtocolDecoderContinentalRke* instance, uint8_t bit) {
    bit &= 1;

    if(!instance->collecting) {
        instance->preamble_shift =
            ((instance->preamble_shift << 1) | bit) & CONTINENTAL_RKE_PREAMBLE_MASK;
        if(instance->preamble_shift == CONTINENTAL_RKE_PREAMBLE_20) {
            instance->collecting = true;
            instance->frame_bit = 0;
            for(size_t i = 0; i < CONTINENTAL_RKE_PAYLOAD_BYTES; i++) {
                instance->frame[i] = 0;
            }
        }
        return;
    }

    uint16_t idx = instance->frame_bit;
    if(bit) {
        instance->frame[idx >> 3] |= (uint8_t)(0x80U >> (idx & 7U));
    }
    instance->frame_bit++;

    if(instance->frame_bit >= CONTINENTAL_RKE_PAYLOAD_BITS) {
        subghz_protocol_continental_rke_process_frame(instance);

        instance->collecting = false;
        instance->preamble_shift = 0;
        instance->frame_bit = 0;
    }
}

static void
    subghz_protocol_continental_rke_restart(SubGhzProtocolDecoderContinentalRke* instance) {
    instance->te_accum = 0;
    instance->preamble_shift = 0;
    instance->collecting = false;
    instance->frame_bit = 0;
}

void* subghz_protocol_decoder_continental_rke_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderContinentalRke* instance =
        malloc(sizeof(SubGhzProtocolDecoderContinentalRke));
    instance->base.protocol = &subghz_protocol_continental_rke;
    instance->generic.protocol_name = subghz_protocol_continental_rke.name;
    subghz_protocol_continental_rke_restart(instance);
    return instance;
}

void subghz_protocol_decoder_continental_rke_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderContinentalRke* instance = context;
    subghz_protocol_continental_rke_restart(instance);
}

void subghz_protocol_decoder_continental_rke_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderContinentalRke* instance = context;

    if(!level && duration >= CONTINENTAL_RKE_TE_RESET) {
        subghz_protocol_continental_rke_restart(instance);
        return;
    }

    if(duration > CONTINENTAL_RKE_TE_MAX ||
       duration < (uint32_t)(CONTINENTAL_RKE_TE_SHORT - CONTINENTAL_RKE_TE_DELTA)) {
        subghz_protocol_continental_rke_restart(instance);
        return;
    }

    if(instance->te_accum + duration > CONTINENTAL_RKE_TE_THRESH) {

        subghz_protocol_continental_rke_add_bit(instance, level ? 1 : 0);
        instance->te_accum = 0;
    } else {

        instance->te_accum += duration;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_continental_rke_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderContinentalRke* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_continental_rke_const.min_count_bit_for_found);
}

