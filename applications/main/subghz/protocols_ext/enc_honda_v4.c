
#define subghz_protocol_honda_v4 enc_honda_v4__subghz_protocol_honda_v4

#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <lib/subghz/protocols/honda_v4.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define HONDA_V4_TE_SHORT        750
#define HONDA_V4_TE_LONG         1100
#define HONDA_V4_PRE             1500
#define HONDA_V4_BITS            51
#define HONDA_V4_PREAMBLE_CYCLES 5
#define HONDA_V4_REPEAT_GAP_US   15000
#define HONDA_V4_REPEAT_DEFAULT  3
#define HONDA_V4_UPLOAD_CAPACITY 256

typedef struct SubGhzProtocolEncoderHondaV4 SubGhzProtocolEncoderHondaV4;
struct SubGhzProtocolEncoderHondaV4 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static size_t
    honda_v4_emit(LevelDuration* up, size_t i, size_t cap, bool level, uint32_t us) {
    if(i < cap) {
        up[i++] = level_duration_make(level, us);
    }
    return i;
}

static bool honda_v4_get_upload(SubGhzProtocolEncoderHondaV4* instance) {
    LevelDuration* up = instance->encoder.upload;
    const size_t cap = HONDA_V4_UPLOAD_CAPACITY;
    size_t index = 0;
    uint64_t data = instance->generic.data;
    uint32_t bits = instance->generic.data_count_bit;
    if(bits == 0 || bits > 64) bits = HONDA_V4_BITS;

    index = honda_v4_emit(up, index, cap, true, HONDA_V4_PRE);
    for(uint8_t n = 0; n < HONDA_V4_PREAMBLE_CYCLES; n++) {
        index = honda_v4_emit(up, index, cap, false, HONDA_V4_PRE);
        index = honda_v4_emit(up, index, cap, true, HONDA_V4_PRE);
    }

    for(int8_t b = (int8_t)bits - 1; b >= 0; b--) {
        bool bit = (data >> b) & 1;
        index = honda_v4_emit(
            up, index, cap, false, bit ? HONDA_V4_TE_LONG : HONDA_V4_TE_SHORT);
        index = honda_v4_emit(up, index, cap, true, HONDA_V4_TE_SHORT);
    }
    index = honda_v4_emit(up, index, cap, false, HONDA_V4_REPEAT_GAP_US);

    instance->encoder.size_upload = index;
    return index > 0 && index < cap;
}

static void* subghz_protocol_encoder_honda_v4_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderHondaV4* instance = malloc(sizeof(SubGhzProtocolEncoderHondaV4));
    instance->base.protocol = &subghz_protocol_honda_v4;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = HONDA_V4_REPEAT_DEFAULT;
    instance->encoder.size_upload = HONDA_V4_UPLOAD_CAPACITY;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

static void subghz_protocol_encoder_honda_v4_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderHondaV4* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

static SubGhzProtocolStatus
    subghz_protocol_encoder_honda_v4_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderHondaV4* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, HONDA_V4_BITS);
    if(ret != SubGhzProtocolStatusOk) return ret;
    flipper_format_read_uint32(flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);
    if(!honda_v4_get_upload(instance)) return SubGhzProtocolStatusErrorEncoderGetUpload;
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

static void subghz_protocol_encoder_honda_v4_stop(void* context) {
    SubGhzProtocolEncoderHondaV4* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

static LevelDuration subghz_protocol_encoder_honda_v4_yield(void* context) {
    SubGhzProtocolEncoderHondaV4* instance = context;
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

static const SubGhzProtocolEncoder enc_honda_v4_vt = {
    .alloc = subghz_protocol_encoder_honda_v4_alloc,
    .free = subghz_protocol_encoder_honda_v4_free,
    .deserialize = subghz_protocol_encoder_honda_v4_deserialize,
    .stop = subghz_protocol_encoder_honda_v4_stop,
    .yield = subghz_protocol_encoder_honda_v4_yield,
};

const SubGhzProtocol subghz_protocol_honda_v4 = {
    .name = HONDA_V4_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_honda_v4_vt,
};

static const SubGhzEncodePluginEntry enc_honda_v4_entry = {
    .name = HONDA_V4_PROTOCOL_NAME,
    .encoder = &enc_honda_v4_vt};
static const FlipperAppPluginDescriptor enc_honda_v4_descriptor = {
    .appid = SUBGHZ_ENCODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION,
    .entry_point = &enc_honda_v4_entry};
const FlipperAppPluginDescriptor* enc_honda_v4_ep(void) {
    return &enc_honda_v4_descriptor;
}
