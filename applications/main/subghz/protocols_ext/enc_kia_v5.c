/* enc_kia_v5.fal : SD encoder for kia_v5 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_yield enc_kia_v5__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_kia_v5_alloc enc_kia_v5__subghz_protocol_encoder_kia_v5_alloc
#define subghz_protocol_encoder_kia_v5_deserialize enc_kia_v5__subghz_protocol_encoder_kia_v5_deserialize
#define subghz_protocol_encoder_kia_v5_free enc_kia_v5__subghz_protocol_encoder_kia_v5_free
#define subghz_protocol_encoder_kia_v5_stop enc_kia_v5__subghz_protocol_encoder_kia_v5_stop
#define subghz_protocol_kia_v5 enc_kia_v5__subghz_protocol_kia_v5
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/kia_v5.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/subghz/protocols/common.h>

#define KIA_V5_PREAMBLE_PAIRS 200U
#define KIA_V5_SYNC_ENTRIES   4U
#define KIA_V5_DATA_BITS      64U
#define KIA_V5_CRC_BITS       3U
#define KIA_V5_END_ENTRIES    2U
#define KIA_V5_UPLOAD_CAPACITY                          \
    (KIA_V5_PREAMBLE_PAIRS * 2U + KIA_V5_SYNC_ENTRIES + \
     (KIA_V5_DATA_BITS + KIA_V5_CRC_BITS) * 2U + KIA_V5_END_ENTRIES)

struct SubGhzProtocolEncoderKiaV5 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint64_t replay_data;
    uint8_t replay_crc;
    bool reencrypt_mode;
};

static const SubGhzBlockConst subghz_protocol_kia_v5_const = {
    .te_short = 400,
    .te_long = 800,
    .te_delta = 150,
    .min_count_bit_for_found = 67,
};

static const uint8_t keystore_bytes[] = {0x53, 0x54, 0x46, 0x52, 0x4b, 0x45, 0x30, 0x30};

static uint8_t kia_v5_custom_to_btn(uint8_t custom_btn_id, uint8_t original_btn);
static uint8_t kia_v5_reverse_byte(uint8_t b);
static uint64_t kia_v5_bit_reverse_64(uint64_t input);
static uint16_t mixer_decode(uint32_t encrypted);
static uint32_t kia_v5_mixer_encode(uint32_t serial, uint16_t counter, uint8_t button);
static uint8_t kia_v5_calculate_crc(uint64_t data);
static bool subghz_protocol_encoder_kia_v5_get_upload(SubGhzProtocolEncoderKiaV5* instance);
static uint64_t kia_v5_rebuild_data(uint32_t serial, uint8_t btn, uint16_t cnt);
static bool kia_v5_reencrypt_and_upload(SubGhzProtocolEncoderKiaV5* instance);

static uint8_t kia_v5_custom_to_btn(uint8_t custom_btn_id, uint8_t original_btn) {
    switch(custom_btn_id) {
    case SUBGHZ_CUSTOM_BTN_UP:
        return 0x02U;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        return 0x01U;
    case SUBGHZ_CUSTOM_BTN_LEFT:
        return 0x04U;
    case SUBGHZ_CUSTOM_BTN_RIGHT:
        return 0x08U;
    case SUBGHZ_CUSTOM_BTN_OK:
    default:
        return original_btn;
    }
}

static uint8_t kia_v5_reverse_byte(uint8_t b) {
    uint8_t r = 0;
    for(int i = 0; i < 8; i++) {
        if(b & (1 << i)) r |= (1 << (7 - i));
    }
    return r;
}

static uint64_t kia_v5_bit_reverse_64(uint64_t input) {
    uint64_t output = 0;
    for(int i = 0; i < 8; i++) {
        uint8_t byte = (input >> (i * 8)) & 0xFF;
        uint8_t reversed = kia_v5_reverse_byte(byte);
        output |= ((uint64_t)reversed << ((7 - i) * 8));
    }
    return output;
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

/* vendored from lib/subghz/protocols/common.c (not exported) */
LevelDuration subghz_protocol_encoder_common_yield(void* context) {
    SubGhzProtocolEncoderCommon* instance = context;

    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        if(!subghz_block_generic_global.endless_tx) instance->encoder.repeat--;
        instance->encoder.front = 0;
    }

    return ret;
}

void* subghz_protocol_encoder_kia_v5_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolEncoderKiaV5* instance = calloc(1, sizeof(SubGhzProtocolEncoderKiaV5));
    furi_check(instance);

    instance->base.protocol = &subghz_protocol_kia_v5;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.size_upload = KIA_V5_UPLOAD_CAPACITY;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);
    instance->encoder.repeat = 6;
    instance->encoder.front = 0;
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_kia_v5_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;
    if(instance->encoder.upload) {
        free(instance->encoder.upload);
    }
    free(instance);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_kia_v5_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) break;

        if(instance->generic.data_count_bit < subghz_protocol_kia_v5_const.min_count_bit_for_found) {
            ret = SubGhzProtocolStatusErrorParserBitCount;
            break;
        }

        uint32_t kia_v5_txraw = 0;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "TxRaw", &kia_v5_txraw, 1) &&
           kia_v5_txraw != 0) {
            instance->reencrypt_mode = false;
            instance->replay_data = instance->generic.data;
            instance->replay_crc = kia_v5_calculate_crc(instance->replay_data);
            if(!subghz_protocol_encoder_kia_v5_get_upload(instance)) {
                ret = SubGhzProtocolStatusErrorEncoderGetUpload;
                break;
            }
            instance->encoder.is_running = true;
            ret = SubGhzProtocolStatusOk;
            break;
        }

        uint32_t yek_high = 0, yek_low = 0;
        uint64_t yek = 0;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "YekHi", &yek_high, 1) &&
           flipper_format_read_uint32(flipper_format, "YekLo", &yek_low, 1)) {
            yek = ((uint64_t)yek_high << 32) | yek_low;
        } else {
            yek = kia_v5_bit_reverse_64(instance->generic.data);
        }

        instance->generic.serial = (uint32_t)((yek >> 32) & 0x0FFFFFFF);
        instance->generic.btn = (uint8_t)((yek >> 60) & 0x0F);

        uint32_t encrypted = (uint32_t)(yek & 0xFFFFFFFF);
        instance->generic.cnt = mixer_decode(encrypted);

        instance->reencrypt_mode = false;
        uint32_t sub_serial = UINT32_MAX;
        uint32_t sub_btn = UINT32_MAX;
        uint32_t sub_cnt = UINT32_MAX;
        flipper_format_rewind(flipper_format);
        const bool have_serial =
            flipper_format_read_uint32(flipper_format, "Serial", &sub_serial, 1);
        flipper_format_rewind(flipper_format);
        const bool have_btn = flipper_format_read_uint32(flipper_format, "Btn", &sub_btn, 1);
        flipper_format_rewind(flipper_format);
        const bool have_cnt = flipper_format_read_uint32(flipper_format, "Cnt", &sub_cnt, 1);

        const uint8_t kia_v5_original_btn = (uint8_t)(instance->generic.btn & 0x0FU);
        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(kia_v5_original_btn);
        }
        subghz_custom_btn_set_max(4);
        const uint8_t kia_v5_selected_btn =
            kia_v5_custom_to_btn(subghz_custom_btn_get(), kia_v5_original_btn);
        const bool kia_v5_custom_active = (kia_v5_selected_btn != kia_v5_original_btn);

        uint32_t kia_v5_mult = furi_hal_subghz_get_rolling_counter_mult();
        if(kia_v5_mult == 0U) kia_v5_mult = 1U;
        bool kia_v5_reencrypted = false;

        if(have_serial && have_btn && have_cnt && sub_serial != UINT32_MAX &&
           sub_btn != UINT32_MAX && sub_cnt != UINT32_MAX) {
            instance->generic.serial = sub_serial & 0x0FFFFFFFU;
            instance->generic.btn = (uint8_t)(sub_btn & 0x0FU);
            instance->generic.cnt = (uint16_t)(sub_cnt & 0xFFFFU);
            if(kia_v5_custom_active) instance->generic.btn = kia_v5_selected_btn;
            instance->reencrypt_mode = true;

            instance->generic.cnt = (uint16_t)((instance->generic.cnt + kia_v5_mult) & 0xFFFFU);

            if(!kia_v5_reencrypt_and_upload(instance)) {
                ret = SubGhzProtocolStatusErrorEncoderGetUpload;
                break;
            }
            kia_v5_reencrypted = true;
        } else {
            if(kia_v5_custom_active) instance->generic.btn = kia_v5_selected_btn;
            instance->reencrypt_mode = true;

            instance->generic.cnt = (uint16_t)((instance->generic.cnt + kia_v5_mult) & 0xFFFFU);

            if(!kia_v5_reencrypt_and_upload(instance)) {
                ret = SubGhzProtocolStatusErrorEncoderGetUpload;
                break;
            }
            kia_v5_reencrypted = true;
        }

        if(kia_v5_reencrypted) {
            uint64_t yek = kia_v5_bit_reverse_64(instance->generic.data);
            uint32_t yek_hi = (uint32_t)(yek >> 32);
            uint32_t yek_lo = (uint32_t)(yek & 0xFFFFFFFF);
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, "YekHi", &yek_hi, 1);
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, "YekLo", &yek_lo, 1);

            uint8_t key_data[8];
            for(int i = 0; i < 8; i++) {
                key_data[i] = (uint8_t)((instance->generic.data >> (56 - 8 * i)) & 0xFF);
            }
            flipper_format_rewind(flipper_format);
            flipper_format_update_hex(flipper_format, "Key", key_data, 8);

            flipper_format_rewind(flipper_format);
            uint32_t cnt_store = instance->generic.cnt;
            flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &cnt_store, 1);
        }

        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_kia_v5_stop(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

static const SubGhzProtocolEncoder enc_kia_v5_vt = {
    .alloc = subghz_protocol_encoder_kia_v5_alloc,
    .free = subghz_protocol_encoder_kia_v5_free,
    .deserialize = subghz_protocol_encoder_kia_v5_deserialize,
    .stop = subghz_protocol_encoder_kia_v5_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_kia_v5 = {
    .name = SUBGHZ_PROTOCOL_KIA_V5_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_kia_v5_vt,
};

static const SubGhzEncodePluginEntry enc_kia_v5_entry = {.name = SUBGHZ_PROTOCOL_KIA_V5_NAME, .encoder = &enc_kia_v5_vt};
static const FlipperAppPluginDescriptor enc_kia_v5_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_kia_v5_entry};
const FlipperAppPluginDescriptor* enc_kia_v5_ep(void) { return &enc_kia_v5_descriptor; }
