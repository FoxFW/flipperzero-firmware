#include "beninca_arc.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "core/log.h"
#include <stddef.h>
#include <stdint.h>
#include <furi_hal_crypto.h>

#include "../blocks/custom_btn_i.h"

#define TAG "BenincaARC"

static bool beninca_arc_raw_replay = false; // #16: TxRaw verbatim replay (freeze counter)

#define BENINCA_ARC_KEY_TYPE 9u

static const SubGhzBlockConst subghz_protocol_beninca_arc_const = {
    .te_short = 300,
    .te_long = 600,
    .te_delta = 155,
    .min_count_bit_for_found = 128,
};

typedef enum {
    BenincaARCDecoderStart = 0,
    BenincaARCDecoderHighLevel,
    BenincaARCDecoderLowLevel,
} BenincaARCDecoderState;

struct SubGhzProtocolDecoderBenincaARC {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;

    SubGhzBlockGeneric generic;

    SubGhzKeystore* keystore;
};

struct SubGhzProtocolEncoderBenincaARC {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;

    SubGhzBlockGeneric generic;

    SubGhzKeystore* keystore;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderBenincaARC);


SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderBenincaARC);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderBenincaARC);

const SubGhzProtocolDecoder subghz_protocol_beninca_arc_decoder = {
    .alloc = subghz_protocol_decoder_beninca_arc_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_beninca_arc_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_beninca_arc_serialize,
    .deserialize = subghz_protocol_decoder_beninca_arc_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

/* host bridge: firmware functions/state the SD encoder uses (handed over at load) */
typedef struct {
    void* beninca_arc_raw_replay;
} enc_beninca_arc_host_t;
static const enc_beninca_arc_host_t enc_beninca_arc_host = {
    .beninca_arc_raw_replay = (void*)&beninca_arc_raw_replay,
};
static const SubGhzEncoderExternal subghz_protocol_beninca_arc_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_beninca_arc", &enc_beninca_arc_host);

const SubGhzProtocol subghz_protocol_beninca_arc = {
    .name = SUBGHZ_PROTOCOL_BENINCA_ARC_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,

    .decoder = &subghz_protocol_beninca_arc_decoder,
    .encoder = &subghz_protocol_beninca_arc_encoder_ext.vtable,
};

static void get_subghz_protocol_beninca_arc_aes_key(SubGhzKeystore* keystore, uint8_t* aes_key) {
    uint64_t mfkey = 0;
    SUBGHZ_KEYSTORE_FOR_EACH(manufacture_code, keystore) {
            if(manufacture_code->type == BENINCA_ARC_KEY_TYPE) {
                mfkey = manufacture_code->key;
                break;
            }
        }

    uint32_t derived_lo = (uint32_t)(mfkey & 0xFFFFFFFF);
    uint32_t derived_hi = (uint32_t)((mfkey >> 32) & 0xFFFFFFFF);

    uint64_t val64_a = ((uint64_t)derived_hi << 32) | derived_lo;
    for(uint8_t i = 0; i < 8; i++) {
        aes_key[i] = (val64_a >> (56 - i * 8)) & 0xFF;
    }

    uint32_t new_lo = ((derived_hi >> 24) & 0xFF) | ((derived_hi >> 8) & 0xFF00) |
                      ((derived_hi << 8) & 0xFF0000) | ((derived_hi << 24) & 0xFF000000);
    uint32_t new_hi = ((derived_lo >> 24) & 0xFF) | ((derived_lo >> 8) & 0xFF00) |
                      ((derived_lo << 8) & 0xFF0000) | ((derived_lo << 24) & 0xFF000000);

    uint64_t val64_b = ((uint64_t)new_hi << 32) | new_lo;
    for(uint8_t i = 0; i < 8; i++) {
        aes_key[i + 8] = (val64_b >> (56 - i * 8)) & 0xFF;
    }
}

static void reverse_bits_in_bytes(uint8_t* data, uint8_t len) {
    for(uint8_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        uint8_t step1 = ((byte & 0x55) << 1) | ((byte >> 1) & 0x55);
        uint8_t step2 = ((step1 & 0x33) << 2) | ((step1 >> 2) & 0x33);
        data[i] = ((step2 & 0x0F) << 4) | (step2 >> 4);
    }
}

static void subghz_protocol_beninca_arc_encrypt(
    SubGhzBlockGeneric* generic,
    SubGhzKeystore* keystore,
    uint64_t middle_bytes) {

    uint8_t plaintext[16];

    plaintext[0] = (generic->serial >> 24) & 0xFF;
    plaintext[1] = (generic->serial >> 16) & 0xFF;
    plaintext[2] = (generic->serial >> 8) & 0xFF;
    plaintext[3] = generic->serial & 0xFF;
    plaintext[4] = generic->btn;
    plaintext[5] = (middle_bytes >> 32) & 0xFF;
    plaintext[6] = (middle_bytes >> 24) & 0xFF;
    plaintext[7] = (middle_bytes >> 16) & 0xFF;
    plaintext[8] = (middle_bytes >> 8) & 0xFF;
    plaintext[9] = middle_bytes & 0xFF;
    plaintext[10] = (generic->cnt >> 24) & 0xFF;
    plaintext[11] = (generic->cnt >> 16) & 0xFF;
    plaintext[12] = (generic->cnt >> 8) & 0xFF;
    plaintext[13] = generic->cnt & 0xFF;
    plaintext[14] = (generic->seed >> 8) & 0xFF;
    plaintext[15] = generic->seed & 0xFF;

    uint8_t aes_key[16];
    get_subghz_protocol_beninca_arc_aes_key(keystore, aes_key);

    uint8_t encrypted[16];
    furi_hal_crypto_aes128_ecb_encrypt(aes_key, plaintext, encrypted);
    memcpy(plaintext, encrypted, 16);

    reverse_bits_in_bytes(plaintext, 16);

    for(uint8_t i = 0; i < 8; i++) {
        generic->data = (generic->data << 8) | plaintext[i];
        generic->data_2 = (generic->data_2 << 8) | plaintext[i + 8];
    }
    return;
}

bool subghz_protocol_beninca_arc_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint32_t cnt,
    SubGhzRadioPreset* preset) {
    furi_assert(context);

    SubGhzProtocolEncoderBenincaARC* instance = context;
    instance->generic.serial = serial;
    instance->generic.btn = btn;
    instance->generic.cnt = cnt;
    instance->generic.seed = 0xAA55;
    instance->generic.data_count_bit = 128;

    subghz_protocol_beninca_arc_encrypt(&instance->generic, instance->keystore, 0x1);

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

void* subghz_protocol_decoder_beninca_arc_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolDecoderBenincaARC* instance = malloc(sizeof(SubGhzProtocolDecoderBenincaARC));
    instance->base.protocol = &subghz_protocol_beninca_arc;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->keystore = subghz_environment_get_keystore(environment);
    instance->decoder.parser_step = BenincaARCDecoderStart;
    return instance;
}

void subghz_protocol_decoder_beninca_arc_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderBenincaARC* instance = context;

    switch(instance->decoder.parser_step) {
    case BenincaARCDecoderStart:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_beninca_arc_const.te_long * 16) <
                        subghz_protocol_beninca_arc_const.te_delta * 15)) {

            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = BenincaARCDecoderHighLevel;
            break;
        }

        break;
    case BenincaARCDecoderHighLevel:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = BenincaARCDecoderLowLevel;

            if((instance->decoder.decode_count_bit ==
                (subghz_protocol_beninca_arc_const.min_count_bit_for_found / 2)) &&
               (instance->decoder.decode_data != 0)) {

                instance->generic.data = instance->decoder.decode_data;
                instance->decoder.decode_data = 0;
            } else if(
                instance->decoder.decode_count_bit ==
                subghz_protocol_beninca_arc_const.min_count_bit_for_found) {

                instance->generic.data_2 = instance->decoder.decode_data;
                instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                instance->decoder.parser_step = BenincaARCDecoderStart;

                if(instance->base.callback) {
                    instance->base.callback(&instance->base, instance->base.context);
                }

                break;
            }
        } else {
            instance->decoder.parser_step = BenincaARCDecoderStart;
        }
        break;
    case BenincaARCDecoderLowLevel:
        if(!level) {

            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_beninca_arc_const.te_short) <
                subghz_protocol_beninca_arc_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_beninca_arc_const.te_long) <
                subghz_protocol_beninca_arc_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = BenincaARCDecoderHighLevel;

            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_beninca_arc_const.te_long) <
                 subghz_protocol_beninca_arc_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_beninca_arc_const.te_short) <
                 subghz_protocol_beninca_arc_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = BenincaARCDecoderHighLevel;
            } else {
                instance->decoder.parser_step = BenincaARCDecoderStart;
            }
            break;
        } else {
            instance->decoder.parser_step = BenincaARCDecoderStart;
            break;
        }
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_beninca_arc_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderBenincaARC* instance = context;
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
    subghz_protocol_decoder_beninca_arc_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderBenincaARC* instance = context;

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_beninca_arc_const.min_count_bit_for_found);
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

