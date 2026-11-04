/* enc_santa_fe.fal : SD encoder for santa_fe (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define santa_fe_protocol enc_santa_fe__santa_fe_protocol
#define subghz_protocol_encoder_santa_fe_alloc enc_santa_fe__subghz_protocol_encoder_santa_fe_alloc
#define subghz_protocol_encoder_santa_fe_deserialize enc_santa_fe__subghz_protocol_encoder_santa_fe_deserialize
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/santa_fe.h>
#include <furi.h>
#include <string.h>
#include <lib/subghz/types.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include <flipper_format/flipper_format.h>
#include <lib/subghz/protocols/pp_auto_common.h>
#include <lib/subghz/protocols/common.h>

#define SF_TE_SHORT    125
#define SF_TE_LONG     375
#define SF_SYNC_US     12000
#define SF_GAP_US      15000
#define SF_BIT_COUNT   80
#define SF_BYTES       10
#define SF_UPLOAD_CAPACITY ((2U) + (2U * SF_BIT_COUNT) + 2U)

struct SubGhzProtocolEncoderSantaFe {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint32_t rolling;
};

/* host bridge: live firmware functions/state this encoder needs (set by the loader) */
typedef struct {
    __typeof__(&ppa_emit) ppa_emit;
    __typeof__(&ppa_encoder_buffer_ensure) ppa_encoder_buffer_ensure;
    __typeof__(&ppa_encoder_free) ppa_encoder_free;
    __typeof__(&ppa_encoder_read_fields) ppa_encoder_read_fields;
    __typeof__(&ppa_encoder_read_repeat) ppa_encoder_read_repeat;
    __typeof__(&ppa_encoder_stop) ppa_encoder_stop;
    __typeof__(&ppa_encoder_yield) ppa_encoder_yield;
    __typeof__(&ppa_verify_protocol_name) ppa_verify_protocol_name;
} enc_santa_fe_host_t;
static const void* enc_santa_fe_host_raw;
#define ENC_SANTA_FE_HOST ((const enc_santa_fe_host_t*)enc_santa_fe_host_raw)
#define ppa_emit (ENC_SANTA_FE_HOST->ppa_emit)
#define ppa_encoder_buffer_ensure (ENC_SANTA_FE_HOST->ppa_encoder_buffer_ensure)
#define ppa_encoder_free (ENC_SANTA_FE_HOST->ppa_encoder_free)
#define ppa_encoder_read_fields (ENC_SANTA_FE_HOST->ppa_encoder_read_fields)
#define ppa_encoder_read_repeat (ENC_SANTA_FE_HOST->ppa_encoder_read_repeat)
#define ppa_encoder_stop (ENC_SANTA_FE_HOST->ppa_encoder_stop)
#define ppa_encoder_yield (ENC_SANTA_FE_HOST->ppa_encoder_yield)
#define ppa_verify_protocol_name (ENC_SANTA_FE_HOST->ppa_verify_protocol_name)

static uint8_t sf_crc8(const uint8_t* data, size_t len);
static bool sf_build_upload(SubGhzProtocolEncoderSantaFe* instance, uint8_t button);

static uint8_t sf_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(int b = 0; b < 8; b++) {
            if(crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x31);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

static bool sf_build_upload(SubGhzProtocolEncoderSantaFe* instance, uint8_t button) {
    uint8_t pkt[SF_BYTES];
    pkt[0] = (uint8_t)(instance->rolling >> 24);
    pkt[1] = (uint8_t)(instance->rolling >> 16);
    pkt[2] = (uint8_t)(instance->rolling >> 8);
    pkt[3] = (uint8_t)(instance->rolling);
    pkt[4] = (uint8_t)(instance->generic.serial >> 16);
    pkt[5] = (uint8_t)(instance->generic.serial >> 8);
    pkt[6] = (uint8_t)(instance->generic.serial);
    pkt[7] = (uint8_t)(instance->generic.cnt & 0xFF);
    pkt[8] = button;
    pkt[9] = sf_crc8(pkt, 9);

    LevelDuration* up = instance->encoder.upload;
    const size_t cap = SF_UPLOAD_CAPACITY;
    size_t index = 0;

    index = ppa_emit(up, index, cap, true, SF_TE_LONG);
    index = ppa_emit(up, index, cap, false, SF_SYNC_US);

    for(int byte = 0; byte < SF_BYTES; byte++) {
        for(int bit = 7; bit >= 0; bit--) {
            if((pkt[byte] >> bit) & 1U) {
                index = ppa_emit(up, index, cap, true, SF_TE_LONG);
                index = ppa_emit(up, index, cap, false, SF_TE_SHORT);
            } else {
                index = ppa_emit(up, index, cap, true, SF_TE_SHORT);
                index = ppa_emit(up, index, cap, false, SF_TE_LONG);
            }
        }
    }
    index = ppa_emit(up, index, cap, false, SF_GAP_US);

    if(index > cap) return false;
    instance->encoder.front = 0;
    instance->encoder.size_upload = index;
    return true;
}

void* subghz_protocol_encoder_santa_fe_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderSantaFe* instance = calloc(1, sizeof(SubGhzProtocolEncoderSantaFe));
    furi_check(instance);
    instance->base.protocol = &santa_fe_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 3;
    ppa_encoder_buffer_ensure(instance, SF_UPLOAD_CAPACITY);
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_santa_fe_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderSantaFe* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;

    if(ppa_verify_protocol_name(flipper_format, instance->base.protocol->name) !=
       SubGhzProtocolStatusOk) {
        return SubGhzProtocolStatusError;
    }
    SubGhzProtocolStatus load_st = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, SF_BIT_COUNT);
    if(load_st != SubGhzProtocolStatusOk) {
        return load_st;
    }

    uint32_t serial = (uint32_t)((instance->generic.data >> 8) & 0xFFFFFF);
    uint32_t btn = instance->generic.btn;
    uint32_t cnt = (uint32_t)(instance->generic.data & 0xFF);
    instance->rolling = (uint32_t)(instance->generic.data >> 32);
    uint32_t rolling = instance->rolling;
    if(flipper_format_read_uint32(flipper_format, "Rolling", &rolling, 1)) instance->rolling = rolling;
    ppa_encoder_read_fields(flipper_format, &serial, &btn, &cnt, NULL);

    instance->generic.serial = serial;
    instance->generic.btn = (uint8_t)btn;
    instance->generic.cnt = (uint16_t)(cnt & 0xFF);

    instance->encoder.repeat = ppa_encoder_read_repeat(flipper_format, 3);
    if(!sf_build_upload(instance, (uint8_t)btn)) {
        return SubGhzProtocolStatusError;
    }
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

static void enc_santa_fe_tr_free(void* context) {
    ppa_encoder_free(context);
}

static void enc_santa_fe_tr_stop(void* context) {
    ppa_encoder_stop(context);
}

static LevelDuration enc_santa_fe_tr_yield(void* context) {
    return ppa_encoder_yield(context);
}

static const SubGhzProtocolEncoder enc_santa_fe_vt = {
    .alloc = subghz_protocol_encoder_santa_fe_alloc,
    .free = enc_santa_fe_tr_free,
    .deserialize = subghz_protocol_encoder_santa_fe_deserialize,
    .stop = enc_santa_fe_tr_stop,
    .yield = enc_santa_fe_tr_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol santa_fe_protocol = {
    .name = SANTA_FE_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_santa_fe_vt,
};

static const SubGhzEncodePluginEntry enc_santa_fe_entry = {.name = SANTA_FE_PROTOCOL_NAME, .encoder = &enc_santa_fe_vt, .host_slot = &enc_santa_fe_host_raw};
static const FlipperAppPluginDescriptor enc_santa_fe_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_santa_fe_entry};
const FlipperAppPluginDescriptor* enc_santa_fe_ep(void) { return &enc_santa_fe_descriptor; }
