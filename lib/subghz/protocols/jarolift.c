#include "jarolift.h"

#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"
#include "core/log.h"
#include "keeloq_common.h"

#include "../subghz_keystore.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocolJarolift"

static bool jarolift_raw_replay = false; // #16: TxRaw verbatim replay (freeze counter)

static const SubGhzBlockConst subghz_protocol_jarolift_const = {
    .te_short = 400,
    .te_long = 800,
    .te_delta = 167,
    .min_count_bit_for_found = 72,
};

struct SubGhzProtocolDecoderJarolift {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;
    SubGhzKeystore* keystore;
};

struct SubGhzProtocolEncoderJarolift {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    SubGhzKeystore* keystore;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderJarolift);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderJarolift);

typedef enum {
    JaroliftDecoderStepReset = 0,
    JaroliftDecoderStepCheckPreambula,
    JaroliftDecoderStepSaveDuration,
    JaroliftDecoderStepCheckDuration,
} JaroliftDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_jarolift_decoder = {
    .alloc = subghz_protocol_decoder_jarolift_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_jarolift_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_jarolift_get_hash_data,
    .serialize = subghz_protocol_decoder_jarolift_serialize,
    .deserialize = subghz_protocol_decoder_jarolift_deserialize,
    .get_string = subghz_protocol_decoder_jarolift_get_string,
};

/* host bridge: firmware functions/state the SD encoder uses (handed over at load) */
typedef struct {
    void* jarolift_raw_replay;
    __typeof__(&subghz_protocol_keeloq_common_decrypt) subghz_protocol_keeloq_common_decrypt;
    __typeof__(&subghz_protocol_keeloq_common_encrypt) subghz_protocol_keeloq_common_encrypt;
    __typeof__(&subghz_protocol_keeloq_common_normal_learning) subghz_protocol_keeloq_common_normal_learning;
} enc_jarolift_host_t;
static const enc_jarolift_host_t enc_jarolift_host = {
    .jarolift_raw_replay = (void*)&jarolift_raw_replay,
    .subghz_protocol_keeloq_common_decrypt = subghz_protocol_keeloq_common_decrypt,
    .subghz_protocol_keeloq_common_encrypt = subghz_protocol_keeloq_common_encrypt,
    .subghz_protocol_keeloq_common_normal_learning = subghz_protocol_keeloq_common_normal_learning,
};
static const SubGhzEncoderExternal subghz_protocol_jarolift_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_jarolift", &enc_jarolift_host);

const SubGhzProtocol subghz_protocol_jarolift = {
    .name = SUBGHZ_PROTOCOL_JAROLIFT_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,

    .decoder = &subghz_protocol_jarolift_decoder,
    .encoder = &subghz_protocol_jarolift_encoder_ext.vtable,
};

static void subghz_protocol_jarolift_remote_controller(
    SubGhzBlockGeneric* instance,
    SubGhzKeystore* keystore);


bool subghz_protocol_jarolift_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolEncoderJarolift* instance = context;
    instance->generic.serial = (serial & 0xFFFFF00);
    instance->generic.cnt = cnt;
    instance->generic.btn = btn;
    instance->generic.seed = 0x0100;
    instance->generic.data_count_bit = 72;

    uint32_t hop_decrypted = (uint64_t)((instance->generic.seed >> 8) & 0xFF) << 24 |
                             ((instance->generic.serial & 0xFF) << 16) |
                             (instance->generic.cnt & 0xFFFF);

    uint64_t hop_encrypted = 0;
    SUBGHZ_KEYSTORE_FOR_EACH(manufacture_code, instance->keystore) {
            if(manufacture_code->type == KEELOQ_LEARNING_NORMAL_JAROLIFT) {

                uint64_t man = subghz_protocol_keeloq_common_normal_learning(
                    instance->generic.serial, manufacture_code->key);
                hop_encrypted = subghz_protocol_keeloq_common_encrypt(hop_decrypted, man);
                break;
            }
        }

    uint64_t fix = (uint64_t)instance->generic.btn << 60 |
                   ((uint64_t)(instance->generic.serial & 0xFFFFFFF) << 32) | hop_encrypted;

    instance->generic.data = subghz_protocol_blocks_reverse_key(fix, 64);

    instance->generic.data_2 =
        subghz_protocol_blocks_reverse_key((instance->generic.seed & 0xFF), 8);

    SubGhzProtocolStatus res =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    uint8_t key_data[sizeof(uint64_t)] = {0};
    for(size_t i = 0; i < sizeof(uint64_t); i++) {
        key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data_2 >> (i * 8)) & 0xFF;
    }

    if(!flipper_format_rewind(flipper_format)) {
        FURI_LOG_E(TAG, "Rewind error");
        res = SubGhzProtocolStatusErrorParserOthers;
    }

    if((res == SubGhzProtocolStatusOk) &&
       !flipper_format_insert_or_update_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
        FURI_LOG_E(TAG, "Unable to add Data2");
        res = SubGhzProtocolStatusErrorParserOthers;
    }

    return res == SubGhzProtocolStatusOk;
}

void* subghz_protocol_decoder_jarolift_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolDecoderJarolift* instance = malloc(sizeof(SubGhzProtocolDecoderJarolift));
    instance->base.protocol = &subghz_protocol_jarolift;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->keystore = subghz_environment_get_keystore(environment);
    return instance;
}

void subghz_protocol_decoder_jarolift_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderJarolift* instance = context;

    switch(instance->decoder.parser_step) {
    case JaroliftDecoderStepReset:
        if((level) && DURATION_DIFF(duration, subghz_protocol_jarolift_const.te_short) <
                          subghz_protocol_jarolift_const.te_delta) {
            instance->decoder.parser_step = JaroliftDecoderStepCheckPreambula;
            instance->header_count++;
        }
        break;
    case JaroliftDecoderStepCheckPreambula:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_jarolift_const.te_short) <
                        subghz_protocol_jarolift_const.te_delta)) {
            instance->decoder.parser_step = JaroliftDecoderStepReset;
            break;
        }
        if((!level) && (instance->header_count == 8) &&
           (DURATION_DIFF(duration, subghz_protocol_jarolift_const.te_long * 5) <
            subghz_protocol_jarolift_const.te_delta * 6)) {

            instance->decoder.parser_step = JaroliftDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->header_count = 0;
            break;
        } else {
            instance->decoder.parser_step = JaroliftDecoderStepReset;
            instance->header_count = 0;
        }
        break;
    case JaroliftDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = JaroliftDecoderStepCheckDuration;
        } else {
            instance->header_count = 0;
            instance->decoder.parser_step = JaroliftDecoderStepReset;
        }
        break;
    case JaroliftDecoderStepCheckDuration:
        if(!level) {
            if(instance->decoder.decode_count_bit == 64) {
                instance->generic.data = instance->decoder.decode_data;
                instance->decoder.decode_data = 0;
            }
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_jarolift_const.te_short) <
                subghz_protocol_jarolift_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_jarolift_const.te_long) <
                subghz_protocol_jarolift_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = JaroliftDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_jarolift_const.te_long) <
                 subghz_protocol_jarolift_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_jarolift_const.te_short) <
                 subghz_protocol_jarolift_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = JaroliftDecoderStepSaveDuration;
            } else {
                if(duration >= ((uint32_t)subghz_protocol_jarolift_const.te_long * 3)) {

                    if((DURATION_DIFF(
                            instance->decoder.te_last, subghz_protocol_jarolift_const.te_long) <
                        subghz_protocol_jarolift_const.te_delta)) {
                        subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                    } else if((DURATION_DIFF(
                                   instance->decoder.te_last,
                                   subghz_protocol_jarolift_const.te_short) <
                               subghz_protocol_jarolift_const.te_delta)) {
                        subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                    }
                    if(instance->decoder.decode_count_bit ==
                       subghz_protocol_jarolift_const.min_count_bit_for_found) {
                        instance->generic.data_2 = instance->decoder.decode_data;
                        instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                    }

                    instance->decoder.parser_step = JaroliftDecoderStepReset;
                    instance->decoder.decode_data = 0;
                    instance->decoder.decode_count_bit = 0;
                    instance->header_count = 0;
                    break;
                }
                instance->decoder.parser_step = JaroliftDecoderStepReset;
                instance->header_count = 0;
            }
        } else {
            instance->decoder.parser_step = JaroliftDecoderStepReset;
            instance->header_count = 0;
        }
        break;
    }
}

static void subghz_protocol_jarolift_remote_controller(
    SubGhzBlockGeneric* instance,
    SubGhzKeystore* keystore) {

    uint32_t group = subghz_protocol_blocks_reverse_key(instance->data_2, 8);
    uint64_t key = subghz_protocol_blocks_reverse_key(instance->data, 64);
    bool ret = false;
    uint32_t decrypt = 0;
    instance->serial = (key >> 32) & 0xFFFFFFF;
    uint32_t hop = key & 0xFFFFFFFF;

    SUBGHZ_KEYSTORE_FOR_EACH(manufacture_code, keystore) {
            if(manufacture_code->type == KEELOQ_LEARNING_NORMAL_JAROLIFT) {
                uint64_t man = subghz_protocol_keeloq_common_normal_learning(
                    instance->serial, manufacture_code->key);
                decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                if(((decrypt >> 16) & 0xFF) == (instance->serial & 0xFF)) {
                    ret = true;
                }
                break;
            }
        }
    if(ret) {
        instance->btn = (key >> 60) & 0xF;
        instance->seed = ((decrypt >> 24) << 8) | (group >> 8);
        instance->cnt = decrypt & 0xFFFF;

        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(instance->btn);
        }
        subghz_custom_btn_set_max(3);
    } else {
        instance->btn = 0;
        instance->serial = 0;
        instance->cnt = 0;
        instance->seed = 0;
    }
}

uint8_t subghz_protocol_decoder_jarolift_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderJarolift* instance = context;
    uint8_t hash = 0;
    uint8_t* p = (uint8_t*)&instance->generic.data;
    for(size_t i = 0; i < 16; i++) {
        hash ^= p[i];
    }
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_jarolift_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderJarolift* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    uint8_t key_data[sizeof(uint64_t)] = {0};
    for(size_t i = 0; i < sizeof(uint64_t); i++) {
        key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data_2 >> (i * 8)) & 0xFF;
    }

    if(!flipper_format_rewind(flipper_format)) {
        FURI_LOG_E(TAG, "Rewind error");
        ret = SubGhzProtocolStatusErrorParserOthers;
    }

    if((ret == SubGhzProtocolStatusOk) &&
       !flipper_format_insert_or_update_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
        FURI_LOG_E(TAG, "Unable to add Data");
        ret = SubGhzProtocolStatusErrorParserOthers;
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_jarolift_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderJarolift* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_jarolift_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(!flipper_format_read_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Missing Data");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }

        for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
            instance->generic.data_2 = instance->generic.data_2 << 8 | key_data[i];
        }
    } while(false);
    return ret;
}

void subghz_protocol_decoder_jarolift_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderJarolift* instance = context;
    // Detect/decode split (FoxFW2.0 v2.1): KeeLoq decrypt needs the live keystore,
    // so it stays internal; the SD plugin only formats.
    subghz_protocol_jarolift_remote_controller(&instance->generic, instance->keystore);
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}

