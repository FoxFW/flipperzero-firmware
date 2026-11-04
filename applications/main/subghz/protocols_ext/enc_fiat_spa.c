/* enc_fiat_spa.fal : SD encoder for fiat_spa (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_fiat_spa_alloc enc_fiat_spa__subghz_protocol_encoder_fiat_spa_alloc
#define subghz_protocol_encoder_fiat_spa_deserialize enc_fiat_spa__subghz_protocol_encoder_fiat_spa_deserialize
#define subghz_protocol_encoder_fiat_spa_free enc_fiat_spa__subghz_protocol_encoder_fiat_spa_free
#define subghz_protocol_encoder_fiat_spa_stop enc_fiat_spa__subghz_protocol_encoder_fiat_spa_stop
#define subghz_protocol_encoder_fiat_spa_yield enc_fiat_spa__subghz_protocol_encoder_fiat_spa_yield
#define subghz_protocol_fiat_spa enc_fiat_spa__subghz_protocol_fiat_spa
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/fiat_spa.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/subghz/protocols/common.h>

#define FIAT_SPA_PREAMBLE_PAIRS 150
#define FIAT_SPA_GAP_US 800
#define FIAT_SPA_TOTAL_BURSTS 3
#define FIAT_SPA_INTER_BURST_GAP 25000
#define FIAT_SPA_UPLOAD_MAX 1328

struct SubGhzProtocolEncoderFiatSpa {
    SubGhzProtocolEncoderBase base;
    void* decoder_callback;
    void* decoder_context;
    SubGhzBlockGeneric generic;
    SubGhzProtocolBlockEncoder encoder;
    uint32_t hop;
    uint32_t fix;
    uint8_t endbyte;
};

static const SubGhzBlockConst subghz_protocol_fiat_spa_const = {
    .te_short = 200,
    .te_long = 400,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

static void subghz_protocol_encoder_fiat_spa_get_upload(SubGhzProtocolEncoderFiatSpa* instance);

static void subghz_protocol_encoder_fiat_spa_get_upload(SubGhzProtocolEncoderFiatSpa* instance) {
    furi_assert(instance);
    size_t index = 0;
    uint32_t te_short = subghz_protocol_fiat_spa_const.te_short;
    uint32_t te_long = subghz_protocol_fiat_spa_const.te_long;

    uint64_t data = ((uint64_t)instance->hop << 32) | instance->fix;
    uint8_t endbyte_to_send = instance->endbyte >> 1;

    for(uint8_t burst = 0; burst < FIAT_SPA_TOTAL_BURSTS; burst++) {
        if(burst > 0) {
            instance->encoder.upload[index++] =
                level_duration_make(false, FIAT_SPA_INTER_BURST_GAP);
        }
        for(int i = 0; i < FIAT_SPA_PREAMBLE_PAIRS; i++) {
            instance->encoder.upload[index++] = level_duration_make(true, te_short);
            instance->encoder.upload[index++] = level_duration_make(false, te_short);
        }
        instance->encoder.upload[index - 1] = level_duration_make(false, FIAT_SPA_GAP_US);

        bool first_bit = (data >> 63) & 1;
        if(first_bit) {
            instance->encoder.upload[index++] = level_duration_make(true, te_long);
        } else {
            instance->encoder.upload[index++] = level_duration_make(true, te_short);
            instance->encoder.upload[index++] = level_duration_make(false, te_long);
        }
        bool prev_bit = first_bit;

        for(int bit = 62; bit >= 0; bit--) {
            bool curr_bit = (data >> bit) & 1;
            if(!prev_bit && !curr_bit) {
                instance->encoder.upload[index++] = level_duration_make(true, te_short);
                instance->encoder.upload[index++] = level_duration_make(false, te_short);
            } else if(!prev_bit && curr_bit) {
                instance->encoder.upload[index++] = level_duration_make(true, te_long);
            } else if(prev_bit && !curr_bit) {
                instance->encoder.upload[index++] = level_duration_make(false, te_long);
            } else {
                instance->encoder.upload[index++] = level_duration_make(false, te_short);
                instance->encoder.upload[index++] = level_duration_make(true, te_short);
            }
            prev_bit = curr_bit;
        }

        for(int bit = 5; bit >= 0; bit--) {
            bool curr_bit = (endbyte_to_send >> bit) & 1;
            if(!prev_bit && !curr_bit) {
                instance->encoder.upload[index++] = level_duration_make(true, te_short);
                instance->encoder.upload[index++] = level_duration_make(false, te_short);
            } else if(!prev_bit && curr_bit) {
                instance->encoder.upload[index++] = level_duration_make(true, te_long);
            } else if(prev_bit && !curr_bit) {
                instance->encoder.upload[index++] = level_duration_make(false, te_long);
            } else {
                instance->encoder.upload[index++] = level_duration_make(false, te_short);
                instance->encoder.upload[index++] = level_duration_make(true, te_short);
            }
            prev_bit = curr_bit;
        }

        if(prev_bit) {
            instance->encoder.upload[index++] = level_duration_make(false, te_short);
        }
        instance->encoder.upload[index++] = level_duration_make(false, te_short * 8);
    }

    instance->encoder.size_upload = index;
    instance->encoder.front = 0;
}

void* subghz_protocol_encoder_fiat_spa_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderFiatSpa* instance = malloc(sizeof(SubGhzProtocolEncoderFiatSpa));
    instance->base.protocol = &subghz_protocol_fiat_spa;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 3;
    instance->encoder.size_upload = FIAT_SPA_UPLOAD_MAX;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    instance->hop = 0;
    instance->fix = 0;
    instance->endbyte = 0;
    return instance;
}

void subghz_protocol_encoder_fiat_spa_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderFiatSpa* instance = context;
    if(instance->encoder.upload) {
        free(instance->encoder.upload);
    }
    free(instance);
}

void subghz_protocol_encoder_fiat_spa_stop(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderFiatSpa* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_fiat_spa_yield(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderFiatSpa* instance = context;
    if(!instance->encoder.is_running || instance->encoder.repeat == 0) {
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

SubGhzProtocolStatus subghz_protocol_encoder_fiat_spa_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderFiatSpa* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) break;

        instance->hop = (uint32_t)(instance->generic.data >> 32);
        instance->fix = (uint32_t)(instance->generic.data & 0xFFFFFFFF);

        uint32_t endbyte_temp = 0;
        if(!flipper_format_read_uint32(flipper_format, "EndByte", &endbyte_temp, 1)) {
            instance->endbyte = 0;
        } else {
            instance->endbyte = (uint8_t)endbyte_temp;
        }

        instance->generic.cnt = instance->hop;
        instance->generic.serial = instance->fix;
        instance->generic.btn = instance->endbyte;

        subghz_protocol_encoder_fiat_spa_get_upload(instance);

        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);
    return ret;
}

static const SubGhzProtocolEncoder enc_fiat_spa_vt = {
    .alloc = subghz_protocol_encoder_fiat_spa_alloc,
    .free = subghz_protocol_encoder_fiat_spa_free,
    .deserialize = subghz_protocol_encoder_fiat_spa_deserialize,
    .stop = subghz_protocol_encoder_fiat_spa_stop,
    .yield = subghz_protocol_encoder_fiat_spa_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_fiat_spa = {
    .name = SUBGHZ_PROTOCOL_FIAT_SPA_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_fiat_spa_vt,
};

static const SubGhzEncodePluginEntry enc_fiat_spa_entry = {.name = SUBGHZ_PROTOCOL_FIAT_SPA_NAME, .encoder = &enc_fiat_spa_vt};
static const FlipperAppPluginDescriptor enc_fiat_spa_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_fiat_spa_entry};
const FlipperAppPluginDescriptor* enc_fiat_spa_ep(void) { return &enc_fiat_spa_descriptor; }
