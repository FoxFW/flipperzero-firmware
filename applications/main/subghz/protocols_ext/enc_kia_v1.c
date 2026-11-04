/* enc_kia_v1.fal : SD encoder for kia_v1 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_kia_v1__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_kia_v1__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_kia_v1_alloc enc_kia_v1__subghz_protocol_encoder_kia_v1_alloc
#define subghz_protocol_encoder_kia_v1_deserialize enc_kia_v1__subghz_protocol_encoder_kia_v1_deserialize
#define subghz_protocol_encoder_kia_v1_yield enc_kia_v1__subghz_protocol_encoder_kia_v1_yield
#define subghz_protocol_kia_v1 enc_kia_v1__subghz_protocol_kia_v1
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/kia_v1.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/subghz/protocols/common.h>

#define TAG "KiaV1"
#define KIA_V1_TOTAL_BURSTS       3
#define KIA_V1_INTER_BURST_GAP_US 25000
#define KIA_V1_HEADER_PULSES      90
#define KIA_V1_UPLOAD_CAPACITY                                                     \
    ((KIA_V1_TOTAL_BURSTS * ((KIA_V1_HEADER_PULSES * 2) + 1 + ((57U - 1U) * 2))) + \
     (KIA_V1_TOTAL_BURSTS - 1))

struct SubGhzProtocolEncoderKiaV1 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static const SubGhzBlockConst subghz_protocol_kia_v1_const = {
    .te_short = 800,
    .te_long = 1600,
    .te_delta = 200,
    .min_count_bit_for_found = 57,
};

static uint8_t kia_v1_crc4(const uint8_t* bytes, int count, uint8_t offset);
static void subghz_protocol_encoder_kia_v1_get_upload(SubGhzProtocolEncoderKiaV1* instance);

static uint8_t kia_v1_crc4(const uint8_t* bytes, int count, uint8_t offset) {
    uint8_t crc = 0;

    for(int i = 0; i < count; i++) {
        uint8_t b = bytes[i];
        crc ^= ((b & 0x0F) ^ (b >> 4));
    }

    crc = (crc + offset) & 0x0F;
    return crc;
}

static void subghz_protocol_encoder_kia_v1_get_upload(SubGhzProtocolEncoderKiaV1* instance) {
    furi_assert(instance);
    if(instance->encoder.upload == NULL) return;
    size_t index = 0;
    LevelDuration* up = instance->encoder.upload;

    uint8_t cnt_high = (instance->generic.cnt >> 8) & 0xF;
    uint8_t char_data[7];
    char_data[0] = (instance->generic.serial >> 24) & 0xFF;
    char_data[1] = (instance->generic.serial >> 16) & 0xFF;
    char_data[2] = (instance->generic.serial >> 8) & 0xFF;
    char_data[3] = instance->generic.serial & 0xFF;
    char_data[4] = instance->generic.btn;
    char_data[5] = instance->generic.cnt & 0xFF;

    char_data[6] = cnt_high;
    uint8_t crc = kia_v1_crc4(char_data, 7, 1);

    instance->generic.data = (uint64_t)instance->generic.serial << 24 |
                             instance->generic.btn << 16 | (instance->generic.cnt & 0xFF) << 8 |
                             ((instance->generic.cnt >> 8) & 0xF) << 4 | crc;

    const uint32_t te_short = (uint32_t)subghz_protocol_kia_v1_const.te_short;
    const uint32_t te_long = (uint32_t)subghz_protocol_kia_v1_const.te_long;

    for(uint8_t burst = 0; burst < KIA_V1_TOTAL_BURSTS; burst++) {
        if(burst > 0) {
            up[index++] = level_duration_make(false, KIA_V1_INTER_BURST_GAP_US);
        }

        for(int i = 0; i < KIA_V1_HEADER_PULSES; i++) {
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
        KIA_V1_TOTAL_BURSTS,
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

void* subghz_protocol_encoder_kia_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderKiaV1* instance = calloc(1, sizeof(SubGhzProtocolEncoderKiaV1));
    if(!instance) {
        return NULL;
    }

    instance->base.protocol = &subghz_protocol_kia_v1;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 10;
    instance->encoder.size_upload = 0;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    return instance;
}

LevelDuration subghz_protocol_encoder_kia_v1_yield(void* context) {
    SubGhzProtocolEncoderKiaV1* instance = context;

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
    subghz_protocol_encoder_kia_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV1* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    flipper_format_rewind(flipper_format);

    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            FURI_LOG_E(TAG, "Missing or wrong Protocol");
            break;
        }

        instance->generic.data_count_bit = subghz_protocol_kia_v1_const.min_count_bit_for_found;

        if(instance->generic.data == 0) break;

        instance->generic.serial = instance->generic.data >> 24;
        instance->generic.btn = (instance->generic.data >> 16) & 0xFF;
        instance->generic.cnt = ((instance->generic.data >> 4) & 0xF) << 8 |
                                ((instance->generic.data >> 8) & 0xFF);

        uint32_t kia_v1_txraw = 0;
        flipper_format_rewind(flipper_format);
        flipper_format_read_uint32(flipper_format, "TxRaw", &kia_v1_txraw, 1);
        if(kia_v1_txraw == 0) {
            uint32_t mult = furi_hal_subghz_get_rolling_counter_mult();
            if(mult == 0U) mult = 1U;
            instance->generic.cnt = (instance->generic.cnt + mult) & 0xFFFU;
        }

        instance->encoder.repeat = 10;

        if(instance->encoder.upload == NULL) {
            instance->encoder.size_upload = KIA_V1_UPLOAD_CAPACITY;
            instance->encoder.upload =
                malloc(instance->encoder.size_upload * sizeof(LevelDuration));
        }
        subghz_protocol_encoder_kia_v1_get_upload(instance);

        uint8_t key_data[8];
        for(int i = 0; i < 8; i++) {
            key_data[i] = (uint8_t)((instance->generic.data >> (56 - 8 * i)) & 0xFF);
        }
        flipper_format_rewind(flipper_format);
        flipper_format_update_hex(flipper_format, "Key", key_data, 8);
        flipper_format_rewind(flipper_format);
        uint32_t cnt_store = instance->generic.cnt;
        flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &cnt_store, 1);

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

static const SubGhzProtocolEncoder enc_kia_v1_vt = {
    .alloc = subghz_protocol_encoder_kia_v1_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_kia_v1_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_kia_v1_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_kia_v1 = {
    .name = SUBGHZ_PROTOCOL_KIA_V1_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_kia_v1_vt,
};

static const SubGhzEncodePluginEntry enc_kia_v1_entry = {.name = SUBGHZ_PROTOCOL_KIA_V1_NAME, .encoder = &enc_kia_v1_vt};
static const FlipperAppPluginDescriptor enc_kia_v1_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_kia_v1_entry};
const FlipperAppPluginDescriptor* enc_kia_v1_ep(void) { return &enc_kia_v1_descriptor; }
