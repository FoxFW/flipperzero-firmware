/* enc_ford_v3.fal : SD encoder for ford_v3 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define ford_protocol_v3 enc_ford_v3__ford_protocol_v3
#define subghz_protocol_encoder_common_free enc_ford_v3__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_yield enc_ford_v3__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_ford_v3_alloc enc_ford_v3__subghz_protocol_encoder_ford_v3_alloc
#define subghz_protocol_encoder_ford_v3_deserialize enc_ford_v3__subghz_protocol_encoder_ford_v3_deserialize
#define subghz_protocol_encoder_ford_v3_stop enc_ford_v3__subghz_protocol_encoder_ford_v3_stop
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/ford_v3.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <string.h>
#include <stdlib.h>
#include <lib/subghz/protocols/common.h>

#define FORD_V3_DATA_BITS      104U
#define FORD_V3_DATA_BYTES     13U
#define FORD_V3_ENC_TE_SHORT           240U
#define FORD_V3_ENC_PREAMBLE_PAIRS     40U
#define FORD_V3_ENC_BURST_COUNT        6U
#define FORD_V3_ENC_INTER_BURST_GAP_US 15000U
#define FORD_V3_ENC_ALLOC_ELEMS        1400U
#define FORD_V3_ENC_SYNC_LO_US         480U
#define FORD_V3_ENCODER_DEFAULT_REPEAT 10U

typedef struct SubGhzProtocolEncoderFordV3 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_bytes[FORD_V3_DATA_BYTES];
} 
SubGhzProtocolEncoderFordV3;

static inline void ford_v3_encoder_add_level(
    SubGhzProtocolEncoderFordV3* instance,
    bool level,
    uint32_t duration);
static inline void ford_v3_encoder_emit_manchester_bit(
    SubGhzProtocolEncoderFordV3* instance,
    bool bit);
static void ford_v3_encoder_emit_burst(SubGhzProtocolEncoderFordV3* instance);
static void ford_v3_encoder_build_upload(SubGhzProtocolEncoderFordV3* instance);

static inline void ford_v3_encoder_add_level(
    SubGhzProtocolEncoderFordV3* instance,
    bool level,
    uint32_t duration) {
    size_t idx = instance->encoder.size_upload;
    if(idx > 0 &&
       level_duration_get_level(instance->encoder.upload[idx - 1]) == level) {
        uint32_t prev = level_duration_get_duration(instance->encoder.upload[idx - 1]);
        instance->encoder.upload[idx - 1] = level_duration_make(level, prev + duration);
    } else {
        furi_check(idx < FORD_V3_ENC_ALLOC_ELEMS);
        instance->encoder.upload[idx] = level_duration_make(level, duration);
        instance->encoder.size_upload++;
    }
}

static inline void ford_v3_encoder_emit_manchester_bit(
    SubGhzProtocolEncoderFordV3* instance,
    bool bit) {
    if(bit) {
        ford_v3_encoder_add_level(instance, true, FORD_V3_ENC_TE_SHORT);
        ford_v3_encoder_add_level(instance, false, FORD_V3_ENC_TE_SHORT);
    } else {
        ford_v3_encoder_add_level(instance, false, FORD_V3_ENC_TE_SHORT);
        ford_v3_encoder_add_level(instance, true, FORD_V3_ENC_TE_SHORT);
    }
}

static void ford_v3_encoder_emit_burst(SubGhzProtocolEncoderFordV3* instance) {
    for(uint8_t i = 0; i < FORD_V3_ENC_PREAMBLE_PAIRS; i++) {
        ford_v3_encoder_add_level(instance, false, FORD_V3_ENC_TE_SHORT);
        ford_v3_encoder_add_level(instance, true, FORD_V3_ENC_TE_SHORT);
    }

    ford_v3_encoder_add_level(instance, false, FORD_V3_ENC_SYNC_LO_US);
    ford_v3_encoder_add_level(instance, true, FORD_V3_ENC_TE_SHORT);

    for(uint16_t bit_pos = 1U; bit_pos < FORD_V3_DATA_BITS; bit_pos++) {
        const uint8_t byte_idx = (uint8_t)(bit_pos / 8U);
        const uint8_t bit_idx = (uint8_t)(7U - (bit_pos % 8U));
        ford_v3_encoder_emit_manchester_bit(
            instance, ((instance->raw_bytes[byte_idx] >> bit_idx) & 1U) != 0U);
    }
}

static void ford_v3_encoder_build_upload(SubGhzProtocolEncoderFordV3* instance) {
    instance->encoder.size_upload = 0;
    instance->encoder.front = 0;

    for(uint8_t burst = 0; burst < FORD_V3_ENC_BURST_COUNT; burst++) {
        ford_v3_encoder_emit_burst(instance);
        if(burst + 1U < FORD_V3_ENC_BURST_COUNT) {
            ford_v3_encoder_add_level(instance, true, FORD_V3_ENC_INTER_BURST_GAP_US);
        }
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

void* subghz_protocol_encoder_ford_v3_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderFordV3* instance = calloc(1, sizeof(SubGhzProtocolEncoderFordV3));
    furi_check(instance);

    instance->base.protocol = &ford_protocol_v3;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = FORD_V3_ENCODER_DEFAULT_REPEAT;
    instance->encoder.upload = calloc(FORD_V3_ENC_ALLOC_ELEMS, sizeof(LevelDuration));
    furi_check(instance->encoder.upload);

    return instance;
}

SubGhzProtocolStatus subghz_protocol_encoder_ford_v3_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderFordV3* instance = context;

    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    instance->encoder.repeat = FORD_V3_ENCODER_DEFAULT_REPEAT;

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    do {
        SubGhzProtocolStatus g = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic, flipper_format, FORD_V3_DATA_BITS);
        if(g != SubGhzProtocolStatusOk) {
            ret = g;
            break;
        }

        flipper_format_rewind(flipper_format);
        uint8_t raw_tmp[FORD_V3_DATA_BYTES] = {0};
        if(!flipper_format_read_hex(flipper_format, "Raw", raw_tmp, sizeof(raw_tmp))) {
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        memcpy(instance->raw_bytes, raw_tmp, sizeof(raw_tmp));

        flipper_format_rewind(flipper_format);
        uint32_t repeat = FORD_V3_ENCODER_DEFAULT_REPEAT;
        if(flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1)) {
            instance->encoder.repeat = repeat;
        }

        ford_v3_encoder_build_upload(instance);
        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_ford_v3_stop(void* context) {
    furi_check(context);
    ((SubGhzProtocolEncoderFordV3*)context)->encoder.is_running = false;
}

static const SubGhzProtocolEncoder enc_ford_v3_vt = {
    .alloc = subghz_protocol_encoder_ford_v3_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_ford_v3_deserialize,
    .stop = subghz_protocol_encoder_ford_v3_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol ford_protocol_v3 = {
    .name = FORD_PROTOCOL_V3_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_ford_v3_vt,
};

static const SubGhzEncodePluginEntry enc_ford_v3_entry = {.name = FORD_PROTOCOL_V3_NAME, .encoder = &enc_ford_v3_vt};
static const FlipperAppPluginDescriptor enc_ford_v3_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_ford_v3_entry};
const FlipperAppPluginDescriptor* enc_ford_v3_ep(void) { return &enc_ford_v3_descriptor; }
