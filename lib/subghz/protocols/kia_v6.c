#include "kia_v6.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <lib/toolbox/manchester_decoder.h>
#include <string.h>
#include <mbedtls/aes.h>

#define TAG "SubGhzProtocolKiaV6"

#define KIA_V6_XOR_MASK_LOW  0x84AF25FB
#define KIA_V6_XOR_MASK_HIGH 0x638766AB

#define KIA_V6_PREAMBLE_PAIRS_1  640
#define KIA_V6_PREAMBLE_PAIRS_2  38
#define KIA_V6_UPLOAD_SIZE       2000

static const SubGhzBlockConst subghz_protocol_kia_v6_const = {
    .te_short = 200,
    .te_long = 400,
    .te_delta = 100,
    .min_count_bit_for_found = 144,
};


struct SubGhzProtocolDecoderKiaV6 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;

    ManchesterState manchester_state;

    uint32_t data_part1_low;
    uint32_t data_part1_high;

    uint32_t stored_part1_low;
    uint32_t stored_part1_high;
    uint32_t stored_part2_low;
    uint32_t stored_part2_high;
    uint16_t data_part3;

    uint8_t bit_count;
    uint8_t fx_field;
    uint8_t crc1_field;
    uint8_t crc2_field;
};

struct SubGhzProtocolEncoderKiaV6 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint32_t stored_part1_low;
    uint32_t stored_part1_high;
    uint32_t stored_part2_low;
    uint32_t stored_part2_high;
    uint16_t data_part3;
    uint8_t fx_field;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderKiaV6);


SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderKiaV6);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderKiaV6);

typedef enum {
    KiaV6DecoderStepReset = 0,
    KiaV6DecoderStepWaitFirstHigh,
    KiaV6DecoderStepCountPreamble,
    KiaV6DecoderStepWaitLongHigh,
    KiaV6DecoderStepData,
} KiaV6DecoderStep;

#define kia_v6_crc8(data, len) subghz_protocol_blocks_crc8((data), (len), 0x07, 0xFF)

static void get_kia_v6_aes_key(uint8_t* aes_key) {
    uint64_t keystore_a = 0x37CE21F8C9F862A8ULL ^ 0x5448455049524154ULL;
    uint32_t keystore_a_hi = (keystore_a >> 32) & 0xFFFFFFFF;
    uint32_t keystore_a_lo = keystore_a & 0xFFFFFFFF;

    uint32_t uVar15_a = keystore_a_lo ^ KIA_V6_XOR_MASK_LOW;
    uint32_t uVar5_a = KIA_V6_XOR_MASK_HIGH ^ keystore_a_hi;

    uint64_t val64_a = ((uint64_t)uVar5_a << 32) | uVar15_a;
    for(int i = 0; i < 8; i++) {
        aes_key[i] = (val64_a >> (56 - i * 8)) & 0xFF;
    }

    uint64_t keystore_b = 0x3FC629F0C1F06AA0ULL ^ 0x5448455049524154ULL;
    uint32_t keystore_b_hi = (keystore_b >> 32) & 0xFFFFFFFF;
    uint32_t keystore_b_lo = keystore_b & 0xFFFFFFFF;

    uint32_t uVar15_b = keystore_b_lo ^ KIA_V6_XOR_MASK_LOW;
    uint32_t uVar5_b = KIA_V6_XOR_MASK_HIGH ^ keystore_b_hi;

    uint64_t val64_b = ((uint64_t)uVar5_b << 32) | uVar15_b;
    for(int i = 0; i < 8; i++) {
        aes_key[i + 8] = (val64_b >> (56 - i * 8)) & 0xFF;
    }
}

static bool kia_v6_decrypt(SubGhzProtocolDecoderKiaV6* instance) {
    uint8_t encrypted_data[16];

    encrypted_data[0] = (instance->stored_part1_high >> 8) & 0xFF;
    encrypted_data[1] = instance->stored_part1_high & 0xFF;

    encrypted_data[2] = (instance->stored_part1_low >> 24) & 0xFF;
    encrypted_data[3] = (instance->stored_part1_low >> 16) & 0xFF;
    encrypted_data[4] = (instance->stored_part1_low >> 8) & 0xFF;
    encrypted_data[5] = instance->stored_part1_low & 0xFF;

    encrypted_data[6] = (instance->stored_part2_high >> 24) & 0xFF;
    encrypted_data[7] = (instance->stored_part2_high >> 16) & 0xFF;
    encrypted_data[8] = (instance->stored_part2_high >> 8) & 0xFF;
    encrypted_data[9] = instance->stored_part2_high & 0xFF;

    encrypted_data[10] = (instance->stored_part2_low >> 24) & 0xFF;
    encrypted_data[11] = (instance->stored_part2_low >> 16) & 0xFF;
    encrypted_data[12] = (instance->stored_part2_low >> 8) & 0xFF;
    encrypted_data[13] = instance->stored_part2_low & 0xFF;

    encrypted_data[14] = (instance->data_part3 >> 8) & 0xFF;
    encrypted_data[15] = instance->data_part3 & 0xFF;

    uint8_t fx_byte0 = (instance->stored_part1_high >> 24) & 0xFF;
    uint8_t fx_byte1 = (instance->stored_part1_high >> 16) & 0xFF;
    instance->fx_field = ((fx_byte0 & 0xF) << 4) | (fx_byte1 & 0xF);

    uint8_t aes_key[16];
    get_kia_v6_aes_key(aes_key);
    mbedtls_aes_context aes_ctx;
    mbedtls_aes_init(&aes_ctx);
    mbedtls_aes_setkey_dec(&aes_ctx, aes_key, 128);
    mbedtls_aes_crypt_ecb(&aes_ctx, MBEDTLS_AES_DECRYPT, encrypted_data, encrypted_data);
    mbedtls_aes_free(&aes_ctx);

    uint8_t* decrypted = encrypted_data;

    uint8_t calculated_crc = kia_v6_crc8(decrypted, 15);
    uint8_t stored_crc = decrypted[15];

    instance->generic.serial = ((uint32_t)decrypted[4] << 16) | ((uint32_t)decrypted[5] << 8) |
                               decrypted[6];

    instance->generic.btn = decrypted[7];

    instance->generic.cnt = ((uint32_t)decrypted[8] << 24) | ((uint32_t)decrypted[9] << 16) |
                            ((uint32_t)decrypted[10] << 8) | decrypted[11];

    instance->crc1_field = decrypted[12];

    instance->crc2_field = decrypted[15];

    return (calculated_crc ^ stored_crc) < 2;
}

const SubGhzProtocolDecoder subghz_protocol_kia_v6_decoder = {
    .alloc = subghz_protocol_decoder_kia_v6_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_kia_v6_feed,
    .reset = subghz_protocol_decoder_kia_v6_reset,
    .get_hash_data = subghz_protocol_decoder_kia_v6_get_hash_data,
    .serialize = subghz_protocol_decoder_kia_v6_serialize,
    .deserialize = subghz_protocol_decoder_kia_v6_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_kia_v6_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_kia_v6");

const SubGhzProtocol subghz_protocol_kia_v6 = {
    .name = SUBGHZ_PROTOCOL_KIA_V6_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = &subghz_protocol_kia_v6_decoder,
    .encoder = &subghz_protocol_kia_v6_encoder_ext.vtable,
};

void* subghz_protocol_decoder_kia_v6_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderKiaV6* instance = malloc(sizeof(SubGhzProtocolDecoderKiaV6));
    furi_assert(instance);
    memset(instance, 0, sizeof(SubGhzProtocolDecoderKiaV6));

    instance->base.protocol = &subghz_protocol_kia_v6;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_kia_v6_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV6* instance = context;
    instance->decoder.parser_step = KiaV6DecoderStepReset;
    instance->header_count = 0;
    instance->bit_count = 0;
    instance->data_part1_low = 0;
    instance->data_part1_high = 0;
    manchester_advance(
        instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);
}

void subghz_protocol_decoder_kia_v6_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV6* instance = context;

    switch(instance->decoder.parser_step) {
    case KiaV6DecoderStepReset:
        if(level == 0) {
            return;
        }
        if(DURATION_DIFF(duration, subghz_protocol_kia_v6_const.te_short) <
           subghz_protocol_kia_v6_const.te_delta) {
            instance->decoder.parser_step = KiaV6DecoderStepWaitFirstHigh;
            instance->decoder.te_last = duration;
            instance->header_count = 0;
            manchester_advance(
                instance->manchester_state,
                ManchesterEventReset,
                &instance->manchester_state,
                NULL);
        }
        return;

    case KiaV6DecoderStepWaitFirstHigh:
        if(level != 0) {
            return;
        }
        uint32_t diff_short = DURATION_DIFF(duration, subghz_protocol_kia_v6_const.te_short);
        uint32_t diff_long = DURATION_DIFF(duration, subghz_protocol_kia_v6_const.te_long);

        uint32_t diff = (diff_long < diff_short) ? diff_long : diff_short;

        if(diff_long < subghz_protocol_kia_v6_const.te_delta && diff_long < diff_short) {
            if(instance->header_count >= 0x259) {
                instance->header_count = 0;
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = KiaV6DecoderStepWaitLongHigh;
                return;
            }
        }

        if(diff >= subghz_protocol_kia_v6_const.te_delta) {
            instance->decoder.parser_step = KiaV6DecoderStepReset;
            return;
        }

        if(DURATION_DIFF(instance->decoder.te_last, subghz_protocol_kia_v6_const.te_short) <
           subghz_protocol_kia_v6_const.te_delta) {
            instance->decoder.te_last = duration;
            instance->header_count++;
            return;
        } else {
            instance->decoder.parser_step = KiaV6DecoderStepReset;
            return;
        }

    case KiaV6DecoderStepWaitLongHigh:
        if(level == 0) {
            instance->decoder.parser_step = KiaV6DecoderStepReset;
            return;
        }
        uint32_t diff_long_check = DURATION_DIFF(duration, subghz_protocol_kia_v6_const.te_long);
        uint32_t diff_short_check = DURATION_DIFF(duration, subghz_protocol_kia_v6_const.te_short);

        if(diff_long_check >= subghz_protocol_kia_v6_const.te_delta) {
            if(diff_short_check >= subghz_protocol_kia_v6_const.te_delta) {
                instance->decoder.parser_step = KiaV6DecoderStepReset;
                return;
            }
        }

        if(DURATION_DIFF(instance->decoder.te_last, subghz_protocol_kia_v6_const.te_long) >=
           subghz_protocol_kia_v6_const.te_delta) {
            instance->decoder.parser_step = KiaV6DecoderStepReset;
            return;
        }
        instance->decoder.decode_data = 0;
        instance->decoder.decode_count_bit = 0;

        subghz_protocol_blocks_add_bit(&instance->decoder, 1);
        subghz_protocol_blocks_add_bit(&instance->decoder, 1);
        subghz_protocol_blocks_add_bit(&instance->decoder, 0);
        subghz_protocol_blocks_add_bit(&instance->decoder, 1);

        instance->data_part1_low = (uint32_t)(instance->decoder.decode_data & 0xFFFFFFFF);
        instance->data_part1_high =
            (uint32_t)((instance->decoder.decode_data >> 32) & 0xFFFFFFFF);
        instance->bit_count = instance->decoder.decode_count_bit;

        instance->decoder.parser_step = KiaV6DecoderStepData;
        return;

    case KiaV6DecoderStepData: {
        ManchesterEvent event;
        bool data_bit;

        if(DURATION_DIFF(duration, subghz_protocol_kia_v6_const.te_short) <
           subghz_protocol_kia_v6_const.te_delta) {
            event = (level & 0x7F) << 1;
        } else if(
            DURATION_DIFF(duration, subghz_protocol_kia_v6_const.te_long) <
            subghz_protocol_kia_v6_const.te_delta) {
            event = level ? ManchesterEventLongHigh : ManchesterEventLongLow;
        } else {
            instance->decoder.parser_step = KiaV6DecoderStepReset;
            return;
        }

        if(manchester_advance(
               instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
            uint32_t uVar4 = instance->data_part1_low;
            uint32_t uVar5 = (uVar4 << 1) | (data_bit ? 1 : 0);
            uint32_t carry = (uVar4 >> 31) & 1;
            uVar4 = (instance->data_part1_high << 1) | carry;
            instance->data_part1_low = uVar5;
            instance->data_part1_high = uVar4;
            instance->decoder.decode_data = ((uint64_t)uVar4 << 32) | uVar5;
            instance->bit_count++;

            if(instance->bit_count == 0x40) {
                instance->stored_part1_low = ~uVar5;
                instance->stored_part1_high = ~uVar4;
                instance->data_part1_low = 0;
                instance->data_part1_high = 0;
            } else if(instance->bit_count == 0x80) {
                instance->stored_part2_low = ~uVar5;
                instance->stored_part2_high = ~uVar4;
                instance->data_part1_low = 0;
                instance->data_part1_high = 0;
            }
        }

        instance->decoder.te_last = duration;

        if(instance->bit_count != subghz_protocol_kia_v6_const.min_count_bit_for_found) {
            return;
        }

        instance->generic.data_count_bit = subghz_protocol_kia_v6_const.min_count_bit_for_found;
        instance->data_part3 = ~((uint16_t)instance->data_part1_low);
        instance->generic.data =
            ((uint64_t)instance->stored_part1_high << 32) | instance->stored_part1_low;

        bool kia_v6_crc_ok = kia_v6_decrypt(instance);

        if(kia_v6_crc_ok && instance->base.callback) {
            instance->base.callback(&instance->base, instance->base.context);
        }

        instance->data_part1_low = 0;
        instance->data_part1_high = 0;
        instance->bit_count = 0;
        instance->decoder.parser_step = KiaV6DecoderStepReset;
        return;
    }

    default:
        return;
    }
}

uint8_t subghz_protocol_decoder_kia_v6_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV6* instance = context;
    uint8_t hash = 0;
    hash ^= (instance->stored_part1_low >> 24) & 0xFF;
    hash ^= (instance->stored_part1_low >> 16) & 0xFF;
    hash ^= (instance->stored_part1_low >> 8) & 0xFF;
    hash ^= instance->stored_part1_low & 0xFF;
    hash ^= (instance->stored_part1_high >> 24) & 0xFF;
    hash ^= (instance->stored_part1_high >> 16) & 0xFF;
    hash ^= (instance->stored_part1_high >> 8) & 0xFF;
    hash ^= instance->stored_part1_high & 0xFF;
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_kia_v6_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV6* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        uint32_t key2_low = instance->stored_part2_low;
        if(!flipper_format_write_uint32(flipper_format, "Key_2", &key2_low, 1)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
    }
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t key2_high = instance->stored_part2_high;
        if(!flipper_format_write_uint32(flipper_format, "Key_3", &key2_high, 1)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
    }
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t key3 = instance->data_part3;
        if(!flipper_format_write_uint32(flipper_format, "Key_4", &key3, 1)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
    }
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t fx = instance->fx_field;
        if(!flipper_format_write_uint32(flipper_format, "Fx", &fx, 1)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_kia_v6_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV6* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret == SubGhzProtocolStatusOk) {
        if(instance->generic.data_count_bit <
           subghz_protocol_kia_v6_const.min_count_bit_for_found) {
            ret = SubGhzProtocolStatusErrorParserBitCount;
        }
    }
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t temp;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Key_2", &temp, 1)) {
            instance->stored_part2_low = temp;
        }
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Key_3", &temp, 1)) {
            instance->stored_part2_high = temp;
        }
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Key_4", &temp, 1)) {
            instance->data_part3 = (uint16_t)temp;
        }
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Fx", &temp, 1)) {
            instance->fx_field = (uint8_t)temp;
        }
        instance->stored_part1_high = (uint32_t)((instance->generic.data >> 32) & 0xFFFFFFFF);
        instance->stored_part1_low = (uint32_t)(instance->generic.data & 0xFFFFFFFF);
        kia_v6_decrypt(instance);
    }
    return ret;
}

