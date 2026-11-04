#include "kia_v5.h"

#include "common.h"
#include "../encode_plugin.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <lib/toolbox/manchester_decoder.h>

#define TAG "SubGhzProtocolKiaV5"

static const SubGhzBlockConst subghz_protocol_kia_v5_const = {
    .te_short = 400,
    .te_long = 800,
    .te_delta = 150,
    .min_count_bit_for_found = 67,
};

static const uint8_t keystore_bytes[] = {0x53, 0x54, 0x46, 0x52, 0x4b, 0x45, 0x30, 0x30};

static uint8_t kia_v5_reverse_byte(uint8_t b) {
    uint8_t r = 0;
    for(int i = 0; i < 8; i++) {
        if(b & (1 << i)) r |= (1 << (7 - i));
    }
    return r;
}

static uint16_t mixer_decode(uint32_t encrypted) {
    uint8_t s0 = (encrypted & 0xFF);
    uint8_t s1 = (encrypted >> 8) & 0xFF;
    uint8_t s2 = (encrypted >> 16) & 0xFF;
    uint8_t s3 = (encrypted >> 24) & 0xFF;

    int round_index = 1;
    for(size_t i = 0; i < 18; i++) {
        uint8_t r = keystore_bytes[round_index] & 0xFF;
        int steps = 8;
        while(steps > 0) {
            uint8_t base;
            if((s3 & 0x40) == 0) {
                base = (s3 & 0x02) == 0 ? 0x74 : 0x2E;
            } else {
                base = (s3 & 0x02) == 0 ? 0x3A : 0x5C;
            }

            if(s2 & 0x08) {
                base = (((base >> 4) & 0x0F) | ((base & 0x0F) << 4)) & 0xFF;
            }
            if(s1 & 0x01) {
                base = ((base & 0x3F) << 2) & 0xFF;
            }
            if(s0 & 0x01) {
                base = (base << 1) & 0xFF;
            }

            uint8_t temp = (s3 ^ s1) & 0xFF;
            s3 = ((s3 & 0x7F) << 1) & 0xFF;
            if(s2 & 0x80) {
                s3 |= 0x01;
            }
            s2 = ((s2 & 0x7F) << 1) & 0xFF;
            if(s1 & 0x80) {
                s2 |= 0x01;
            }
            s1 = ((s1 & 0x7F) << 1) & 0xFF;
            if(s0 & 0x80) {
                s1 |= 0x01;
            }
            s0 = ((s0 & 0x7F) << 1) & 0xFF;

            uint8_t chk = (base ^ (r ^ temp)) & 0xFF;
            if(chk & 0x80) {
                s0 |= 0x01;
            }
            r = ((r & 0x7F) << 1) & 0xFF;
            steps--;
        }
        round_index = (round_index - 1) & 0x7;
    }
    return (s0 + (s1 << 8)) & 0xFFFF;
}

static uint32_t kia_v5_mixer_encode(uint32_t serial, uint16_t counter, uint8_t button) {
    uint8_t state_a = (uint8_t)(((serial >> 8) & 0x0FU) | ((button & 0x0FU) << 4));
    uint8_t state_b = (uint8_t)((counter >> 8) & 0xFFU);
    uint8_t state_c = (uint8_t)(serial & 0xFFU);
    uint8_t state_d = (uint8_t)(counter & 0xFFU);

    int ks_idx = 0;
    for(int round_i = 0; round_i < 18; round_i++) {
        uint8_t r = keystore_bytes[ks_idx] & 0xFFU;
        ks_idx = (ks_idx + 1) & 0x07;

        uint8_t running_d = state_d;
        for(int step = 0; step < 8; step++) {
            uint8_t base;
            if((state_a & 0x80U) == 0) {
                base = (state_a & 0x04U) == 0 ? 0x74U : 0x2EU;
            } else {
                base = (state_a & 0x04U) == 0 ? 0x3AU : 0x5CU;
            }

            if(state_c & 0x10U) {
                base = (uint8_t)(((base >> 4) & 0x0FU) | ((base & 0x0FU) << 4));
            }
            if(state_b & 0x02U) {
                base = (uint8_t)((base & 0x3FU) << 2);
            }

            uint8_t base_final = base;
            if(running_d & 0x02U) {
                base_final = (uint8_t)((base & 0x7FU) << 1);
            }

            const bool carry_b = (state_b & 0x01U) != 0;
            const bool carry_c = (state_c & 0x01U) != 0;
            const bool carry_a = (state_a & 0x01U) != 0;

            uint8_t new_d = (uint8_t)(running_d >> 1);
            if(carry_b) new_d |= 0x80U;

            running_d ^= state_c;

            state_b = (uint8_t)(state_b >> 1);
            if(carry_c) state_b |= 0x80U;

            state_c = (uint8_t)(state_c >> 1);
            if(carry_a) state_c |= 0x80U;

            const uint8_t feedback = (uint8_t)(((running_d ^ r) << 7) ^ base_final);
            state_a = (uint8_t)(state_a >> 1);
            if(feedback & 0x80U) state_a |= 0x80U;

            r = (uint8_t)(r >> 1);
            running_d = new_d;
        }
        state_d = running_d;
    }

    return ((uint32_t)state_a << 24) | ((uint32_t)state_c << 16) | ((uint32_t)state_b << 8) |
           (uint32_t)state_d;
}

struct SubGhzProtocolDecoderKiaV5 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;

    ManchesterState manchester_state;
    uint64_t decoded_data;
    uint64_t saved_key;
    uint8_t bit_count;
    uint64_t yek;
    uint8_t crc;
};

struct SubGhzProtocolEncoderKiaV5 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint64_t replay_data;
    uint8_t replay_crc;
    bool reencrypt_mode;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderKiaV5);


SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderKiaV5);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderKiaV5);

#define KIA_V5_PREAMBLE_PAIRS 200U
#define KIA_V5_SYNC_ENTRIES   4U
#define KIA_V5_DATA_BITS      64U
#define KIA_V5_CRC_BITS       3U
#define KIA_V5_END_ENTRIES    2U
#define KIA_V5_UPLOAD_CAPACITY                          \
    (KIA_V5_PREAMBLE_PAIRS * 2U + KIA_V5_SYNC_ENTRIES + \
     (KIA_V5_DATA_BITS + KIA_V5_CRC_BITS) * 2U + KIA_V5_END_ENTRIES)

typedef enum {
    KiaV5DecoderStepReset = 0,
    KiaV5DecoderStepCheckPreamble,
    KiaV5DecoderStepData,
} KiaV5DecoderStep;

static uint8_t kia_v5_calculate_crc(uint64_t data) {
    uint8_t crc = 0;
    for(int i = 63; i >= 0; i--) {
        const uint8_t bit = (data >> i) & 1U;
        const uint8_t shifted_out = (crc >> 1U) & 1U;
        crc = (uint8_t)(((crc & 1U) << 1U) | bit);
        if(shifted_out) {
            crc ^= 3U;
        }
    }
    return (uint8_t)(crc & 3U);
}

const SubGhzProtocolDecoder subghz_protocol_kia_v5_decoder = {
    .alloc = subghz_protocol_decoder_kia_v5_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_kia_v5_feed,
    .reset = subghz_protocol_decoder_kia_v5_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_kia_v5_serialize,
    .deserialize = subghz_protocol_decoder_kia_v5_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_kia_v5_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_kia_v5");

const SubGhzProtocol subghz_protocol_kia_v5 = {
    .name = SUBGHZ_PROTOCOL_KIA_V5_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = &subghz_protocol_kia_v5_decoder,
    .encoder = &subghz_protocol_kia_v5_encoder_ext.vtable,
};

static bool subghz_protocol_encoder_kia_v5_get_upload(SubGhzProtocolEncoderKiaV5* instance) {
    furi_assert(instance);

    const uint32_t te_short = (uint32_t)subghz_protocol_kia_v5_const.te_short;
    const uint32_t te_long = (uint32_t)subghz_protocol_kia_v5_const.te_long;
    size_t index = 0;

    for(size_t i = 0; i < KIA_V5_PREAMBLE_PAIRS; i++) {
        instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);
        instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
    }

    instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
    instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_long);
    instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
    instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);

    for(int b = 63; b >= 0; b--) {
        const bool bv = ((instance->replay_data >> b) & 1ULL) != 0ULL;
        if(bv) {
            instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
            instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);
        } else {
            instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);
            instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
        }
    }

    instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);
    instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);

    const bool crc_b1 = ((instance->replay_crc >> 1U) & 1U) != 0U;
    if(crc_b1) {
        instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
        instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);
    } else {
        instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);
        instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
    }

    const bool crc_b0 = (instance->replay_crc & 1U) != 0U;
    if(crc_b0) {
        instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
        instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);
    } else {
        instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);
        instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
    }

    instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
    instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);

    instance->encoder.size_upload = index;
    instance->encoder.front = 0;
    return true;
}

static uint64_t kia_v5_rebuild_data(uint32_t serial, uint8_t btn, uint16_t cnt) {
    const uint32_t s = serial & 0x0FFFFFFFU;
    const uint8_t b = (uint8_t)(btn & 0x0FU);
    const uint32_t mixer = kia_v5_mixer_encode(s, cnt, b);
    const uint64_t yek_new = ((uint64_t)b << 60) | ((uint64_t)s << 32) | (uint64_t)mixer;

    uint64_t data_new = 0;
    for(int i = 0; i < 8; i++) {
        const uint8_t byte = (uint8_t)((yek_new >> (i * 8)) & 0xFFU);
        data_new |= ((uint64_t)kia_v5_reverse_byte(byte) << ((7 - i) * 8));
    }
    return data_new;
}

static bool kia_v5_reencrypt_and_upload(SubGhzProtocolEncoderKiaV5* instance) {
    const uint64_t data_new = kia_v5_rebuild_data(
        instance->generic.serial, instance->generic.btn, instance->generic.cnt);

    instance->replay_data = data_new;
    instance->generic.data = data_new;
    instance->replay_crc = kia_v5_calculate_crc(instance->replay_data);

    return subghz_protocol_encoder_kia_v5_get_upload(instance);
}

void subghz_protocol_encoder_kia_v5_set_button(void* context, uint8_t button) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;
    instance->generic.btn = button;
    if(instance->reencrypt_mode) {
        (void)kia_v5_reencrypt_and_upload(instance);
    }
}

void subghz_protocol_encoder_kia_v5_set_counter(void* context, uint16_t counter) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;
    instance->generic.cnt = counter;
    if(instance->reencrypt_mode) {
        (void)kia_v5_reencrypt_and_upload(instance);
    }
}

void subghz_protocol_encoder_kia_v5_increment_counter(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;
    if(instance->generic.cnt < 0xFFFF) {
        instance->generic.cnt++;
    } else {
        instance->generic.cnt = 0;
    }
    if(instance->reencrypt_mode) {
        (void)kia_v5_reencrypt_and_upload(instance);
    }
}

uint16_t subghz_protocol_encoder_kia_v5_get_counter(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;
    return instance->generic.cnt;
}

uint8_t subghz_protocol_encoder_kia_v5_get_button(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;
    return instance->generic.btn;
}

static void kia_v5_add_bit(SubGhzProtocolDecoderKiaV5* instance, bool bit) {
    instance->decoded_data = (instance->decoded_data << 1) | (bit ? 1 : 0);
    instance->bit_count++;
}

void* subghz_protocol_decoder_kia_v5_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderKiaV5* instance = malloc(sizeof(SubGhzProtocolDecoderKiaV5));
    furi_assert(instance);
    instance->base.protocol = &subghz_protocol_kia_v5;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_kia_v5_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV5* instance = context;
    instance->decoder.parser_step = KiaV5DecoderStepReset;
    instance->header_count = 0;
    instance->bit_count = 0;
    instance->decoded_data = 0;
    instance->saved_key = 0;
    instance->yek = 0;
    instance->crc = 0;
    instance->manchester_state = ManchesterStateMid1;
}

void subghz_protocol_decoder_kia_v5_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV5* instance = context;

    switch(instance->decoder.parser_step) {
    case KiaV5DecoderStepReset:
        if((level) && (DURATION_DIFF(duration, subghz_protocol_kia_v5_const.te_short) <
                       subghz_protocol_kia_v5_const.te_delta)) {
            instance->decoder.parser_step = KiaV5DecoderStepCheckPreamble;
            instance->decoder.te_last = duration;
            instance->header_count = 1;
            instance->bit_count = 0;
            instance->decoded_data = 0;
            manchester_advance(
                instance->manchester_state,
                ManchesterEventReset,
                &instance->manchester_state,
                NULL);
        }
        break;

    case KiaV5DecoderStepCheckPreamble:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_kia_v5_const.te_long) <
               subghz_protocol_kia_v5_const.te_delta) {
                if(instance->header_count > 40) {
                    instance->decoder.parser_step = KiaV5DecoderStepData;
                    instance->bit_count = 0;
                    instance->decoded_data = 0;
                    instance->saved_key = 0;
                    instance->header_count = 0;
                } else {
                    instance->decoder.te_last = duration;
                }
            } else if(
                DURATION_DIFF(duration, subghz_protocol_kia_v5_const.te_short) <
                subghz_protocol_kia_v5_const.te_delta) {
                instance->decoder.te_last = duration;
            } else {
                instance->decoder.parser_step = KiaV5DecoderStepReset;
            }
        } else {
            if((DURATION_DIFF(duration, subghz_protocol_kia_v5_const.te_short) <
                subghz_protocol_kia_v5_const.te_delta) &&
               (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_kia_v5_const.te_short) <
                subghz_protocol_kia_v5_const.te_delta)) {
                instance->header_count++;
            } else if(
                (DURATION_DIFF(duration, subghz_protocol_kia_v5_const.te_long) <
                 subghz_protocol_kia_v5_const.te_delta) &&
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_kia_v5_const.te_short) <
                 subghz_protocol_kia_v5_const.te_delta)) {
                instance->header_count++;
            } else if(
                DURATION_DIFF(instance->decoder.te_last, subghz_protocol_kia_v5_const.te_long) <
                subghz_protocol_kia_v5_const.te_delta) {
                instance->header_count++;
            } else {
                instance->decoder.parser_step = KiaV5DecoderStepReset;
            }
            instance->decoder.te_last = duration;
        }
        break;

    case KiaV5DecoderStepData: {
        ManchesterEvent event;

        if(DURATION_DIFF(duration, subghz_protocol_kia_v5_const.te_short) <
           subghz_protocol_kia_v5_const.te_delta) {
            event = level ? ManchesterEventShortHigh : ManchesterEventShortLow;
        } else if(
            DURATION_DIFF(duration, subghz_protocol_kia_v5_const.te_long) <
            subghz_protocol_kia_v5_const.te_delta) {
            event = level ? ManchesterEventLongHigh : ManchesterEventLongLow;
        } else {
            if(instance->bit_count >= subghz_protocol_kia_v5_const.min_count_bit_for_found) {
                instance->generic.data = instance->saved_key;
                instance->generic.data_count_bit =
                    subghz_protocol_kia_v5_const.min_count_bit_for_found;

                instance->crc = (uint8_t)(instance->decoded_data & 0x07);

                instance->yek = 0;
                for(int i = 0; i < 8; i++) {
                    uint8_t byte = (instance->generic.data >> (i * 8)) & 0xFF;
                    uint8_t reversed = 0;
                    for(int b = 0; b < 8; b++) {
                        if(byte & (1 << b)) reversed |= (1 << (7 - b));
                    }
                    instance->yek |= ((uint64_t)reversed << ((7 - i) * 8));
                }

                instance->generic.serial = (uint32_t)((instance->yek >> 32) & 0x0FFFFFFF);
                instance->generic.btn = (uint8_t)((instance->yek >> 60) & 0x0F);

                uint32_t encrypted = (uint32_t)(instance->yek & 0xFFFFFFFF);
                instance->generic.cnt = mixer_decode(encrypted);

                instance->decoder.decode_data = instance->generic.data;
                instance->decoder.decode_count_bit = instance->generic.data_count_bit;

                if(instance->crc == kia_v5_calculate_crc(instance->generic.data) &&
                   instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
            }

            instance->decoder.parser_step = KiaV5DecoderStepReset;
            break;
        }

        bool data_bit;
        if(instance->bit_count <= 66 &&
           manchester_advance(
               instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
            kia_v5_add_bit(instance, data_bit);
            if(instance->bit_count == 64) {
                instance->saved_key = instance->decoded_data;
                instance->decoded_data = 0;
            }
        }

        instance->decoder.te_last = duration;
        break;
    }
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_kia_v5_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV5* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        uint32_t serial_tmp = instance->generic.serial;
        if(!flipper_format_write_uint32(flipper_format, "Serial", &serial_tmp, 1)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
    }
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t btn_tmp = instance->generic.btn;
        if(!flipper_format_write_uint32(flipper_format, "Btn", &btn_tmp, 1)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
    }
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t cnt_tmp = instance->generic.cnt;
        if(!flipper_format_write_uint32(flipper_format, "Cnt", &cnt_tmp, 1)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
    }
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t crc_temp = instance->crc;
        if(!flipper_format_write_uint32(flipper_format, "CRC", &crc_temp, 1)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
    }
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t yek_high = (uint32_t)(instance->yek >> 32);
        uint32_t yek_low = (uint32_t)(instance->yek & 0xFFFFFFFF);
        if(!flipper_format_write_uint32(flipper_format, "YekHi", &yek_high, 1)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
        if(ret == SubGhzProtocolStatusOk) {
            if(!flipper_format_write_uint32(flipper_format, "YekLo", &yek_low, 1)) {
                ret = SubGhzProtocolStatusErrorParserOthers;
            }
        }
    }

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_kia_v5_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV5* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret == SubGhzProtocolStatusOk) {
        const uint16_t want = subghz_protocol_kia_v5_const.min_count_bit_for_found;
        if(instance->generic.data_count_bit >= 64 &&
           instance->generic.data_count_bit <= want) {
            instance->generic.data_count_bit = want;
        } else {
            ret = SubGhzProtocolStatusErrorValueBitCount;
        }
    }

    if(ret == SubGhzProtocolStatusOk) {
        flipper_format_rewind(flipper_format);
        uint32_t temp_crc = 0;
        if(flipper_format_read_uint32(flipper_format, "CRC", &temp_crc, 1)) {
            instance->crc = (uint8_t)temp_crc;
        } else {
            instance->crc = 0;
        }

        flipper_format_rewind(flipper_format);
        uint32_t yek_high = 0, yek_low = 0;
        if(flipper_format_read_uint32(flipper_format, "YekHi", &yek_high, 1) &&
           flipper_format_read_uint32(flipper_format, "YekLo", &yek_low, 1)) {
            instance->yek = ((uint64_t)yek_high << 32) | yek_low;
        } else {
            instance->yek = 0;
            for(int i = 0; i < 8; i++) {
                uint8_t byte = (instance->generic.data >> (i * 8)) & 0xFF;
                uint8_t reversed = 0;
                for(int j = 0; j < 8; j++) {
                    if(byte & (1 << j)) {
                        reversed |= (1 << (7 - j));
                    }
                }
                instance->yek |= ((uint64_t)reversed << ((7 - i) * 8));
            }
        }

        instance->generic.serial = (uint32_t)((instance->yek >> 32) & 0x0FFFFFFF);
        instance->generic.btn = (uint8_t)((instance->yek >> 60) & 0x0F);

        uint32_t encrypted = (uint32_t)(instance->yek & 0xFFFFFFFF);
        instance->generic.cnt = mixer_decode(encrypted);
    }

    return ret;
}

