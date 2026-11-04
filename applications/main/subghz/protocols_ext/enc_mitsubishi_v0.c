/* enc_mitsubishi_v0.fal : SD encoder for mitsubishi_v0 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_yield enc_mitsubishi_v0__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_mitsubishi_v0_alloc enc_mitsubishi_v0__subghz_protocol_encoder_mitsubishi_v0_alloc
#define subghz_protocol_encoder_mitsubishi_v0_deserialize enc_mitsubishi_v0__subghz_protocol_encoder_mitsubishi_v0_deserialize
#define subghz_protocol_encoder_mitsubishi_v0_free enc_mitsubishi_v0__subghz_protocol_encoder_mitsubishi_v0_free
#define subghz_protocol_encoder_mitsubishi_v0_stop enc_mitsubishi_v0__subghz_protocol_encoder_mitsubishi_v0_stop
#define subghz_protocol_mitsubishi_v0 enc_mitsubishi_v0__subghz_protocol_mitsubishi_v0
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/mitsubishi_v0.h>
#include <inttypes.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define MITSUBISHI_V0_PREAMBLE_COUNT 100
#define MITSUBISHI_V0_BIT_TE         250
#define MITSUBISHI_V0_BIT_TE_GAP     500
#define MITSUBISHI_V0_BIT_COUNT      96

struct SubGhzProtocolEncoderMitsubishiV0 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    size_t upload_capacity;
};

static void mitsubishi_v0_scramble(uint8_t* payload, uint16_t counter);
static void subghz_protocol_encoder_mitsubishi_v0_get_upload(SubGhzProtocolEncoderMitsubishiV0* instance);

static void mitsubishi_v0_scramble(uint8_t* payload, uint16_t counter) {
    uint8_t hi = (counter >> 8) & 0xFF;
    uint8_t lo = counter & 0xFF;
    uint8_t mask1 = (hi & 0xAA) | (lo & 0x55);
    uint8_t mask2 = (lo & 0xAA) | (hi & 0x55);
    uint8_t mask3 = mask1 ^ mask2;

    for(int i = 0; i < 5; i++) {
        payload[i] ^= mask3;
    }

    for(int i = 0; i < 8; i++) {
        payload[i] = ~payload[i];
    }
}

static void subghz_protocol_encoder_mitsubishi_v0_get_upload(SubGhzProtocolEncoderMitsubishiV0* instance) {
    size_t index = 0;
    uint8_t payload[12] = {0};

    payload[0] = (instance->generic.serial >> 24) & 0xFF;
    payload[1] = (instance->generic.serial >> 16) & 0xFF;
    payload[2] = (instance->generic.serial >> 8) & 0xFF;
    payload[3] = instance->generic.serial & 0xFF;
    payload[4] = (instance->generic.cnt >> 8) & 0xFF;
    payload[5] = instance->generic.cnt & 0xFF;
    payload[6] = instance->generic.btn;
    payload[9] = 0x5A;
    payload[10] = 0xFF;
    payload[11] = 0xFF;

    mitsubishi_v0_scramble(payload, (uint16_t)instance->generic.cnt);

    for(int i = 0; i < MITSUBISHI_V0_PREAMBLE_COUNT; i++) {
        instance->encoder.upload[index++] = level_duration_make(true, MITSUBISHI_V0_BIT_TE);
        instance->encoder.upload[index++] = level_duration_make(false, MITSUBISHI_V0_BIT_TE);
    }

    for(int i = 0; i < 95; i++) {
        instance->encoder.upload[index++] = level_duration_make(true, MITSUBISHI_V0_BIT_TE);
        instance->encoder.upload[index++] = level_duration_make(false, MITSUBISHI_V0_BIT_TE);
    }

    for(int i = 0; i < 12; i++) {
        for(int bit = 7; bit >= 0; bit--) {
            bool curr = (payload[i] >> bit) & 1;
            if(curr) {
                instance->encoder.upload[index++] = level_duration_make(true, MITSUBISHI_V0_BIT_TE);
                instance->encoder.upload[index++] = level_duration_make(false, MITSUBISHI_V0_BIT_TE_GAP);
            } else {
                instance->encoder.upload[index++] = level_duration_make(true, MITSUBISHI_V0_BIT_TE_GAP);
                instance->encoder.upload[index++] = level_duration_make(false, MITSUBISHI_V0_BIT_TE);
            }
        }
    }

    instance->encoder.size_upload = index;
    instance->encoder.front = 0;
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

void* subghz_protocol_encoder_mitsubishi_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderMitsubishiV0* instance = calloc(1, sizeof(SubGhzProtocolEncoderMitsubishiV0));
    furi_check(instance);

    instance->base.protocol = &subghz_protocol_mitsubishi_v0;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 5;
    instance->encoder.size_upload = 0;

    instance->upload_capacity = (MITSUBISHI_V0_PREAMBLE_COUNT * 2) + 20 + (MITSUBISHI_V0_BIT_COUNT * 2) + 2;
    instance->encoder.upload = calloc(instance->upload_capacity, sizeof(LevelDuration));

    return instance;
}

void subghz_protocol_encoder_mitsubishi_v0_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderMitsubishiV0* instance = context;
    if(instance->encoder.upload) free(instance->encoder.upload);
    free(instance);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_mitsubishi_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    SubGhzProtocolEncoderMitsubishiV0* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) break;
        if(!flipper_format_rewind(flipper_format)) break;
        flipper_format_read_uint32(flipper_format, "Serial", &instance->generic.serial, 1);
        flipper_format_read_uint32(flipper_format, "Cnt", &instance->generic.cnt, 1);
        uint32_t btn_temp = 0;
        flipper_format_read_uint32(flipper_format, "Btn", &btn_temp, 1);
        instance->generic.btn = (uint8_t)btn_temp;

        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(instance->generic.btn);
        }
        subghz_custom_btn_set_max(4);

        subghz_protocol_encoder_mitsubishi_v0_get_upload(instance);
        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);
    return ret;
}

void subghz_protocol_encoder_mitsubishi_v0_stop(void* context) {
    ((SubGhzProtocolEncoderMitsubishiV0*)context)->encoder.is_running = false;
}

static const SubGhzProtocolEncoder enc_mitsubishi_v0_vt = {
    .alloc = subghz_protocol_encoder_mitsubishi_v0_alloc,
    .free = subghz_protocol_encoder_mitsubishi_v0_free,
    .deserialize = subghz_protocol_encoder_mitsubishi_v0_deserialize,
    .stop = subghz_protocol_encoder_mitsubishi_v0_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_mitsubishi_v0 = {
    .name = MITSUBISHI_PROTOCOL_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 |
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_mitsubishi_v0_vt,
};

static const SubGhzEncodePluginEntry enc_mitsubishi_v0_entry = {.name = MITSUBISHI_PROTOCOL_V0_NAME, .encoder = &enc_mitsubishi_v0_vt};
static const FlipperAppPluginDescriptor enc_mitsubishi_v0_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_mitsubishi_v0_entry};
const FlipperAppPluginDescriptor* enc_mitsubishi_v0_ep(void) { return &enc_mitsubishi_v0_descriptor; }
