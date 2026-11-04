/* enc_kia_v2.fal : SD encoder for kia_v2 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_kia_v2__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_kia_v2__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_kia_v2_alloc enc_kia_v2__subghz_protocol_encoder_kia_v2_alloc
#define subghz_protocol_encoder_kia_v2_deserialize enc_kia_v2__subghz_protocol_encoder_kia_v2_deserialize
#define subghz_protocol_encoder_kia_v2_yield enc_kia_v2__subghz_protocol_encoder_kia_v2_yield
#define subghz_protocol_kia_v2 enc_kia_v2__subghz_protocol_kia_v2
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/kia_v2.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/toolbox/manchester_encoder.h>
#include <furi.h>
#include <lib/subghz/protocols/common.h>

#define TAG "KiaV2"
#define KIA_V2_HEADER_PAIRS 252
#define KIA_V2_TOTAL_BURSTS 2
#define KIA_V2_UPLOAD_CAPACITY \
    (KIA_V2_TOTAL_BURSTS * ((KIA_V2_HEADER_PAIRS * 2) + 1 + ((53U - 1U) * 2)))

struct SubGhzProtocolEncoderKiaV2 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_kia_v2_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 150,
    .min_count_bit_for_found = 53,
};

static uint8_t kia_v2_calculate_crc(uint64_t data);
static void subghz_protocol_encoder_kia_v2_get_upload(SubGhzProtocolEncoderKiaV2* instance);

static uint8_t kia_v2_calculate_crc(uint64_t data) {
    uint64_t data_without_crc = data >> 4;

    uint8_t bytes[6];
    bytes[0] = (uint8_t)(data_without_crc);
    bytes[1] = (uint8_t)(data_without_crc >> 8);
    bytes[2] = (uint8_t)(data_without_crc >> 16);
    bytes[3] = (uint8_t)(data_without_crc >> 24);
    bytes[4] = (uint8_t)(data_without_crc >> 32);
    bytes[5] = (uint8_t)(data_without_crc >> 40);

    uint8_t crc = 0;
    for(int i = 0; i < 6; i++) {
        crc ^= (bytes[i] & 0x0F) ^ (bytes[i] >> 4);
    }

    return (crc + 1) & 0x0F;
}

static void subghz_protocol_encoder_kia_v2_get_upload(SubGhzProtocolEncoderKiaV2* instance) {
    furi_assert(instance);
    if(instance->encoder.upload == NULL) return;
    size_t index = 0;
    LevelDuration* up = instance->encoder.upload;

    const uint32_t te_short = (uint32_t)subghz_protocol_kia_v2_const.te_short;
    const uint32_t te_long = (uint32_t)subghz_protocol_kia_v2_const.te_long;

    uint8_t crc = kia_v2_calculate_crc(instance->generic.data);
    instance->generic.data = (instance->generic.data & ~0x0FULL) | crc;

    for(uint8_t burst = 0; burst < KIA_V2_TOTAL_BURSTS; burst++) {
        for(int i = 0; i < KIA_V2_HEADER_PAIRS; i++) {
            up[index++] = level_duration_make(false, te_long);
            up[index++] = level_duration_make(true, te_long);
        }

        up[index++] = level_duration_make(false, te_short);

        for(uint8_t i = instance->generic.data_count_bit; i > 1; i--) {
            bool bit = bit_read(instance->generic.data, i - 2);
            up[index++] = level_duration_make(bit, te_short);
            up[index++] = level_duration_make(!bit, te_short);
        }
    }

    instance->encoder.size_upload = index;
    instance->encoder.front = 0;

    FURI_LOG_I(
        TAG,
        "Upload built: %d bursts, size_upload=%zu, data_count_bit=%u, data=0x%016llX",
        KIA_V2_TOTAL_BURSTS,
        instance->encoder.size_upload,
        instance->generic.data_count_bit,
        instance->generic.data);
}

/* vendored from lib/subghz/protocols/common.c (not exported) */
void subghz_protocol_encoder_common_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderCommon* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

/* vendored from lib/subghz/protocols/common.c (not exported) */
void subghz_protocol_encoder_common_stop(void* context) {
    SubGhzProtocolEncoderCommon* instance = context;
    instance->encoder.is_running = false;
}

void* subghz_protocol_encoder_kia_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderKiaV2* instance = calloc(1, sizeof(SubGhzProtocolEncoderKiaV2));
    if(!instance) {
        return NULL;
    }

    instance->base.protocol = &subghz_protocol_kia_v2;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 10;
    instance->encoder.size_upload = 0;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;

    return instance;
}

LevelDuration subghz_protocol_encoder_kia_v2_yield(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV2* instance = context;

    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        instance->encoder.repeat--;
        instance->encoder.front = 0;
    }

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_kia_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV2* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    flipper_format_rewind(flipper_format);

    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            FURI_LOG_E(TAG, "Missing or wrong Protocol");
            break;
        }

        instance->generic.data_count_bit = subghz_protocol_kia_v2_const.min_count_bit_for_found;

        if(instance->generic.data == 0) break;

        instance->generic.serial = (uint32_t)((instance->generic.data >> 20) & 0xFFFFFFFF);
        instance->generic.btn = (uint8_t)((instance->generic.data >> 16) & 0x0F);

        uint16_t raw_count = (uint16_t)((instance->generic.data >> 4) & 0xFFF);
        instance->generic.cnt = ((raw_count >> 4) | (raw_count << 8)) & 0xFFF;

        uint32_t kia_v2_txraw = 0;
        flipper_format_rewind(flipper_format);
        flipper_format_read_uint32(flipper_format, "TxRaw", &kia_v2_txraw, 1);
        flipper_format_rewind(flipper_format);
        if(kia_v2_txraw == 0) {
            uint32_t mult = furi_hal_subghz_get_rolling_counter_mult();
            if(mult == 0U) mult = 1U;
            instance->generic.cnt = (instance->generic.cnt + mult) & 0xFFFU;
        }

        instance->encoder.repeat = 10;

        uint64_t new_data = 0;

        new_data |= 1ULL << 52;

        new_data |= ((uint64_t)instance->generic.serial << 20) & 0xFFFFFFFFF00000ULL;

        uint32_t uVar6 = ((uint32_t)(instance->generic.cnt & 0xFF) << 8) |
                         ((uint32_t)(instance->generic.btn & 0x0F) << 16) |
                         ((uint32_t)(instance->generic.cnt >> 4) & 0xF0);

        new_data |= (uint64_t)uVar6;

        instance->generic.data = new_data;
        instance->generic.data_count_bit = 53;

        uint8_t key_data[8];
        for(int i = 0; i < 8; i++) {
            key_data[i] = (uint8_t)((instance->generic.data >> (56 - 8 * i)) & 0xFF);
        }
        flipper_format_rewind(flipper_format);
        flipper_format_update_hex(flipper_format, "Key", key_data, 8);
        flipper_format_rewind(flipper_format);
        uint32_t cnt_store = instance->generic.cnt;
        flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &cnt_store, 1);

        FURI_LOG_I(
            TAG,
            "Encoder reconstruct: serial=0x%08lX, btn=0x%X, cnt=0x%03lX, uVar6=0x%05lX, data=0x%016llX",
            (unsigned long)instance->generic.serial,
            (unsigned int)instance->generic.btn,
            (unsigned long)instance->generic.cnt,
            (unsigned long)uVar6,
            (unsigned long long)instance->generic.data);

        if(instance->encoder.upload == NULL) {
            instance->encoder.size_upload = KIA_V2_UPLOAD_CAPACITY;
            instance->encoder.upload =
                malloc(instance->encoder.size_upload * sizeof(LevelDuration));
        }
        subghz_protocol_encoder_kia_v2_get_upload(instance);

        instance->encoder.is_running = true;

        FURI_LOG_I(
            TAG,
            "Encoder deserialized: repeat=%u, size_upload=%zu, is_running=%d, front=%zu",
            instance->encoder.repeat,
            instance->encoder.size_upload,
            instance->encoder.is_running,
            instance->encoder.front);

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_kia_v2_vt = {
    .alloc = subghz_protocol_encoder_kia_v2_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_kia_v2_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_kia_v2_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_kia_v2 = {
    .name = SUBGHZ_PROTOCOL_KIA_V2_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_kia_v2_vt,
};

static const SubGhzEncodePluginEntry enc_kia_v2_entry = {.name = SUBGHZ_PROTOCOL_KIA_V2_NAME, .encoder = &enc_kia_v2_vt};
static const FlipperAppPluginDescriptor enc_kia_v2_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_kia_v2_entry};
const FlipperAppPluginDescriptor* enc_kia_v2_ep(void) { return &enc_kia_v2_descriptor; }
