
#define subghz_protocol_recon_pwm enc_recon_pwm__proto

#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <string.h>

#define RECON_PWM_UPLOAD_CAPACITY 512
#define RECON_PWM_REPEAT_DEFAULT  3

typedef struct {
    const char* name;
    uint32_t te_short;
    uint32_t te_long;
    uint8_t preamble_mult;
    uint8_t term_mult;
    bool value_short_is_one;

    bool complementary;
    uint16_t pre_pairs;
    uint32_t sep_us;
    uint16_t frame_bits;
} ReconPwmSpec;

static const ReconPwmSpec recon_pwm_specs[] = {

    {"iDo 117/111", 450, 1450, 10, 5, true, false, 0, 0, 0},

    {"Microchip-HCS361", 200, 400, 0, 0, false, true, 8, 2000, 67},
    {"Microchip-HCS362", 200, 400, 0, 0, false, true, 12, 600, 69},
};

static const ReconPwmSpec* recon_pwm_find(const char* name) {
    for(size_t i = 0; i < sizeof(recon_pwm_specs) / sizeof(recon_pwm_specs[0]); i++) {
        if(strcmp(recon_pwm_specs[i].name, name) == 0) return &recon_pwm_specs[i];
    }
    return NULL;
}

typedef struct SubGhzProtocolEncoderReconPwm SubGhzProtocolEncoderReconPwm;
struct SubGhzProtocolEncoderReconPwm {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint8_t frame[9];
    uint16_t frame_bits;
};

static const SubGhzProtocol subghz_protocol_recon_pwm = {
    .name = "ReconPwm",
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = NULL};

static size_t recon_push(LevelDuration* up, size_t i, size_t cap, bool level, uint32_t us) {
    if(i < cap) up[i++] = level_duration_make(level, us);
    return i;
}

static bool recon_pwm_build(SubGhzProtocolEncoderReconPwm* instance, const ReconPwmSpec* s) {
    LevelDuration* up = instance->encoder.upload;
    const size_t cap = RECON_PWM_UPLOAD_CAPACITY;
    size_t i = 0;

    if(s->complementary) {

        uint16_t nbits = instance->frame_bits ? instance->frame_bits : s->frame_bits;
        if(nbits == 0 || nbits > 72) return false;
        const uint32_t te = s->te_short;
        const uint32_t tl = s->te_long ? s->te_long : 2 * s->te_short;
        i = recon_push(up, i, cap, false, 20000);
        for(uint16_t k = 0; k < s->pre_pairs; k++) {
            i = recon_push(up, i, cap, true, te);
            if(k + 1 < s->pre_pairs) i = recon_push(up, i, cap, false, te);
        }
        i = recon_push(up, i, cap, false, s->sep_us);
        for(uint16_t b = 0; b < nbits; b++) {
            bool one = (instance->frame[b >> 3] >> (7 - (b & 7))) & 1;
            if(one) {
                i = recon_push(up, i, cap, true, te);
                i = recon_push(up, i, cap, false, tl);
            } else {
                i = recon_push(up, i, cap, true, tl);
                i = recon_push(up, i, cap, false, te);
            }
        }
        i = recon_push(up, i, cap, true, te);
        i = recon_push(up, i, cap, false, 20000);
        instance->encoder.size_upload = i;
        return i > 0 && i < cap;
    }

    uint64_t data = instance->generic.data;
    uint32_t bits = instance->generic.data_count_bit;
    if(bits == 0 || bits > 64) bits = 64;

    i = recon_push(up, i, cap, false, 20000);
    i = recon_push(up, i, cap, true, s->te_short * s->preamble_mult);
    i = recon_push(up, i, cap, false, s->te_short * s->preamble_mult);
    for(int8_t b = (int8_t)bits - 1; b >= 0; b--) {
        bool one = (data >> b) & 1;
        uint32_t low = (one == s->value_short_is_one) ? s->te_short : s->te_long;
        i = recon_push(up, i, cap, true, s->te_short);
        i = recon_push(up, i, cap, false, low);
    }
    i = recon_push(up, i, cap, true, s->te_short * s->term_mult + s->te_short);
    i = recon_push(up, i, cap, false, 20000);

    instance->encoder.size_upload = i;
    return i > 0 && i < cap;
}

static void* subghz_protocol_encoder_recon_pwm_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderReconPwm* instance = malloc(sizeof(SubGhzProtocolEncoderReconPwm));
    instance->base.protocol = &subghz_protocol_recon_pwm;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = RECON_PWM_REPEAT_DEFAULT;
    instance->encoder.size_upload = RECON_PWM_UPLOAD_CAPACITY;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

static void subghz_protocol_encoder_recon_pwm_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderReconPwm* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

static SubGhzProtocolStatus
    subghz_protocol_encoder_recon_pwm_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderReconPwm* instance = context;

    FuriString* proto = furi_string_alloc();
    bool have = flipper_format_rewind(flipper_format) &&
                flipper_format_read_string(flipper_format, "Protocol", proto);
    const ReconPwmSpec* spec = have ? recon_pwm_find(furi_string_get_cstr(proto)) : NULL;
    furi_string_free(proto);
    if(!spec) return SubGhzProtocolStatusErrorParserOthers;

    SubGhzProtocolStatus ret =
        subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret != SubGhzProtocolStatusOk) return ret;

    instance->frame_bits = 0;
    if(spec->frame_bits > 0) {
        uint16_t fbytes = (spec->frame_bits + 7) / 8;
        if(fbytes > sizeof(instance->frame)) return SubGhzProtocolStatusErrorParserOthers;
        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_hex(flipper_format, "Frame", instance->frame, fbytes)) {
            return SubGhzProtocolStatusErrorParserOthers;
        }
        instance->frame_bits = spec->frame_bits;
    }

    flipper_format_read_uint32(flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);
    if(!recon_pwm_build(instance, spec)) return SubGhzProtocolStatusErrorEncoderGetUpload;
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

static void subghz_protocol_encoder_recon_pwm_stop(void* context) {
    SubGhzProtocolEncoderReconPwm* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

static LevelDuration subghz_protocol_encoder_recon_pwm_yield(void* context) {
    SubGhzProtocolEncoderReconPwm* instance = context;
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

static const SubGhzProtocolEncoder enc_recon_pwm_vt = {
    .alloc = subghz_protocol_encoder_recon_pwm_alloc,
    .free = subghz_protocol_encoder_recon_pwm_free,
    .deserialize = subghz_protocol_encoder_recon_pwm_deserialize,
    .stop = subghz_protocol_encoder_recon_pwm_stop,
    .yield = subghz_protocol_encoder_recon_pwm_yield,
};

static const SubGhzEncodePluginEntry enc_recon_pwm_entry = {
    .name = "*",
    .encoder = &enc_recon_pwm_vt};
static const FlipperAppPluginDescriptor enc_recon_pwm_descriptor = {
    .appid = SUBGHZ_ENCODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION,
    .entry_point = &enc_recon_pwm_entry};
const FlipperAppPluginDescriptor* enc_recon_pwm_ep(void) {
    return &enc_recon_pwm_descriptor;
}
