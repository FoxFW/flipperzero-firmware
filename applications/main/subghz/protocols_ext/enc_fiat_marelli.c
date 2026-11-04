/* enc_fiat_marelli.fal : SD encoder for fiat_marelli (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_fiat_marelli__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_fiat_marelli__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_fiat_marelli__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_fiat_marelli_alloc enc_fiat_marelli__subghz_protocol_encoder_fiat_marelli_alloc
#define subghz_protocol_encoder_fiat_marelli_deserialize enc_fiat_marelli__subghz_protocol_encoder_fiat_marelli_deserialize
#define subghz_protocol_fiat_marelli enc_fiat_marelli__subghz_protocol_fiat_marelli
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/fiat_marelli.h>
#include <lib/subghz/protocols/fiat_v1.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <furi_hal_subghz.h>
#include <lib/subghz/protocols/common.h>

#define FIAT_MARELLI_MAX_DATA_BITS      104
#define FIAT_MARELLI_MIN_DATA_BITS      80
#define FIAT_MARELLI_ENCODER_UPLOAD_MAX 1500
#define FIAT_MARELLI_ENCODER_REPEAT     3
#define FIAT_MARELLI_PREAMBLE_PAIRS     100

struct SubGhzProtocolEncoderFiatMarelli {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint8_t raw_data[13];
    uint32_t extra_data;
    uint8_t bit_count;
    uint32_t te_detected;
};

static const SubGhzBlockConst subghz_protocol_fiat_marelli_const = {
    .te_short = 260,
    .te_long = 520,
    .te_delta = 80,
    .min_count_bit_for_found = 80,
};

static uint8_t fiat_marelli_crc8(const uint8_t* data, size_t len);
static bool fiat_marelli_encoder_get_upload(SubGhzProtocolEncoderFiatMarelli* instance);
static void fiat_marelli_encoder_rebuild_raw_data(SubGhzProtocolEncoderFiatMarelli* instance);

static uint8_t fiat_marelli_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x03;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x01) : (crc << 1);
        }
    }
    return crc;
}

static bool fiat_marelli_encoder_get_upload(SubGhzProtocolEncoderFiatMarelli* instance) {
    uint32_t te = instance->te_detected;
    if(te == 0) te = subghz_protocol_fiat_marelli_const.te_short;

    uint32_t te_short = te;
    uint32_t te_long = te * 2;
    uint32_t gap_duration = te * 12;
    uint32_t sync_duration = te * 8;

    size_t index = 0;
    size_t max_upload = FIAT_MARELLI_ENCODER_UPLOAD_MAX;
    uint8_t data_bits = instance->bit_count;
    if(data_bits == 0) data_bits = instance->generic.data_count_bit;
    if(data_bits < FIAT_MARELLI_MIN_DATA_BITS || data_bits > FIAT_MARELLI_MAX_DATA_BITS) {
        return false;
    }

    for(uint8_t i = 0; i < FIAT_MARELLI_PREAMBLE_PAIRS && (index + 1) < max_upload; i++) {
        instance->encoder.upload[index++] = level_duration_make(true, te_short);
        if(i < FIAT_MARELLI_PREAMBLE_PAIRS - 1) {
            instance->encoder.upload[index++] = level_duration_make(false, te_short);
        }
    }

    if(index < max_upload) {
        instance->encoder.upload[index++] = level_duration_make(false, te_short + gap_duration);
    }

    if(index < max_upload) {
        instance->encoder.upload[index++] = level_duration_make(true, sync_duration);
    }

    bool in_mid1 = true;

    for(uint8_t bit_i = 0; bit_i < data_bits && (index + 1) < max_upload; bit_i++) {
        uint8_t byte_idx = bit_i / 8;
        uint8_t bit_pos = 7 - (bit_i % 8);
        bool data_bit = (instance->raw_data[byte_idx] >> bit_pos) & 1;

        if(in_mid1) {
            if(data_bit) {
                instance->encoder.upload[index++] = level_duration_make(false, te_short);
                instance->encoder.upload[index++] = level_duration_make(true, te_short);
            } else {
                instance->encoder.upload[index++] = level_duration_make(false, te_long);
                in_mid1 = false;
            }
        } else {
            if(data_bit) {
                instance->encoder.upload[index++] = level_duration_make(true, te_long);
                in_mid1 = true;
            } else {
                instance->encoder.upload[index++] = level_duration_make(true, te_short);
                instance->encoder.upload[index++] = level_duration_make(false, te_short);
            }
        }
    }

    if(in_mid1) {
        if(index < max_upload) {
            instance->encoder.upload[index++] =
                level_duration_make(false, te_short + gap_duration * 3);
        }
    } else {
        if(index > 0) {
            instance->encoder.upload[index - 1] =
                level_duration_make(false, te_short + gap_duration * 3);
        }
    }

    instance->encoder.size_upload = index;
    return index > 0;
}

static void fiat_marelli_encoder_rebuild_raw_data(SubGhzProtocolEncoderFiatMarelli* instance) {
    memset(instance->raw_data, 0, sizeof(instance->raw_data));

    uint64_t key = instance->generic.data;
    for(int i = 0; i < 8; i++) {
        instance->raw_data[i] = (uint8_t)(key >> (56 - i * 8));
    }

    uint8_t extra_bits =
        instance->generic.data_count_bit > 64 ? (instance->generic.data_count_bit - 64) : 0;
    for(uint8_t i = 0; i < extra_bits && i < 32; i++) {
        uint8_t byte_idx = 8 + (i / 8);
        uint8_t bit_pos = 7 - (i % 8);
        if(instance->extra_data & (1UL << (extra_bits - 1 - i))) {
            instance->raw_data[byte_idx] |= (1 << bit_pos);
        }
    }

    instance->bit_count = instance->generic.data_count_bit;

    if(instance->bit_count >= 104) {
        instance->raw_data[12] = fiat_marelli_crc8(instance->raw_data, 12);
    }
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

void* subghz_protocol_encoder_fiat_marelli_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderFiatMarelli* instance = calloc(1, sizeof(SubGhzProtocolEncoderFiatMarelli));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_fiat_marelli;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = FIAT_MARELLI_ENCODER_REPEAT;
    instance->encoder.size_upload = FIAT_MARELLI_ENCODER_UPLOAD_MAX;
    instance->encoder.upload = malloc(FIAT_MARELLI_ENCODER_UPLOAD_MAX * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);
    instance->encoder.is_running = false;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_fiat_marelli_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderFiatMarelli* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) break;

        uint32_t extra = 0;
        if(flipper_format_read_uint32(flipper_format, "Extra", &extra, 1)) {
            instance->extra_data = extra;
        }

        uint32_t te = 0;
        if(flipper_format_read_uint32(flipper_format, "TE", &te, 1)) {
            instance->te_detected = te;
        }

        fiat_marelli_encoder_rebuild_raw_data(instance);

        if(!fiat_marelli_encoder_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }

        instance->encoder.repeat = FIAT_MARELLI_ENCODER_REPEAT;
        instance->encoder.front = 0;
        instance->encoder.is_running = true;

    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_fiat_marelli_vt = {
    .alloc = subghz_protocol_encoder_fiat_marelli_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_fiat_marelli_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_fiat_marelli = {
    .name = FIAT_MARELLI_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = 0,
    .decoder = NULL,
    .encoder = &enc_fiat_marelli_vt,
};

static const SubGhzEncodePluginEntry enc_fiat_marelli_entry = {.name = FIAT_MARELLI_PROTOCOL_NAME, .encoder = &enc_fiat_marelli_vt};
static const FlipperAppPluginDescriptor enc_fiat_marelli_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_fiat_marelli_entry};
const FlipperAppPluginDescriptor* enc_fiat_marelli_ep(void) { return &enc_fiat_marelli_descriptor; }
