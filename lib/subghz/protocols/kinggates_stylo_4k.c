#include "kinggates_stylo_4k.h"

#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"
#include "keeloq_common.h"

#include "../subghz_keystore.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocoKingGatesStylo4k"

static bool kinggates_stylo_4k_raw_replay = false; // #16: TxRaw verbatim replay (freeze counter)

static const SubGhzBlockConst subghz_protocol_kinggates_stylo_4k_const = {
    .te_short = 400,
    .te_long = 1100,
    .te_delta = 140,
    .min_count_bit_for_found = 89,
};

struct SubGhzProtocolDecoderKingGates_stylo_4k {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;
    SubGhzKeystore* keystore;
};

struct SubGhzProtocolEncoderKingGates_stylo_4k {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    SubGhzKeystore* keystore;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderKingGates_stylo_4k);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderKingGates_stylo_4k);

typedef enum {
    KingGates_stylo_4kDecoderStepReset = 0,
    KingGates_stylo_4kDecoderStepCheckPreambula,
    KingGates_stylo_4kDecoderStepCheckStartBit,
    KingGates_stylo_4kDecoderStepSaveDuration,
    KingGates_stylo_4kDecoderStepCheckDuration,
} KingGates_stylo_4kDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_kinggates_stylo_4k_decoder = {
    .alloc = subghz_protocol_decoder_kinggates_stylo_4k_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_kinggates_stylo_4k_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_kinggates_stylo_4k_serialize,
    .deserialize = subghz_protocol_decoder_kinggates_stylo_4k_deserialize,
    .get_string = subghz_protocol_decoder_kinggates_stylo_4k_get_string,
};

/* host bridge: firmware functions/state the SD encoder uses (handed over at load) */
typedef struct {
    void* kinggates_stylo_4k_raw_replay;
    __typeof__(&subghz_protocol_keeloq_common_decrypt) subghz_protocol_keeloq_common_decrypt;
    __typeof__(&subghz_protocol_keeloq_common_encrypt) subghz_protocol_keeloq_common_encrypt;
} enc_kinggates_stylo_4k_host_t;
static const enc_kinggates_stylo_4k_host_t enc_kinggates_stylo_4k_host = {
    .kinggates_stylo_4k_raw_replay = (void*)&kinggates_stylo_4k_raw_replay,
    .subghz_protocol_keeloq_common_decrypt = subghz_protocol_keeloq_common_decrypt,
    .subghz_protocol_keeloq_common_encrypt = subghz_protocol_keeloq_common_encrypt,
};
static const SubGhzEncoderExternal subghz_protocol_kinggates_stylo_4k_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_kinggates_stylo_4k", &enc_kinggates_stylo_4k_host);

const SubGhzProtocol subghz_protocol_kinggates_stylo_4k = {
    .name = SUBGHZ_PROTOCOL_KINGGATES_STYLO_4K_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,

    .decoder = &subghz_protocol_kinggates_stylo_4k_decoder,
    .encoder = &subghz_protocol_kinggates_stylo_4k_encoder_ext.vtable,
};

static void subghz_protocol_kinggates_stylo_4k_remote_controller(
    SubGhzBlockGeneric* instance,
    SubGhzKeystore* keystore);


bool subghz_protocol_kinggates_stylo_4k_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolEncoderKingGates_stylo_4k* instance = context;
    instance->generic.serial = serial;
    instance->generic.cnt = cnt;
    instance->generic.btn = btn;
    instance->generic.data_count_bit = 89;

    uint32_t decrypt = instance->generic.btn << 28 | (((uint32_t)0x0C) << 24) |
                       ((instance->generic.serial & 0xFF) << 16) |
                       (instance->generic.cnt & 0xFFFF);

    uint64_t encrypt = 0;
    SUBGHZ_KEYSTORE_FOR_EACH(manufacture_code, instance->keystore) {
            if(manufacture_code->type == KEELOQ_LEARNING_SIMPLE_KINGGATES) {

                encrypt = subghz_protocol_keeloq_common_encrypt(decrypt, manufacture_code->key);
                encrypt = subghz_protocol_blocks_reverse_key(encrypt, 32);
                instance->generic.data_2 = encrypt << 4;
                break;
            }
        }

    uint64_t fix = ((uint64_t)((instance->generic.serial >> 16) & 0xFFFF) << 21) |
                   instance->generic.btn << 17 | 0b1 << 16 | (instance->generic.serial & 0xFFFF);

    instance->generic.data = subghz_protocol_blocks_reverse_key(fix, 53);

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

void* subghz_protocol_decoder_kinggates_stylo_4k_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolDecoderKingGates_stylo_4k* instance =
        malloc(sizeof(SubGhzProtocolDecoderKingGates_stylo_4k));
    instance->base.protocol = &subghz_protocol_kinggates_stylo_4k;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->keystore = subghz_environment_get_keystore(environment);
    return instance;
}

void subghz_protocol_decoder_kinggates_stylo_4k_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderKingGates_stylo_4k* instance = context;

    switch(instance->decoder.parser_step) {
    case KingGates_stylo_4kDecoderStepReset:
        if((level) && DURATION_DIFF(duration, subghz_protocol_kinggates_stylo_4k_const.te_short) <
                          subghz_protocol_kinggates_stylo_4k_const.te_delta) {
            instance->decoder.parser_step = KingGates_stylo_4kDecoderStepCheckPreambula;
            instance->header_count++;
        }
        break;
    case KingGates_stylo_4kDecoderStepCheckPreambula:
        if((!level) &&
           (DURATION_DIFF(duration, subghz_protocol_kinggates_stylo_4k_const.te_short) <
            subghz_protocol_kinggates_stylo_4k_const.te_delta)) {
            instance->decoder.parser_step = KingGates_stylo_4kDecoderStepReset;
            break;
        }
        if((instance->header_count > 2) &&
           (DURATION_DIFF(duration, subghz_protocol_kinggates_stylo_4k_const.te_long * 2) <
            subghz_protocol_kinggates_stylo_4k_const.te_delta * 2)) {

            instance->decoder.parser_step = KingGates_stylo_4kDecoderStepCheckStartBit;
        } else {
            instance->decoder.parser_step = KingGates_stylo_4kDecoderStepReset;
            instance->header_count = 0;
        }
        break;
    case KingGates_stylo_4kDecoderStepCheckStartBit:
        if((level) &&
           DURATION_DIFF(duration, subghz_protocol_kinggates_stylo_4k_const.te_short * 2) <
               subghz_protocol_kinggates_stylo_4k_const.te_delta * 2) {
            instance->decoder.parser_step = KingGates_stylo_4kDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->header_count = 0;
        }
        break;
    case KingGates_stylo_4kDecoderStepSaveDuration:
        if(!level) {
            if(duration >= ((uint32_t)subghz_protocol_kinggates_stylo_4k_const.te_long * 3)) {
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_kinggates_stylo_4k_const.min_count_bit_for_found) {
                    instance->generic.data = instance->generic.data_2;
                    instance->generic.data_2 = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }

                instance->decoder.parser_step = KingGates_stylo_4kDecoderStepReset;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->header_count = 0;
                break;
            } else {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = KingGates_stylo_4kDecoderStepCheckDuration;
            }
        } else {
            instance->decoder.parser_step = KingGates_stylo_4kDecoderStepReset;
            instance->header_count = 0;
        }
        break;
    case KingGates_stylo_4kDecoderStepCheckDuration:
        if(level) {
            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_kinggates_stylo_4k_const.te_short) <
                subghz_protocol_kinggates_stylo_4k_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_kinggates_stylo_4k_const.te_long) <
                subghz_protocol_kinggates_stylo_4k_const.te_delta * 2)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = KingGates_stylo_4kDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_kinggates_stylo_4k_const.te_long) <
                 subghz_protocol_kinggates_stylo_4k_const.te_delta * 2) &&
                (DURATION_DIFF(duration, subghz_protocol_kinggates_stylo_4k_const.te_short) <
                 subghz_protocol_kinggates_stylo_4k_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = KingGates_stylo_4kDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = KingGates_stylo_4kDecoderStepReset;
                instance->header_count = 0;
            }
            if(instance->decoder.decode_count_bit == 53) {
                instance->generic.data_2 = instance->decoder.decode_data;
                instance->decoder.decode_data = 0;
            }
        } else {
            instance->decoder.parser_step = KingGates_stylo_4kDecoderStepReset;
            instance->header_count = 0;
        }
        break;
    }
}

static void subghz_protocol_kinggates_stylo_4k_remote_controller(
    SubGhzBlockGeneric* instance,
    SubGhzKeystore* keystore) {

    uint32_t hop = subghz_protocol_blocks_reverse_key(instance->data_2 >> 4, 32);
    uint64_t fix = subghz_protocol_blocks_reverse_key(instance->data, 53);
    bool ret = false;
    uint32_t decrypt = 0;
    instance->btn = (fix >> 17) & 0x0F;
    instance->serial = ((fix >> 5) & 0xFFFF0000) | (fix & 0xFFFF);

    SUBGHZ_KEYSTORE_FOR_EACH(manufacture_code, keystore) {
            if(manufacture_code->type == KEELOQ_LEARNING_SIMPLE_KINGGATES) {
                decrypt = subghz_protocol_keeloq_common_decrypt(hop, manufacture_code->key);
                if(((decrypt >> 28) == instance->btn) && (((decrypt >> 24) & 0x0F) == 0x0C) &&
                   (((decrypt >> 16) & 0xFF) == (instance->serial & 0xFF))) {
                    ret = true;
                    break;
                }
            }
        }
    if(ret) {
        instance->cnt = decrypt & 0xFFFF;

        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(instance->btn);
        }
        subghz_custom_btn_set_max(3);
    } else {
        instance->btn = 0;
        instance->serial = 0;
        instance->cnt = 0;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_kinggates_stylo_4k_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderKingGates_stylo_4k* instance = context;
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

SubGhzProtocolStatus subghz_protocol_decoder_kinggates_stylo_4k_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderKingGates_stylo_4k* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_kinggates_stylo_4k_const.min_count_bit_for_found);
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

void subghz_protocol_decoder_kinggates_stylo_4k_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderKingGates_stylo_4k* instance = context;
    // Detect/decode split (FoxFW2.0 v2.1): KeeLoq decrypt needs the live keystore,
    // so it stays internal; the SD plugin only formats.
    subghz_protocol_kinggates_stylo_4k_remote_controller(&instance->generic, instance->keystore);
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}

