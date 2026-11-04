/* enc_honeywell.fal : SD encoder for honeywell (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_honeywell_alloc enc_honeywell__subghz_protocol_encoder_honeywell_alloc
#define subghz_protocol_encoder_honeywell_deserialize enc_honeywell__subghz_protocol_encoder_honeywell_deserialize
#define subghz_protocol_encoder_honeywell_free enc_honeywell__subghz_protocol_encoder_honeywell_free
#define subghz_protocol_encoder_honeywell_stop enc_honeywell__subghz_protocol_encoder_honeywell_stop
#define subghz_protocol_encoder_honeywell_yield enc_honeywell__subghz_protocol_encoder_honeywell_yield
#define subghz_protocol_honeywell enc_honeywell__subghz_protocol_honeywell
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/honeywell.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolHoneywell"

struct SubGhzProtocolEncoderHoneywell {
    SubGhzProtocolEncoderBase base;
    SubGhzBlockGeneric generic;
    SubGhzProtocolBlockEncoder encoder;
};

static const SubGhzBlockConst subghz_protocol_honeywell_const = {
    .te_long = 280,
    .te_short = 143,
    .te_delta = 51,
    .min_count_bit_for_found = 64,
};

static LevelDuration
    subghz_protocol_encoder_honeywell_add_duration_to_upload(ManchesterEncoderResult result);
static void
    subghz_protocol_encoder_honeywell_get_upload(SubGhzProtocolEncoderHoneywell* instance);

static LevelDuration
    subghz_protocol_encoder_honeywell_add_duration_to_upload(ManchesterEncoderResult result) {
    LevelDuration data = {.duration = 0, .level = 0};
    switch(result) {
    case ManchesterEncoderResultShortLow:
        data.duration = subghz_protocol_honeywell_const.te_short;
        data.level = false;
        break;
    case ManchesterEncoderResultLongLow:
        data.duration = subghz_protocol_honeywell_const.te_long;
        data.level = false;
        break;
    case ManchesterEncoderResultLongHigh:
        data.duration = subghz_protocol_honeywell_const.te_long;
        data.level = true;
        break;
    case ManchesterEncoderResultShortHigh:
        data.duration = subghz_protocol_honeywell_const.te_short;
        data.level = true;
        break;

    default:
        furi_crash("SubGhz: ManchesterEncoderResult is incorrect.");
        break;
    }
    return level_duration_make(data.level, data.duration);
}

static void
    subghz_protocol_encoder_honeywell_get_upload(SubGhzProtocolEncoderHoneywell* instance) {
    furi_assert(instance);
    size_t index = 0;

    ManchesterEncoderState enc_state;
    manchester_encoder_reset(&enc_state);
    ManchesterEncoderResult result;

    for(uint8_t i = 63; i > 0; i--) {
        if(!manchester_encoder_advance(
               &enc_state, bit_read(instance->generic.data, i - 1), &result)) {
            instance->encoder.upload[index++] =
                subghz_protocol_encoder_honeywell_add_duration_to_upload(result);
            manchester_encoder_advance(
                &enc_state, bit_read(instance->generic.data, i - 1), &result);
        }
        instance->encoder.upload[index++] =
            subghz_protocol_encoder_honeywell_add_duration_to_upload(result);
    }
    instance->encoder.upload[index] = subghz_protocol_encoder_honeywell_add_duration_to_upload(
        manchester_encoder_finish(&enc_state));
    if(level_duration_get_level(instance->encoder.upload[index])) {
        index++;
    }

    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)subghz_protocol_honeywell_const.te_long * 300);

    instance->encoder.size_upload = index;
}

void* subghz_protocol_encoder_honeywell_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderHoneywell* instance = malloc(sizeof(SubGhzProtocolEncoderHoneywell));

    instance->base.protocol = &subghz_protocol_honeywell;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 4;
    instance->encoder.size_upload = 64 * 2 + 10;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_honeywell_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderHoneywell* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_honeywell_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderHoneywell* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        if(SubGhzProtocolStatusOk !=
           subghz_block_generic_deserialize(&instance->generic, flipper_format)) {
            FURI_LOG_E(TAG, "Deserialize error");
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_encoder_honeywell_get_upload(instance);

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }

        instance->encoder.is_running = true;

        res = SubGhzProtocolStatusOk;
    } while(false);

    return res;
}

void subghz_protocol_encoder_honeywell_stop(void* context) {
    SubGhzProtocolEncoderHoneywell* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_honeywell_yield(void* context) {
    SubGhzProtocolEncoderHoneywell* instance = context;

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

static const SubGhzProtocolEncoder enc_honeywell_vt = {
    .alloc = subghz_protocol_encoder_honeywell_alloc,
    .free = subghz_protocol_encoder_honeywell_free,
    .deserialize = subghz_protocol_encoder_honeywell_deserialize,
    .stop = subghz_protocol_encoder_honeywell_stop,
    .yield = subghz_protocol_encoder_honeywell_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_honeywell = {
    .name = SUBGHZ_PROTOCOL_HONEYWELL_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send |
            SubGhzProtocolFlag_Sensors,
    .decoder = NULL,
    .encoder = &enc_honeywell_vt,
};

static const SubGhzEncodePluginEntry enc_honeywell_entry = {.name = SUBGHZ_PROTOCOL_HONEYWELL_NAME, .encoder = &enc_honeywell_vt};
static const FlipperAppPluginDescriptor enc_honeywell_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_honeywell_entry};
const FlipperAppPluginDescriptor* enc_honeywell_ep(void) { return &enc_honeywell_descriptor; }
