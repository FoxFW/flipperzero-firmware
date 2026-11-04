/* enc_secplus_v1.fal : SD encoder for secplus_v1 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_alloc enc_secplus_v1__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_secplus_v1__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_secplus_v1__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_secplus_v1__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_secplus_v1_alloc enc_secplus_v1__subghz_protocol_encoder_secplus_v1_alloc
#define subghz_protocol_encoder_secplus_v1_deserialize enc_secplus_v1__subghz_protocol_encoder_secplus_v1_deserialize
#define subghz_protocol_secplus_v1 enc_secplus_v1__subghz_protocol_secplus_v1
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/secplus_v1.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocoSecPlusV1"
#define SECPLUS_V1_BIT_0   0
#define SECPLUS_V1_BIT_1   1
#define SECPLUS_V1_BIT_2   2
#define SECPLUS_V1_PACKET_1_HEADER     0x00
#define SECPLUS_V1_PACKET_2_HEADER     0x02
#define SECPLUS_V1_PACKET_1_INDEX_BASE 0
#define SECPLUS_V1_PACKET_2_INDEX_BASE 21

struct SubGhzProtocolEncoderSecPlus_v1 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t data_array[44];
};

static const SubGhzBlockConst subghz_protocol_secplus_v1_const = {
    .te_short = 500,
    .te_long = 1500,
    .te_delta = 100,
    .min_count_bit_for_found = 21,
};

/* host bridge: live firmware functions/state this encoder needs (set by the loader) */
extern bool secplus_v1_raw_replay__bridge_ty;
typedef struct {
    void* secplus_v1_raw_replay;
} enc_secplus_v1_host_t;
static const void* enc_secplus_v1_host_raw;
#define ENC_SECPLUS_V1_HOST ((const enc_secplus_v1_host_t*)enc_secplus_v1_host_raw)
#define secplus_v1_raw_replay (*(__typeof__(secplus_v1_raw_replay__bridge_ty)*)(ENC_SECPLUS_V1_HOST->secplus_v1_raw_replay))

static bool
    subghz_protocol_encoder_secplus_v1_get_upload(SubGhzProtocolEncoderSecPlus_v1* instance);
static bool subghz_protocol_secplus_v1_encode(SubGhzProtocolEncoderSecPlus_v1* instance);

static bool
    subghz_protocol_encoder_secplus_v1_get_upload(SubGhzProtocolEncoderSecPlus_v1* instance) {
    furi_assert(instance);
    size_t index = 0;
    size_t size_upload = (instance->generic.data_count_bit * 2);
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Encoder size upload exceeds allocated encoder buffer.");
        return false;
    } else {
        instance->encoder.size_upload = size_upload;
    }

    instance->encoder.upload[index++] = level_duration_make(
        false, (uint32_t)subghz_protocol_secplus_v1_const.te_short * (116 + 3));
    instance->encoder.upload[index++] =
        level_duration_make(true, (uint32_t)subghz_protocol_secplus_v1_const.te_short);

    for(uint8_t i = SECPLUS_V1_PACKET_1_INDEX_BASE + 1; i < SECPLUS_V1_PACKET_1_INDEX_BASE + 21;
        i++) {
        switch(instance->data_array[i]) {
        case SECPLUS_V1_BIT_0:
            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_secplus_v1_const.te_short * 3);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_secplus_v1_const.te_short);
            break;
        case SECPLUS_V1_BIT_1:
            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_secplus_v1_const.te_short * 2);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_secplus_v1_const.te_short * 2);
            break;
        case SECPLUS_V1_BIT_2:
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_secplus_v1_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_secplus_v1_const.te_short * 3);
            break;

        default:
            FURI_LOG_E(TAG, "Encoder error, wrong bit type");
            return false;
            break;
        }
    }

    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)subghz_protocol_secplus_v1_const.te_short * (116));
    instance->encoder.upload[index++] =
        level_duration_make(true, (uint32_t)subghz_protocol_secplus_v1_const.te_short * 3);

    for(uint8_t i = SECPLUS_V1_PACKET_2_INDEX_BASE + 1; i < SECPLUS_V1_PACKET_2_INDEX_BASE + 21;
        i++) {
        switch(instance->data_array[i]) {
        case SECPLUS_V1_BIT_0:
            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_secplus_v1_const.te_short * 3);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_secplus_v1_const.te_short);
            break;
        case SECPLUS_V1_BIT_1:
            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_secplus_v1_const.te_short * 2);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_secplus_v1_const.te_short * 2);
            break;
        case SECPLUS_V1_BIT_2:
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_secplus_v1_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_secplus_v1_const.te_short * 3);
            break;

        default:
            FURI_LOG_E(TAG, "Encoder error, wrong bit type.");
            return false;
            break;
        }
    }

    return true;
}

static bool subghz_protocol_secplus_v1_encode(SubGhzProtocolEncoderSecPlus_v1* instance) {
    uint32_t fixed = (instance->generic.data >> 32) & 0xFFFFFFFF;
    uint32_t rolling = instance->generic.data & 0xFFFFFFFF;

    uint8_t rolling_array[20] = {0};
    uint8_t fixed_array[20] = {0};
    uint32_t acc = 0;

    if(secplus_v1_raw_replay) {
        /* #16: freeze counter on TxRaw verbatim replay */
    } else if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF) {

        if(!subghz_block_generic_global_counter_override_get(&rolling)) {

            if((rolling + furi_hal_subghz_get_rolling_counter_mult()) > 0xFFFFFFFF) {
                rolling = 0xE6000000;
            } else {
                rolling += furi_hal_subghz_get_rolling_counter_mult();
            }
        }
        if(rolling < 0xE6000000) rolling = 0xE6000000;
    } else {

        if((rolling + 0x1) > 0xFFFFFFFF) {
            rolling = 0xE6000000;
        } else if(rolling >= 0xE6000000 && rolling != 0xFFFFFFFE) {
            rolling = 0xFFFFFFFE;
        } else {
            rolling++;
        }
    }

    instance->generic.data &= 0xFFFFFFFF00000000;
    instance->generic.data |= rolling;

    if(fixed > 0xCFD41B90) {
        FURI_LOG_E(TAG, "Encode wrong fixed data");
        return false;
    }

    rolling = subghz_protocol_blocks_reverse_key(rolling, 32);

    for(int i = 19; i > -1; i--) {
        rolling_array[i] = rolling % 3;
        rolling /= 3;
        fixed_array[i] = fixed % 3;
        fixed /= 3;
    }

    instance->data_array[SECPLUS_V1_PACKET_1_INDEX_BASE] = SECPLUS_V1_PACKET_1_HEADER;
    instance->data_array[SECPLUS_V1_PACKET_2_INDEX_BASE] = SECPLUS_V1_PACKET_2_HEADER;

    for(uint8_t i = 1; i < 11; i++) {
        acc += rolling_array[i - 1];
        instance->data_array[i * 2 - 1] = rolling_array[i - 1];
        acc += fixed_array[i - 1];
        instance->data_array[i * 2] = acc % 3;
    }

    acc = 0;

    for(uint8_t i = 11; i < 21; i++) {
        acc += rolling_array[i - 1];
        instance->data_array[i * 2] = rolling_array[i - 1];
        acc += fixed_array[i - 1];
        instance->data_array[i * 2 + 1] = acc % 3;
    }

    return true;
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

/* vendored from lib/subghz/protocols/common.c (not exported) */
void* subghz_protocol_encoder_common_alloc(
    size_t instance_size,
    const SubGhzProtocol* protocol,
    size_t repeat,
    size_t size_upload) {
    SubGhzProtocolEncoderCommonGeneric* instance = malloc(instance_size);
    instance->common.base.protocol = protocol;
    instance->generic.protocol_name = protocol->name;
    instance->common.encoder.repeat = repeat;
    instance->common.encoder.size_upload = size_upload;
    instance->common.encoder.upload = malloc(size_upload * sizeof(LevelDuration));
    instance->common.encoder.is_running = false;
    return instance;
}

void* subghz_protocol_encoder_secplus_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderSecPlus_v1), &subghz_protocol_secplus_v1, 3, 128);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_secplus_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderSecPlus_v1* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        /* #16: TxRaw verbatim replay — freeze the rolling counter. */
        secplus_v1_raw_replay = false;
        {
            uint32_t secplus_v1_txraw = 0;
            flipper_format_rewind(flipper_format);
            if(flipper_format_read_uint32(flipper_format, "TxRaw", &secplus_v1_txraw, 1) &&
               secplus_v1_txraw != 0) {
                secplus_v1_raw_replay = true;
            }
            flipper_format_rewind(flipper_format);
        }
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            2 * subghz_protocol_secplus_v1_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        if(!subghz_protocol_secplus_v1_encode(instance)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        if(!subghz_protocol_encoder_secplus_v1_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            ;
            break;
        }

        uint8_t key_data[sizeof(uint64_t)] = {0};
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data >> (i * 8)) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to add Key");
            ret = SubGhzProtocolStatusErrorParserKey;
            break;
        }

        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_secplus_v1_vt = {
    .alloc = subghz_protocol_encoder_secplus_v1_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_secplus_v1_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_secplus_v1 = {
    .name = SUBGHZ_PROTOCOL_SECPLUS_V1_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay | SubGhzProtocolFlag_Save,
    .decoder = NULL,
    .encoder = &enc_secplus_v1_vt,
};

static const SubGhzEncodePluginEntry enc_secplus_v1_entry = {.name = SUBGHZ_PROTOCOL_SECPLUS_V1_NAME, .encoder = &enc_secplus_v1_vt, .host_slot = &enc_secplus_v1_host_raw};
static const FlipperAppPluginDescriptor enc_secplus_v1_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_secplus_v1_entry};
const FlipperAppPluginDescriptor* enc_secplus_v1_ep(void) { return &enc_secplus_v1_descriptor; }
