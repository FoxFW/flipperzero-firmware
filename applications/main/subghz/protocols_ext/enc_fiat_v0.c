/* enc_fiat_v0.fal : SD encoder for fiat_v0 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define fiat_protocol_v0 enc_fiat_v0__fiat_protocol_v0
#define subghz_protocol_encoder_common_free enc_fiat_v0__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_yield enc_fiat_v0__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_fiat_v0_alloc enc_fiat_v0__subghz_protocol_encoder_fiat_v0_alloc
#define subghz_protocol_encoder_fiat_v0_deserialize enc_fiat_v0__subghz_protocol_encoder_fiat_v0_deserialize
#define subghz_protocol_encoder_fiat_v0_stop enc_fiat_v0__subghz_protocol_encoder_fiat_v0_stop
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/fiat_v0.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/subghz/protocols/common.h>

#define FIAT_V0_PREAMBLE_PAIRS  150
#define FIAT_V0_GAP_US          800
#define FIAT_V0_TOTAL_BURSTS    3
#define FIAT_V0_INTER_BURST_GAP 25000
#define FIAT_V0_UPLOAD_CAPACITY 1328U

struct SubGhzProtocolEncoderFiatV0 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t hop;
    uint32_t fix;
    uint8_t endbyte;
};

static const SubGhzBlockConst subghz_protocol_fiat_v0_const = {
    .te_short = 200,
    .te_long = 400,
    .te_delta = 100,
    .min_count_bit_for_found = 71,
};

static void subghz_protocol_encoder_fiat_v0_get_upload(SubGhzProtocolEncoderFiatV0* instance);

static void subghz_protocol_encoder_fiat_v0_get_upload(SubGhzProtocolEncoderFiatV0* instance) {
    furi_check(instance);
    LevelDuration* up = instance->encoder.upload;
    if(up == NULL) return;

    size_t index = 0;
    uint32_t te_short = subghz_protocol_fiat_v0_const.te_short;
    uint32_t te_long = subghz_protocol_fiat_v0_const.te_long;

    uint64_t data = ((uint64_t)instance->hop << 32) | instance->fix;
    uint8_t endbyte_to_send = instance->endbyte >> 1;

    for(uint8_t burst = 0; burst < FIAT_V0_TOTAL_BURSTS; burst++) {
        if(burst > 0) {
            up[index++] = level_duration_make(false, FIAT_V0_INTER_BURST_GAP);
        }

        for(int i = 0; i < FIAT_V0_PREAMBLE_PAIRS; i++) {
            up[index++] = level_duration_make(true, te_short);
            up[index++] = level_duration_make(false, te_short);
        }
        if(index > 0) up[index - 1] = level_duration_make(false, FIAT_V0_GAP_US);

        bool first_bit = (data >> 63) & 1;
        if(first_bit) {
            up[index++] = level_duration_make(true, te_long);
        } else {
            up[index++] = level_duration_make(true, te_short);
            up[index++] = level_duration_make(false, te_long);
        }
        bool prev_bit = first_bit;

        for(int bit = 62; bit >= 0; bit--) {
            bool curr_bit = (data >> bit) & 1;
            if(!prev_bit && !curr_bit) {
                up[index++] = level_duration_make(true, te_short);
                up[index++] = level_duration_make(false, te_short);
            } else if(!prev_bit && curr_bit) {
                up[index++] = level_duration_make(true, te_long);
            } else if(prev_bit && !curr_bit) {
                up[index++] = level_duration_make(false, te_long);
            } else {
                up[index++] = level_duration_make(false, te_short);
                up[index++] = level_duration_make(true, te_short);
            }
            prev_bit = curr_bit;
        }

        for(int bit = 5; bit >= 0; bit--) {
            bool curr_bit = (endbyte_to_send >> bit) & 1;
            if(!prev_bit && !curr_bit) {
                up[index++] = level_duration_make(true, te_short);
                up[index++] = level_duration_make(false, te_short);
            } else if(!prev_bit && curr_bit) {
                up[index++] = level_duration_make(true, te_long);
            } else if(prev_bit && !curr_bit) {
                up[index++] = level_duration_make(false, te_long);
            } else {
                up[index++] = level_duration_make(false, te_short);
                up[index++] = level_duration_make(true, te_short);
            }
            prev_bit = curr_bit;
        }

        if(prev_bit) {
            up[index++] = level_duration_make(false, te_short);
        }
        up[index++] = level_duration_make(false, te_short * 8);
    }

    instance->encoder.size_upload = index;
    instance->encoder.front = 0;
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

void* subghz_protocol_encoder_fiat_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderFiatV0* instance = calloc(1, sizeof(SubGhzProtocolEncoderFiatV0));
    furi_check(instance);

    instance->base.protocol = &fiat_protocol_v0;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 10;
    instance->encoder.size_upload = 0;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;

    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_fiat_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderFiatV0* instance = context;

    instance->encoder.is_running = false;
    instance->encoder.front = 0;

    do {
        flipper_format_rewind(flipper_format);
        uint32_t temp_data = 0;
        if(!flipper_format_read_uint32(flipper_format, "Bit", &temp_data, 1)) {
            break;
        }
        if(temp_data != 64 && temp_data != 71) {
            break;
        }
        instance->generic.data_count_bit = temp_data;

        flipper_format_rewind(flipper_format);
        uint8_t key_bytes[8];
        if(!flipper_format_read_hex(flipper_format, "Key", key_bytes, 8)) {
            break;
        }
        uint64_t key = 0;
        for(int i = 0; i < 8; i++) {
            key = (key << 8) | key_bytes[i];
        }
        instance->generic.data = key;
        instance->hop = (uint32_t)(key >> 32);
        instance->fix = (uint32_t)(key & 0xFFFFFFFFU);

        uint32_t eb_read = 0;
        flipper_format_rewind(flipper_format);
        bool have_endbyte = flipper_format_read_uint32(flipper_format, "EndByte", &eb_read, 1);

        flipper_format_rewind(flipper_format);
        uint32_t btn_u32 = 0;
        flipper_format_read_uint32(flipper_format, "Btn", &btn_u32, 1);

        if(have_endbyte) {
            instance->endbyte = (uint8_t)(eb_read & 0x7FU);
        } else {
            instance->endbyte = (uint8_t)(btn_u32 & 0x7FU);
        }

        instance->generic.btn = instance->endbyte;
        instance->generic.cnt = instance->hop;
        instance->generic.serial = instance->fix;

        flipper_format_rewind(flipper_format);
        uint32_t repeat = 10;
        flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
        instance->encoder.repeat = repeat;

        instance->encoder.upload = malloc(FIAT_V0_UPLOAD_CAPACITY * sizeof(LevelDuration));
        subghz_protocol_encoder_fiat_v0_get_upload(instance);
        instance->encoder.is_running = true;

        return SubGhzProtocolStatusOk;
    } while(false);

    return SubGhzProtocolStatusError;
}

void subghz_protocol_encoder_fiat_v0_stop(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderFiatV0* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

static const SubGhzProtocolEncoder enc_fiat_v0_vt = {
    .alloc = subghz_protocol_encoder_fiat_v0_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_fiat_v0_deserialize,
    .stop = subghz_protocol_encoder_fiat_v0_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol fiat_protocol_v0 = {
    .name = FIAT_PROTOCOL_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_fiat_v0_vt,
};

static const SubGhzEncodePluginEntry enc_fiat_v0_entry = {.name = FIAT_PROTOCOL_V0_NAME, .encoder = &enc_fiat_v0_vt};
static const FlipperAppPluginDescriptor enc_fiat_v0_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_fiat_v0_entry};
const FlipperAppPluginDescriptor* enc_fiat_v0_ep(void) { return &enc_fiat_v0_descriptor; }
