#include "ducato_v0.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"

#define TAG "DucatoV0"

#define DUCATO_V0_PREAMBLE_MIN 120
#define DUCATO_V0_TE_SYNC      800
#define DUCATO_V0_DATA_BITS    71

static const SubGhzBlockConst subghz_protocol_ducato_v0_const = {
    .te_short = 200,
    .te_long = 400,
    .te_delta = 99,
    .min_count_bit_for_found = DUCATO_V0_DATA_BITS,
};

static const uint8_t ducato_manchester_table[4] = {0x01, 0x91, 0x9B, 0xFB};
static const uint8_t ducato_parity_table[16] = {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0};
static const uint8_t ducato_crc_masks[5] = {0xB3, 0x80, 0x83, 0x22, 0x73};

struct SubGhzProtocolDecoderDucatoV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    uint32_t te_last;
    uint16_t preamble_count;
    uint8_t man_state;

    uint64_t data_lo;
    uint32_t data_hi;
    uint16_t bit_count;

    uint32_t fix;
    uint32_t hop;
    uint32_t serial;
    uint8_t btn;
    uint8_t crc;
    bool crc_ok;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderDucatoV0);

typedef enum {
    DucatoV0StepReset = 0,
    DucatoV0StepPreamble = 1,
    DucatoV0StepData = 2,
} DucatoV0Step;

static bool ducato_v0_check_crc(uint64_t key64, uint8_t crc7) {
    uint8_t b[8];
    for(uint8_t i = 0; i < 8; i++) b[i] = (uint8_t)(key64 >> (8 * (7 - i)));
    uint8_t acc = (uint8_t)(
        (b[0] & ducato_crc_masks[0]) ^ (b[1] & ducato_crc_masks[1]) ^
        (b[2] & ducato_crc_masks[2]) ^ (b[3] & ducato_crc_masks[3]) ^
        (b[5] & ducato_crc_masks[4]));
    uint8_t parity = ducato_parity_table[acc >> 4] ^ ducato_parity_table[acc & 0x0F];
    return (crc7 & 0x01U) == parity;
}

static bool ducato_man_decode(SubGhzProtocolDecoderDucatoV0* instance, uint8_t timing_type, uint8_t* bit) {
    uint8_t s = instance->man_state;
    if(s >= 4) {
        instance->man_state = 1;
        return false;
    }
    uint8_t ns = (ducato_manchester_table[s] >> timing_type) & 0x3;
    if(s == ns) return false;
    instance->man_state = ns;
    if(ns == 2) {
        *bit = 0;
        return true;
    }
    if(ns == 1) {
        *bit = 1;
        return true;
    }
    return false;
}

static void ducato_v0_finish(SubGhzProtocolDecoderDucatoV0* instance) {

    uint64_t key = ((uint64_t)instance->data_hi << 57) | (instance->data_lo >> 7);
    uint8_t crc = (uint8_t)(instance->data_lo & 0x7F);

    uint32_t fix = (uint32_t)(key & 0xFFFFFFFFU);
    uint32_t hop = (uint32_t)(key >> 32);
    uint32_t serial = (fix >> 8) & 0x00FFFFFFU;
    uint8_t btn = (uint8_t)(fix & 0xFFU);

    instance->parser_step = DucatoV0StepReset;
    instance->preamble_count = 0;
    instance->man_state = 1;
    instance->data_lo = 0;
    instance->data_hi = 0;
    instance->bit_count = 0;

    if(serial == 0x000000U || serial == 0xFFFFFFU || serial == 0x555555U ||
       serial == 0xAAAAAAU) {
        return;
    }

    instance->fix = fix;
    instance->hop = hop;
    instance->serial = serial;
    instance->btn = btn;
    instance->crc = crc;
    instance->crc_ok = ducato_v0_check_crc(key, crc);

    instance->generic.data = key;
    instance->generic.data_count_bit = DUCATO_V0_DATA_BITS;
    instance->generic.serial = serial;
    instance->generic.btn = btn;
    instance->generic.cnt = 0;
    instance->decoder.decode_data = key;
    instance->decoder.decode_count_bit = DUCATO_V0_DATA_BITS;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_ducato_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderDucatoV0* instance = calloc(1, sizeof(SubGhzProtocolDecoderDucatoV0));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_ducato_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->man_state = 1;
    return instance;
}

void subghz_protocol_decoder_ducato_v0_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderDucatoV0* instance = context;
    instance->parser_step = DucatoV0StepReset;
    instance->te_last = 0;
    instance->preamble_count = 0;
    instance->man_state = 1;
    instance->data_lo = 0;
    instance->data_hi = 0;
    instance->bit_count = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_ducato_v0_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderDucatoV0* instance = context;
    uint32_t ts = subghz_protocol_ducato_v0_const.te_short;
    uint32_t tl = subghz_protocol_ducato_v0_const.te_long;
    uint32_t d = subghz_protocol_ducato_v0_const.te_delta;

    switch(instance->parser_step) {
    case DucatoV0StepReset:
        if(level && DURATION_DIFF(duration, ts) <= d) {
            instance->parser_step = DucatoV0StepPreamble;
            instance->te_last = duration;
            instance->preamble_count = 1;
            instance->data_lo = 0;
            instance->data_hi = 0;
            instance->bit_count = 0;
            instance->man_state = 1;
        }
        break;

    case DucatoV0StepPreamble:
        if(DURATION_DIFF(duration, ts) <= d) {
            if(DURATION_DIFF(instance->te_last, ts) <= d) {
                instance->preamble_count++;
            } else {
                instance->parser_step = DucatoV0StepReset;
                instance->preamble_count = 0;
            }
            instance->te_last = duration;
        } else if(DURATION_DIFF(duration, DUCATO_V0_TE_SYNC) <= d) {
            if(instance->preamble_count >= DUCATO_V0_PREAMBLE_MIN) {
                instance->parser_step = DucatoV0StepData;
                instance->preamble_count = 0;
                instance->man_state = 1;
                instance->data_lo = 0;
                instance->data_hi = 0;
                instance->bit_count = 0;
            } else {
                instance->parser_step = DucatoV0StepReset;
                instance->preamble_count = 0;
            }
        } else {
            instance->parser_step = DucatoV0StepReset;
            instance->preamble_count = 0;
        }
        break;

    case DucatoV0StepData: {
        uint8_t timing_type = 0xFF;
        if(DURATION_DIFF(duration, ts) <= d) {
            timing_type = level ? 0 : 2;
        } else if(DURATION_DIFF(duration, tl) <= d) {
            timing_type = level ? 4 : 6;
        }

        if(timing_type != 0xFF) {
            uint8_t bit = 0;
            if(ducato_man_decode(instance, timing_type, &bit)) {
                uint32_t carry = (uint32_t)(instance->data_lo >> 63);
                instance->data_lo = (instance->data_lo << 1) | bit;
                instance->data_hi = (instance->data_hi << 1) | carry;
                instance->bit_count++;
                if(instance->bit_count == DUCATO_V0_DATA_BITS) {
                    ducato_v0_finish(instance);
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_ducato_v0_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderDucatoV0* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t serial = instance->serial;
    uint32_t btn = instance->btn;
    uint32_t hop = instance->hop;
    uint32_t crc = instance->crc;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "Btn", &btn, 1) ||
       !flipper_format_write_uint32(flipper_format, "Hop", &hop, 1) ||
       !flipper_format_write_uint32(flipper_format, "CRC", &crc, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint8_t fdata[9];
    uint64_t g = instance->generic.data;
    for(uint8_t b = 0; b < 8; b++) fdata[b] = (uint8_t)(g >> (56 - b * 8));
    fdata[8] = (uint8_t)((instance->crc & 0x7F) << 1);
    if(!flipper_format_write_hex(flipper_format, "Data", fdata, sizeof(fdata))) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_ducato_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderDucatoV0* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_ducato_v0_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint64_t key = instance->generic.data;
    instance->fix = (uint32_t)(key & 0xFFFFFFFFU);
    instance->hop = (uint32_t)(key >> 32);
    instance->serial = (instance->fix >> 8) & 0x00FFFFFFU;
    instance->btn = (uint8_t)(instance->fix & 0xFFU);

    uint32_t crc_u = 0;
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "CRC", &crc_u, 1)) {
        instance->crc = (uint8_t)(crc_u & 0x7FU);
    } else {
        instance->crc = 0;
    }
    instance->crc_ok = ducato_v0_check_crc(key, instance->crc);

    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->btn;
    instance->generic.cnt = 0;
    instance->decoder.decode_data = key;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_ducato_v0_decoder = {
    .alloc = subghz_protocol_decoder_ducato_v0_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_ducato_v0_feed,
    .reset = subghz_protocol_decoder_ducato_v0_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_ducato_v0_serialize,
    .deserialize = subghz_protocol_decoder_ducato_v0_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_ducato_v0_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_recon_manchester");

const SubGhzProtocol subghz_protocol_ducato_v0 = {
    .name = DUCATO_V0_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_ducato_v0_decoder,
    .encoder = &subghz_protocol_ducato_v0_encoder_ext.vtable,
};
