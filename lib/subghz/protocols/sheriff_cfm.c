#include "sheriff_cfm.h"

#include "common.h"
#include "../encode_plugin.h"
#include "keeloq_common.h"
#include "../blocks/custom_btn_i.h"
#include "../blocks/generic.h"

#include <string.h>

#define TAG "SubGhzProtocolSheriffCfm"

static const uint8_t cfm_pi_bytes[] = {
    0xA4, 0x58, 0xFE, 0xA3, 0xF4, 0x93, 0x3D, 0x7E,
    0x0D, 0x95, 0x74, 0x8F, 0x72, 0x8E, 0xB6, 0x58,
};

static const uint8_t cfm_zx750_encoded[8] = {
    0x32, 0x4D, 0xCB, 0x84, 0x5F, 0xE9, 0x27, 0xCB,
};

static const uint8_t cfm_zx930_encoded[8] = {
    0x94, 0x3B, 0x63, 0xA5, 0xE8, 0xF3, 0xAB, 0x60,
};

static void cfm_pi_decode(uint8_t* out, const uint8_t* encoded, size_t pi_offset, size_t len) {
    for(size_t i = 0; i < len; i++) {
        out[i] = encoded[i] ^ cfm_pi_bytes[pi_offset + i];
    }
}

static uint8_t cfm_rlf(uint8_t in) {
    return (uint8_t)((in << 1) | (in >> 7));
}

static uint8_t cfm_rrf(uint8_t in) {
    return (uint8_t)((in >> 1) | (in << 7));
}

static uint8_t cfm_swap(uint8_t in) {
    return (uint8_t)((in << 4) | (in >> 4));
}

typedef enum {
    SheriffCfmModelZX750 = 0,
    SheriffCfmModelZX930 = 1,
    SheriffCfmModelCount = 2,
} SheriffCfmModel;

static void cfm_decrypt_transform(uint8_t* hop, SheriffCfmModel model) {
    uint8_t temp;
    switch(model) {
    case SheriffCfmModelZX750:
        hop[0] = cfm_swap(hop[0]);
        hop[2] = cfm_swap(hop[2]);
        break;

    case SheriffCfmModelZX930:
        hop[0] = ~hop[0];
        temp = hop[1];
        hop[1] = hop[2];
        hop[2] = temp;
        hop[0] = cfm_rrf(hop[0]);
        hop[1] = cfm_swap(hop[1]);
        hop[1] = cfm_rlf(hop[1]);
        hop[1] = cfm_rlf(hop[1]);
        break;

    default:
        break;
    }
}

static const SubGhzBlockConst subghz_protocol_sheriff_cfm_const = {
    .te_short = 400,
    .te_long = 800,
    .te_delta = 140,
    .min_count_bit_for_found = 64,
};

struct SubGhzProtocolDecoderSheriffCfm {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
    SheriffCfmModel model;
};

struct SubGhzProtocolEncoderSheriffCfm {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    SheriffCfmModel model;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderSheriffCfm);


SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderSheriffCfm);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderSheriffCfm);

typedef enum {
    SheriffCfmDecoderStepReset = 0,
    SheriffCfmDecoderStepCheckPreambula,
    SheriffCfmDecoderStepSaveDuration,
    SheriffCfmDecoderStepCheckDuration,
} SheriffCfmDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_sheriff_cfm_decoder = {
    .alloc = subghz_protocol_decoder_sheriff_cfm_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_sheriff_cfm_feed,
    .reset = subghz_protocol_decoder_sheriff_cfm_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_sheriff_cfm_serialize,
    .deserialize = subghz_protocol_decoder_sheriff_cfm_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

/* host bridge: firmware functions/state the SD encoder uses (handed over at load) */
typedef struct {
    __typeof__(&subghz_protocol_keeloq_common_decrypt) subghz_protocol_keeloq_common_decrypt;
    __typeof__(&subghz_protocol_keeloq_common_encrypt) subghz_protocol_keeloq_common_encrypt;
} enc_sheriff_cfm_host_t;
static const enc_sheriff_cfm_host_t enc_sheriff_cfm_host = {
    .subghz_protocol_keeloq_common_decrypt = subghz_protocol_keeloq_common_decrypt,
    .subghz_protocol_keeloq_common_encrypt = subghz_protocol_keeloq_common_encrypt,
};
static const SubGhzEncoderExternal subghz_protocol_sheriff_cfm_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_sheriff_cfm", &enc_sheriff_cfm_host);

const SubGhzProtocol subghz_protocol_sheriff_cfm = {
    .name = SUBGHZ_PROTOCOL_SHERIFF_CFM_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_sheriff_cfm_decoder,
    .encoder = &subghz_protocol_sheriff_cfm_encoder_ext.vtable,
};

static uint64_t cfm_get_mfkey(SheriffCfmModel model) {
    uint8_t dkey[8];
    switch(model) {
    case SheriffCfmModelZX750:
        cfm_pi_decode(dkey, cfm_zx750_encoded, 0, 8);
        break;
    case SheriffCfmModelZX930:
        cfm_pi_decode(dkey, cfm_zx930_encoded, 8, 8);
        break;
    default:
        return 0;
    }
    uint64_t key = 0;
    for(int i = 7; i >= 0; i--) {
        key = (key << 8) | dkey[i];
    }
    return key;
}

static bool cfm_try_decrypt(
    uint64_t data,
    SheriffCfmModel* out_model,
    uint8_t* out_btn,
    uint32_t* out_serial,
    uint16_t* out_cnt) {

    uint32_t hop_encrypted = (uint32_t)(data & 0xFFFFFFFF);
    uint32_t fix = (uint32_t)(data >> 32);

    for(SheriffCfmModel model = 0; model < SheriffCfmModelCount; model++) {
        uint8_t hop_bytes[4];
        hop_bytes[0] = (uint8_t)(hop_encrypted & 0xFF);
        hop_bytes[1] = (uint8_t)((hop_encrypted >> 8) & 0xFF);
        hop_bytes[2] = (uint8_t)((hop_encrypted >> 16) & 0xFF);
        hop_bytes[3] = (uint8_t)((hop_encrypted >> 24) & 0xFF);

        cfm_decrypt_transform(hop_bytes, model);

        uint32_t hop_transformed =
            (uint32_t)hop_bytes[0] |
            ((uint32_t)hop_bytes[1] << 8) |
            ((uint32_t)hop_bytes[2] << 16) |
            ((uint32_t)hop_bytes[3] << 24);

        uint64_t mfkey = cfm_get_mfkey(model);
        uint32_t decrypted = subghz_protocol_keeloq_common_decrypt(hop_transformed, mfkey);

        uint16_t dec_serial_lo = (uint16_t)((decrypted >> 16) & 0x3FF);
        uint16_t fix_serial_lo = (uint16_t)(fix & 0x3FF);

        uint8_t btn_byte = (uint8_t)((decrypted >> 24) & 0xFF);
        bool valid_btn = (btn_byte == 0x10 || btn_byte == 0x20 ||
                          btn_byte == 0x40 || btn_byte == 0x80);

        if(valid_btn && (dec_serial_lo == fix_serial_lo)) {
            *out_model = model;
            *out_btn = btn_byte;
            *out_serial = fix;
            *out_cnt = (uint16_t)(decrypted & 0xFFFF);
            return true;
        }
    }
    return false;
}

void* subghz_protocol_decoder_sheriff_cfm_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderSheriffCfm), &subghz_protocol_sheriff_cfm);
}



void subghz_protocol_decoder_sheriff_cfm_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderSheriffCfm* instance = context;
    instance->decoder.parser_step = SheriffCfmDecoderStepReset;
    instance->header_count = 0;
}

void subghz_protocol_decoder_sheriff_cfm_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderSheriffCfm* instance = context;

    switch(instance->decoder.parser_step) {
    case SheriffCfmDecoderStepReset:
        if((level) && DURATION_DIFF(duration, subghz_protocol_sheriff_cfm_const.te_short) <
                          subghz_protocol_sheriff_cfm_const.te_delta) {
            instance->decoder.parser_step = SheriffCfmDecoderStepCheckPreambula;
            instance->header_count++;
        }
        break;

    case SheriffCfmDecoderStepCheckPreambula:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_sheriff_cfm_const.te_short) <
                        subghz_protocol_sheriff_cfm_const.te_delta)) {
            instance->decoder.parser_step = SheriffCfmDecoderStepReset;
            break;
        }
        if((instance->header_count > 2) &&
           (DURATION_DIFF(duration, subghz_protocol_sheriff_cfm_const.te_short * 10) <
            subghz_protocol_sheriff_cfm_const.te_delta * 10)) {
            instance->decoder.parser_step = SheriffCfmDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        } else {
            instance->decoder.parser_step = SheriffCfmDecoderStepReset;
            instance->header_count = 0;
        }
        break;

    case SheriffCfmDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = SheriffCfmDecoderStepCheckDuration;
        }
        break;

    case SheriffCfmDecoderStepCheckDuration:
        if(!level) {
            if(duration >= ((uint32_t)subghz_protocol_sheriff_cfm_const.te_short * 2 +
                            subghz_protocol_sheriff_cfm_const.te_delta)) {
                instance->decoder.parser_step = SheriffCfmDecoderStepReset;
                if((instance->decoder.decode_count_bit >=
                    subghz_protocol_sheriff_cfm_const.min_count_bit_for_found) &&
                   (instance->decoder.decode_count_bit <=
                    subghz_protocol_sheriff_cfm_const.min_count_bit_for_found + 2)) {
                    SheriffCfmModel model;
                    uint8_t btn;
                    uint32_t serial;
                    uint16_t cnt;
                    if(cfm_try_decrypt(
                           instance->decoder.decode_data, &model, &btn, &serial, &cnt)) {
                        instance->generic.data = instance->decoder.decode_data;
                        instance->generic.data_count_bit =
                            subghz_protocol_sheriff_cfm_const.min_count_bit_for_found;
                        instance->model = model;
                        instance->generic.btn = btn;
                        instance->generic.serial = serial;
                        instance->generic.cnt = cnt;
                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                    }
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->header_count = 0;
                break;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_sheriff_cfm_const.te_short) <
                 subghz_protocol_sheriff_cfm_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_sheriff_cfm_const.te_long) <
                 subghz_protocol_sheriff_cfm_const.te_delta * 2)) {
                if(instance->decoder.decode_count_bit <
                   subghz_protocol_sheriff_cfm_const.min_count_bit_for_found) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                } else {
                    instance->decoder.decode_count_bit++;
                }
                instance->decoder.parser_step = SheriffCfmDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_sheriff_cfm_const.te_long) <
                 subghz_protocol_sheriff_cfm_const.te_delta * 2) &&
                (DURATION_DIFF(duration, subghz_protocol_sheriff_cfm_const.te_short) <
                 subghz_protocol_sheriff_cfm_const.te_delta)) {
                if(instance->decoder.decode_count_bit <
                   subghz_protocol_sheriff_cfm_const.min_count_bit_for_found) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                } else {
                    instance->decoder.decode_count_bit++;
                }
                instance->decoder.parser_step = SheriffCfmDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = SheriffCfmDecoderStepReset;
                instance->header_count = 0;
            }
        } else {
            instance->decoder.parser_step = SheriffCfmDecoderStepReset;
            instance->header_count = 0;
        }
        break;
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_sheriff_cfm_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderSheriffCfm* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        uint32_t model = (uint32_t)instance->model;
        flipper_format_insert_or_update_uint32(flipper_format, "Model", &model, 1);
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_sheriff_cfm_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderSheriffCfm* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_sheriff_cfm_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) break;

        uint32_t model_temp = 0;
        if(flipper_format_read_uint32(flipper_format, "Model", &model_temp, 1)) {
            instance->model = (SheriffCfmModel)model_temp;
        }

        SheriffCfmModel detected;
        uint8_t btn;
        uint32_t serial;
        uint16_t cnt;
        if(cfm_try_decrypt(instance->generic.data, &detected, &btn, &serial, &cnt)) {
            instance->model = detected;
            instance->generic.btn = btn;
            instance->generic.serial = serial;
            instance->generic.cnt = cnt;
        }
    } while(false);

    return ret;
}

