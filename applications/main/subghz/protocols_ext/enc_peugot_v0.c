/* enc_peugot_v0.fal : SD encoder for peugot_v0 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_peugot_v0__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_peugot_v0__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_peugot_v0__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_peugot_v0_alloc enc_peugot_v0__subghz_protocol_encoder_peugot_v0_alloc
#define subghz_protocol_encoder_peugot_v0_deserialize enc_peugot_v0__subghz_protocol_encoder_peugot_v0_deserialize
#define subghz_protocol_peugot_v0 enc_peugot_v0__subghz_protocol_peugot_v0
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/peugot_v0.h>
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

#define TAG "SubGhzProtocolPeugotV0"
#define PEUGOT_V0_DATA_BITS         80

struct SubGhzProtocolEncoderPeugotV0 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint16_t data_16;  // Bits 64-79 for encoding
};

static const SubGhzBlockConst subghz_protocol_peugot_v0_const = {
    .te_short = 220,
    .te_long = 440,
    .te_delta = 100,
    .min_count_bit_for_found = PEUGOT_V0_DATA_BITS,
};

/**
 * Build one complete frame into the upload buffer.
 * Signal: [80x SHORT pairs (preamble)][Manchester FE sync + 80 data bits][END GAP]
 */
static void subghz_protocol_encoder_peugot_v0_get_upload(SubGhzProtocolEncoderPeugotV0* instance);

/**
 * Build one complete frame into the upload buffer.
 * Signal: [80x SHORT pairs (preamble)][Manchester FE sync + 80 data bits][END GAP]
 */
static void subghz_protocol_encoder_peugot_v0_get_upload(SubGhzProtocolEncoderPeugotV0* instance) {
    furi_assert(instance);
    size_t index = 0;

    const uint32_t te_short = (uint32_t)subghz_protocol_peugot_v0_const.te_short;
    const uint32_t te_long = (uint32_t)subghz_protocol_peugot_v0_const.te_long;

    // PREAMBLE: 80 SHORT pairs (= 80 Manchester 1-bits)
    for(int p = 0; p < 80; p++) {
        instance->encoder.upload[index++] = level_duration_make(true, te_short);
        instance->encoder.upload[index++] = level_duration_make(false, te_short);
    }

    // FE sync (8 bits) + 80 data bits = 88 bits Manchester encoded
    // After preamble of 1s, Manchester state is Mid1
    uint8_t manchester_state = ManchesterStateMid1;

    // FE = 11111110 (8 bits sync)
    uint8_t sync_byte = 0xFE;

    // Total: 8 (sync) + 80 (data) = 88 bits
    for(int i = 87; i >= 0; i--) {
        bool bit;
        if(i >= 80) {
            // Sync bits: FE byte, bits 7..0 (from MSB to LSB)
            bit = (sync_byte >> (i - 80)) & 1;
        } else if(i >= 16) {
            // Data bits 63..0 from generic.data
            bit = (instance->generic.data >> (i - 16)) & 1;
        } else {
            // Data bits 79-64 from data_16 (bits 15..0)
            bit = (instance->data_16 >> i) & 1;
        }

        switch(manchester_state) {
        case ManchesterStateMid1:
            if(bit == 0) {
                instance->encoder.upload[index++] = level_duration_make(true, te_long);
                manchester_state = ManchesterStateMid0;
            } else {
                instance->encoder.upload[index++] = level_duration_make(true, te_short);
                instance->encoder.upload[index++] = level_duration_make(false, te_short);
                manchester_state = ManchesterStateMid1;
            }
            break;
        case ManchesterStateMid0:
            if(bit == 0) {
                instance->encoder.upload[index++] = level_duration_make(false, te_short);
                instance->encoder.upload[index++] = level_duration_make(true, te_short);
                manchester_state = ManchesterStateMid0;
            } else {
                instance->encoder.upload[index++] = level_duration_make(false, te_long);
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
    instance->encoder.upload[index++] = level_duration_make(end_level, (uint32_t)6000);

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

// ============================================================================
// DISPLAY
// ============================================================================

// ============================================================================
// ENCODER IMPLEMENTATION
// ============================================================================

void* subghz_protocol_encoder_peugot_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderPeugotV0* instance = malloc(sizeof(SubGhzProtocolEncoderPeugotV0));

    instance->base.protocol = &subghz_protocol_peugot_v0;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 3;
    instance->encoder.size_upload = 512;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_peugot_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderPeugotV0* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        if(subghz_block_generic_deserialize(&instance->generic, flipper_format) !=
           SubGhzProtocolStatusOk) {
            FURI_LOG_E(TAG, "Deserialize error");
            break;
        }

        if(instance->generic.data_count_bit != PEUGOT_V0_DATA_BITS) {
            FURI_LOG_E(
                TAG, "Invalid bit count: %lu", (unsigned long)instance->generic.data_count_bit);
            res = SubGhzProtocolStatusErrorValueBitCount;
            break;
        }

        // Read Key2
        uint32_t key2 = 0;
        flipper_format_read_uint32(flipper_format, "Key2", &key2, 1);
        instance->data_16 = (uint16_t)(key2 & 0xFFFF);

        // Optional repeat parameter
        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_encoder_peugot_v0_get_upload(instance);

        instance->encoder.is_running = true;
        res = SubGhzProtocolStatusOk;
    } while(false);

    return res;
}

static const SubGhzProtocolEncoder enc_peugot_v0_vt = {
    .alloc = subghz_protocol_encoder_peugot_v0_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_peugot_v0_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_peugot_v0 = {
    .name = SUBGHZ_PROTOCOL_PEUGOT_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_peugot_v0_vt,
};

static const SubGhzEncodePluginEntry enc_peugot_v0_entry = {.name = SUBGHZ_PROTOCOL_PEUGOT_V0_NAME, .encoder = &enc_peugot_v0_vt};
static const FlipperAppPluginDescriptor enc_peugot_v0_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_peugot_v0_entry};
const FlipperAppPluginDescriptor* enc_peugot_v0_ep(void) { return &enc_peugot_v0_descriptor; }
