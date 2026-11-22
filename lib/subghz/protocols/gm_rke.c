#include "gm_rke.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"

#define TAG "GmRke"

#define GM_RKE_TE_SHORT      300
#define GM_RKE_TE_LONG       500
#define GM_RKE_SPACE_MIN     120
#define GM_RKE_BIT_THRESHOLD 400
#define GM_RKE_SPACE_MAX     900
#define GM_RKE_MARK_MIN      100
#define GM_RKE_MARK_MAX      1000

#define GM_RKE_FRAME_BITS 113
#define GM_RKE_PAYLOAD_BITS 112
#define GM_RKE_PAYLOAD_BYTES 14
#define GM_RKE_MIN_BITS 112

#define GM_RKE_MAX_BITS  256
#define GM_RKE_KEEP_BITS 160

static const SubGhzBlockConst subghz_protocol_gm_rke_const = {
    .te_short = GM_RKE_TE_SHORT,
    .te_long = GM_RKE_TE_LONG,
    .te_delta = 150,
    .min_count_bit_for_found = GM_RKE_MIN_BITS,
};

typedef enum {
    GmRkeDecoderStepReset = 0,
    GmRkeDecoderStepData,
} GmRkeDecoderStep;

struct SubGhzProtocolDecoderGmRke {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t bit_count;
    uint8_t bits[GM_RKE_MAX_BITS];

    uint8_t unknown_prefix;
    uint32_t encrypted;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderGmRke);

struct SubGhzProtocolEncoderGmRke {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderGmRke);

SubGhzProtocolStatus subghz_protocol_decoder_gm_rke_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderGmRke* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint8_t by[GM_RKE_PAYLOAD_BYTES];
    by[0] = 0xFF;
    by[1] = instance->unknown_prefix;
    uint8_t btn = (uint8_t)(instance->generic.btn & 0x7);
    by[2] = (uint8_t)((((16 - btn) & 0x0F) << 4) | btn);
    uint32_t id = instance->generic.serial;
    by[3] = (uint8_t)(id >> 24);
    by[4] = (uint8_t)(id >> 16);
    by[5] = (uint8_t)(id >> 8);
    by[6] = (uint8_t)id;
    uint32_t seq = instance->generic.cnt;
    by[7] = (uint8_t)(seq >> 16);
    by[8] = (uint8_t)(seq >> 8);
    by[9] = (uint8_t)seq;
    uint32_t enc = instance->encrypted;
    by[10] = (uint8_t)(enc >> 16);
    by[11] = (uint8_t)(enc >> 8);
    by[12] = (uint8_t)enc;
    int sum = 0;
    for(int i = 1; i < 13; i++) sum += by[i];
    by[13] = (uint8_t)((256 - (sum & 0xFF)) & 0xFF);

    uint8_t data[15];
    memset(data, 0, sizeof(data));
    for(int i = 0; i < GM_RKE_PAYLOAD_BITS; i++) {
        if((by[i >> 3] >> (7 - (i & 7))) & 1) data[i >> 3] |= (uint8_t)(0x80U >> (i & 7));
    }
    if(!flipper_format_write_hex(flipper_format, "Data", data, sizeof(data))) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

const SubGhzProtocolDecoder subghz_protocol_gm_rke_decoder = {
    .alloc = subghz_protocol_decoder_gm_rke_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_gm_rke_feed,
    .reset = subghz_protocol_decoder_gm_rke_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_gm_rke_serialize,
    .deserialize = subghz_protocol_decoder_gm_rke_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_gm_rke_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_recon_manchester");

const SubGhzProtocol subghz_protocol_gm_rke = {
    .name = GM_RKE_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_gm_rke_decoder,
    .encoder = &subghz_protocol_gm_rke_encoder_ext.vtable,
};

static void subghz_protocol_gm_rke_process_frame(SubGhzProtocolDecoderGmRke* instance) {
    if(instance->bit_count < GM_RKE_FRAME_BITS) return;

    uint16_t off = instance->bit_count - GM_RKE_FRAME_BITS;

    uint8_t bytes[GM_RKE_PAYLOAD_BYTES];
    for(uint16_t i = 0; i < GM_RKE_PAYLOAD_BYTES; i++) {
        uint8_t b = 0;
        for(uint16_t j = 0; j < 8; j++) {
            b = (uint8_t)((b << 1) | (instance->bits[off + i * 8 + j] & 1));
        }
        bytes[i] = b;
    }

    if(bytes[0] != 0xFF) return;

    int button_checksum = (bytes[2] >> 4) + (bytes[2] & 0x0F);
    if(button_checksum == 0 || (button_checksum & 0x0F) != 0) return;

    int full_checksum = 0;
    for(uint16_t i = 1; i < GM_RKE_PAYLOAD_BYTES; i++) {
        full_checksum += bytes[i];
    }
    if(full_checksum == 0 || (full_checksum & 0xFF) != 0) return;

    uint8_t button = bytes[2] & 0x7;
    uint32_t id = ((uint32_t)bytes[3] << 24) | ((uint32_t)bytes[4] << 16) |
                  ((uint32_t)bytes[5] << 8) | (uint32_t)bytes[6];
    uint32_t sequence = ((uint32_t)bytes[7] << 16) | ((uint32_t)bytes[8] << 8) | (uint32_t)bytes[9];
    uint32_t encrypted =
        ((uint32_t)bytes[10] << 16) | ((uint32_t)bytes[11] << 8) | (uint32_t)bytes[12];

    uint64_t packed = ((uint64_t)id << 27) | ((uint64_t)(sequence & 0xFFFFFF) << 3) |
                      (uint64_t)(button & 0x7);

    instance->generic.data = packed;
    instance->generic.data_count_bit = GM_RKE_FRAME_BITS;
    instance->generic.serial = id;
    instance->generic.btn = button;
    instance->generic.cnt = sequence;
    instance->unknown_prefix = bytes[1];
    instance->encrypted = encrypted;

    instance->decoder.decode_data = packed;
    instance->decoder.decode_count_bit = GM_RKE_PAYLOAD_BITS;

    FURI_LOG_D(
        TAG,
        "ID:%02X%08lX Btn:%X Seq:%06lX",
        bytes[1],
        (unsigned long)id,
        button,
        (unsigned long)sequence);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void* subghz_protocol_decoder_gm_rke_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderGmRke* instance = malloc(sizeof(SubGhzProtocolDecoderGmRke));
    instance->base.protocol = &subghz_protocol_gm_rke;
    instance->generic.protocol_name = subghz_protocol_gm_rke.name;
    return instance;
}

void subghz_protocol_decoder_gm_rke_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderGmRke* instance = context;
    instance->decoder.parser_step = GmRkeDecoderStepReset;
    instance->bit_count = 0;
}

static void subghz_protocol_gm_rke_add_bit(SubGhzProtocolDecoderGmRke* instance, uint8_t bit) {
    if(instance->bit_count >= GM_RKE_MAX_BITS) {

        uint16_t drop = instance->bit_count - GM_RKE_KEEP_BITS;
        for(uint16_t i = 0; i < GM_RKE_KEEP_BITS; i++) {
            instance->bits[i] = instance->bits[drop + i];
        }
        instance->bit_count = GM_RKE_KEEP_BITS;
    }
    instance->bits[instance->bit_count++] = bit & 1;
}

void subghz_protocol_decoder_gm_rke_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderGmRke* instance = context;

    switch(instance->decoder.parser_step) {
    case GmRkeDecoderStepReset:

        if(level && duration >= GM_RKE_MARK_MIN && duration <= GM_RKE_MARK_MAX) {
            instance->bit_count = 0;
            instance->decoder.parser_step = GmRkeDecoderStepData;
        }
        break;

    case GmRkeDecoderStepData:
        if(level) {

            if(duration < GM_RKE_MARK_MIN || duration > GM_RKE_MARK_MAX) {
                subghz_protocol_gm_rke_process_frame(instance);
                instance->bit_count = 0;
                instance->decoder.parser_step = GmRkeDecoderStepReset;
            }
        } else {

            if(duration > GM_RKE_SPACE_MAX) {
                subghz_protocol_gm_rke_process_frame(instance);
                instance->bit_count = 0;
                instance->decoder.parser_step = GmRkeDecoderStepReset;
            } else if(duration >= GM_RKE_SPACE_MIN) {
                uint8_t bit = (duration >= GM_RKE_BIT_THRESHOLD) ? 1 : 0;
                subghz_protocol_gm_rke_add_bit(instance, bit);
            } else {

                subghz_protocol_gm_rke_process_frame(instance);
                instance->bit_count = 0;
                instance->decoder.parser_step = GmRkeDecoderStepReset;
            }
        }
        break;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_gm_rke_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderGmRke* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_gm_rke_const.min_count_bit_for_found);
}

