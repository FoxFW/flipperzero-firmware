/* enc_tesla_charge.fal : SD encoder for tesla_charge (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_tesla_charge__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_yield enc_tesla_charge__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_tesla_charge_alloc enc_tesla_charge__subghz_protocol_encoder_tesla_charge_alloc
#define subghz_protocol_encoder_tesla_charge_deserialize enc_tesla_charge__subghz_protocol_encoder_tesla_charge_deserialize
#define subghz_protocol_encoder_tesla_charge_stop enc_tesla_charge__subghz_protocol_encoder_tesla_charge_stop
#define subghz_protocol_tesla_charge enc_tesla_charge__subghz_protocol_tesla_charge
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/tesla_charge.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/subghz/protocols/common.h>

#define TESLA_CHARGE_TE_DEFAULT 400

struct SubGhzProtocolEncoderTeslaCharge {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
};

static const SubGhzBlockConst subghz_protocol_tesla_charge_const = {
    .te_short = 400,
    .te_long = 800,
    .te_delta = 140, // ~35% of te_short
    .min_count_bit_for_found = 50,
};

/**
 * Generating an upload from data.
 * @param instance Pointer to a SubGhzProtocolEncoderTeslaCharge instance
 * @return true On success
 */
static bool
    subghz_protocol_encoder_tesla_charge_get_upload(SubGhzProtocolEncoderTeslaCharge* instance);

/**
 * Generating an upload from data.
 * @param instance Pointer to a SubGhzProtocolEncoderTeslaCharge instance
 * @return true On success
 */
static bool
    subghz_protocol_encoder_tesla_charge_get_upload(SubGhzProtocolEncoderTeslaCharge* instance) {
    furi_assert(instance);

    const uint32_t te = instance->te ? instance->te : TESLA_CHARGE_TE_DEFAULT;
    size_t index = 0;
    const size_t cap = instance->encoder.size_upload;

    // --- Preamble: 12 short high/low cells + a trailing short high ---
    for(uint8_t i = 0; i < 12; i++) {
        if(index + 2 > cap) return false;
        instance->encoder.upload[index++] = level_duration_make(true, te);
        instance->encoder.upload[index++] = level_duration_make(false, te);
    }
    if(index + 2 > cap) return false;
    instance->encoder.upload[index++] = level_duration_make(true, te);
    // --- Sync gap: ~3*te low ---
    instance->encoder.upload[index++] = level_duration_make(false, te * 3);

    // --- Payload: biphase, MSB first ---
    // Rule (verified bit-exact against captures & the manchester decoder):
    //   first  half-bit level = !prev_second   (transition on every bit boundary)
    //   second half-bit level = !bit
    // Entering the payload the line is low (the sync gap), so prev_second = 0.
    uint8_t prev_second = 0;
    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        uint8_t bit = (instance->generic.data >> (i - 1)) & 1;
        bool first = (prev_second == 0); // !prev_second -> high when prev was low
        bool second = (bit == 0); // !bit

        if(index + 2 > cap) return false;
        if(first == second) {
            // level held for the whole cell -> single 2*te pulse
            instance->encoder.upload[index++] = level_duration_make(first, te * 2);
        } else {
            instance->encoder.upload[index++] = level_duration_make(first, te);
            instance->encoder.upload[index++] = level_duration_make(second, te);
        }
        prev_second = second ? 1 : 0;
    }

    // --- Inter-burst gap before the next repeat ---
    if(index + 1 > cap) return false;
    instance->encoder.upload[index++] = level_duration_make(false, te * 3);

    instance->encoder.size_upload = index;
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

/* ---------------------------------------------------------------------------
 * Encoder
 * ------------------------------------------------------------------------ */

void* subghz_protocol_encoder_tesla_charge_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderTeslaCharge* instance =
        malloc(sizeof(SubGhzProtocolEncoderTeslaCharge));

    instance->base.protocol = &subghz_protocol_tesla_charge;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 5;
    // preamble(26) + sync + payload(up to 100) + gap; 256 is comfortably safe
    instance->encoder.size_upload = 256;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    instance->te = TESLA_CHARGE_TE_DEFAULT;
    return instance;
}

SubGhzProtocolStatus subghz_protocol_encoder_tesla_charge_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderTeslaCharge* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_tesla_charge_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        // Optional TE (fixed protocol, default 400us)
        if(!flipper_format_read_uint32(flipper_format, "TE", (uint32_t*)&instance->te, 1)) {
            instance->te = TESLA_CHARGE_TE_DEFAULT;
        }
        // Optional value
        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        if(!subghz_protocol_encoder_tesla_charge_get_upload(instance)) {
            instance->encoder.front = 0;
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_tesla_charge_stop(void* context) {
    SubGhzProtocolEncoderTeslaCharge* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

static const SubGhzProtocolEncoder enc_tesla_charge_vt = {
    .alloc = subghz_protocol_encoder_tesla_charge_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_tesla_charge_deserialize,
    .stop = subghz_protocol_encoder_tesla_charge_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_tesla_charge = {
    .name = TESLA_CHARGE_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_tesla_charge_vt,
};

static const SubGhzEncodePluginEntry enc_tesla_charge_entry = {.name = TESLA_CHARGE_PROTOCOL_NAME, .encoder = &enc_tesla_charge_vt};
static const FlipperAppPluginDescriptor enc_tesla_charge_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_tesla_charge_entry};
const FlipperAppPluginDescriptor* enc_tesla_charge_ep(void) { return &enc_tesla_charge_descriptor; }
