/* enc_toyota_v0.fal : SD encoder for toyota_v0 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_toyota_v0__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_toyota_v0__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_toyota_v0__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_toyota_v0_alloc enc_toyota_v0__subghz_protocol_encoder_toyota_v0_alloc
#define subghz_protocol_encoder_toyota_v0_deserialize enc_toyota_v0__subghz_protocol_encoder_toyota_v0_deserialize
#define subghz_protocol_toyota_v0 enc_toyota_v0__subghz_protocol_toyota_v0
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/toyota_v0.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <furi.h>
#include <furi_hal.h>
#include <lib/subghz/protocols/common.h>

#define TOYOTA_V0_DATA_BITS       88    // 11 bytes actual data
#define TOYOTA_V0_FRAME_BITS      184   // 88 data + 8 zero-gap + 88 complement
#define TOYOTA_V0_TOTAL_BURSTS    5
#define TOYOTA_V0_PREAMBLE_PAIRS  20

struct SubGhzProtocolEncoderToyotaV0 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t data_32;
};

static const SubGhzBlockConst subghz_protocol_toyota_v0_const = {
    .te_short = 750,
    .te_long = 1500,
    .te_delta = 250,
    .min_count_bit_for_found = TOYOTA_V0_DATA_BITS,
};

// ============================================================================
// DISPLAY
// ============================================================================

// ============================================================================
// ENCODER
// ============================================================================

static bool toyota_v0_get_data_bit(SubGhzProtocolEncoderToyotaV0* instance, uint8_t idx);
/**
 * Build one complete frame:
 * [PREAMBLE: 20x SHORT pairs] [96 Manchester data bits] [96 Manchester complement bits] [END GAP]
 */
static void toyota_v0_build_upload(SubGhzProtocolEncoderToyotaV0* instance);

// ============================================================================
// DISPLAY
// ============================================================================

// ============================================================================
// ENCODER
// ============================================================================

static bool toyota_v0_get_data_bit(SubGhzProtocolEncoderToyotaV0* instance, uint8_t idx) {
    // 88-bit data: 64 bits in generic.data + 24 bits in data_32 (left-aligned)
    if(idx < 64) {
        return (instance->generic.data >> (63 - idx)) & 1;
    } else {
        // data_32 is left-aligned, so bit 64 is bit 31 of data_32
        return (instance->data_32 >> (31 - (idx - 64))) & 1;
    }
}

/**
 * Build one complete frame:
 * [PREAMBLE: 20x SHORT pairs] [96 Manchester data bits] [96 Manchester complement bits] [END GAP]
 */
static void toyota_v0_build_upload(SubGhzProtocolEncoderToyotaV0* instance) {
    furi_assert(instance);
    size_t index = 0;

    uint32_t te_s = subghz_protocol_toyota_v0_const.te_short;
    uint32_t te_l = subghz_protocol_toyota_v0_const.te_long;

    // PREAMBLE: alternating SHORT pairs
    for(int p = 0; p < TOYOTA_V0_PREAMBLE_PAIRS; p++) {
        instance->encoder.upload[index++] = level_duration_make(true, te_s);
        instance->encoder.upload[index++] = level_duration_make(false, te_s);
    }

    // Manchester-encode 192 bits: 96 data bits + 96 complement bits
    uint8_t manchester_state = ManchesterStateMid1;

    for(int i = 0; i < TOYOTA_V0_FRAME_BITS; i++) {
        bool b;
        if(i < TOYOTA_V0_DATA_BITS) {
            b = toyota_v0_get_data_bit(instance, i);
        } else {
            // Complement: invert the corresponding data bit
            b = !toyota_v0_get_data_bit(instance, i - TOYOTA_V0_DATA_BITS);
        }

        switch(manchester_state) {
        case ManchesterStateMid1:
            if(b == 0) {
                instance->encoder.upload[index++] = level_duration_make(true, te_l);
                manchester_state = ManchesterStateMid0;
            } else {
                instance->encoder.upload[index++] = level_duration_make(true, te_s);
                instance->encoder.upload[index++] = level_duration_make(false, te_s);
                manchester_state = ManchesterStateMid1;
            }
            break;
        case ManchesterStateMid0:
            if(b == 0) {
                instance->encoder.upload[index++] = level_duration_make(false, te_s);
                instance->encoder.upload[index++] = level_duration_make(true, te_s);
                manchester_state = ManchesterStateMid0;
            } else {
                instance->encoder.upload[index++] = level_duration_make(false, te_l);
                manchester_state = ManchesterStateMid1;
            }
            break;
        default:
            manchester_state = ManchesterStateMid1;
            break;
        }
    }

    // END GAP
    bool end_level = (manchester_state == ManchesterStateMid1);
    instance->encoder.upload[index++] = level_duration_make(end_level, 6000);

    instance->encoder.size_upload = index;
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

void* subghz_protocol_encoder_toyota_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderToyotaV0* instance = malloc(sizeof(SubGhzProtocolEncoderToyotaV0));
    instance->base.protocol = &subghz_protocol_toyota_v0;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = TOYOTA_V0_TOTAL_BURSTS;
    instance->encoder.size_upload = 1024;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_toyota_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderToyotaV0* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;

    do {
        if(subghz_block_generic_deserialize(&instance->generic, flipper_format) !=
           SubGhzProtocolStatusOk) {
            break;
        }

        uint32_t k2 = 0;
        if(!flipper_format_read_uint32(flipper_format, "Key2", &k2, 1)) break;
        instance->data_32 = k2;

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        toyota_v0_build_upload(instance);
        instance->encoder.is_running = true;
        res = SubGhzProtocolStatusOk;
    } while(false);

    return res;
}

static const SubGhzProtocolEncoder enc_toyota_v0_vt = {
    .alloc = subghz_protocol_encoder_toyota_v0_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_toyota_v0_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_toyota_v0 = {
    .name = SUBGHZ_PROTOCOL_TOYOTA_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_toyota_v0_vt,
};

static const SubGhzEncodePluginEntry enc_toyota_v0_entry = {.name = SUBGHZ_PROTOCOL_TOYOTA_V0_NAME, .encoder = &enc_toyota_v0_vt};
static const FlipperAppPluginDescriptor enc_toyota_v0_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_toyota_v0_entry};
const FlipperAppPluginDescriptor* enc_toyota_v0_ep(void) { return &enc_toyota_v0_descriptor; }
