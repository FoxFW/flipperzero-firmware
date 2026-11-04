/* enc_hyundai_kia_rio.fal : SD encoder for hyundai_kia_rio (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define hyundai_kia_rio_protocol enc_hyundai_kia_rio__hyundai_kia_rio_protocol
#define subghz_protocol_encoder_hyundai_kia_rio_alloc enc_hyundai_kia_rio__subghz_protocol_encoder_hyundai_kia_rio_alloc
#define subghz_protocol_encoder_hyundai_kia_rio_deserialize enc_hyundai_kia_rio__subghz_protocol_encoder_hyundai_kia_rio_deserialize
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/hyundai_kia_rio.h>
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

#define FF_KEY                    "Key"
#define HKR_BIT_COUNT 64
#define HKR_TE_SHORT  312
#define HKR_TE_LONG   728
#define HKR_SYNC_US   10400
#define HKR_GAP_US    10000
#define HKR_UPLOAD_CAPACITY ((2U) + (2U * HKR_BIT_COUNT) + 2U)

struct SubGhzProtocolEncoderHyundaiKiaRio {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
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
    __typeof__(&ppa_u64_to_bytes_be) ppa_u64_to_bytes_be;
    __typeof__(&ppa_verify_protocol_name) ppa_verify_protocol_name;
} enc_hyundai_kia_rio_host_t;
static const void* enc_hyundai_kia_rio_host_raw;
#define ENC_HYUNDAI_KIA_RIO_HOST ((const enc_hyundai_kia_rio_host_t*)enc_hyundai_kia_rio_host_raw)
#define ppa_emit (ENC_HYUNDAI_KIA_RIO_HOST->ppa_emit)
#define ppa_encoder_buffer_ensure (ENC_HYUNDAI_KIA_RIO_HOST->ppa_encoder_buffer_ensure)
#define ppa_encoder_free (ENC_HYUNDAI_KIA_RIO_HOST->ppa_encoder_free)
#define ppa_encoder_read_fields (ENC_HYUNDAI_KIA_RIO_HOST->ppa_encoder_read_fields)
#define ppa_encoder_read_repeat (ENC_HYUNDAI_KIA_RIO_HOST->ppa_encoder_read_repeat)
#define ppa_encoder_stop (ENC_HYUNDAI_KIA_RIO_HOST->ppa_encoder_stop)
#define ppa_encoder_yield (ENC_HYUNDAI_KIA_RIO_HOST->ppa_encoder_yield)
#define ppa_u64_to_bytes_be (ENC_HYUNDAI_KIA_RIO_HOST->ppa_u64_to_bytes_be)
#define ppa_verify_protocol_name (ENC_HYUNDAI_KIA_RIO_HOST->ppa_verify_protocol_name)

static uint16_t hkr_button_to_mask(uint8_t button);
static uint8_t hkr_mask_to_button(uint16_t mask);
static uint16_t hkr_checksum(uint32_t serial, uint16_t mask);
static bool hkr_build_upload(SubGhzProtocolEncoderHyundaiKiaRio* instance);

static uint16_t hkr_button_to_mask(uint8_t button) {
    switch(button) {
    case 1:
        return 0x0100;
    case 2:
        return 0x0200;
    case 4:
        return 0x0400;
    case 8:
        return 0x0800;
    default:
        return (uint16_t)((uint16_t)button << 8);
    }
}

static uint8_t hkr_mask_to_button(uint16_t mask) {
    switch(mask) {
    case 0x0100:
        return 1;
    case 0x0200:
        return 2;
    case 0x0400:
        return 4;
    case 0x0800:
        return 8;
    default:
        return (uint8_t)(mask >> 8);
    }
}

static uint16_t hkr_checksum(uint32_t serial, uint16_t mask) {
    uint16_t c = (uint16_t)(serial ^ (serial >> 16));
    c ^= mask;
    return (uint16_t)(~c);
}

static bool hkr_build_upload(SubGhzProtocolEncoderHyundaiKiaRio* instance) {
    LevelDuration* up = instance->encoder.upload;
    const size_t cap = HKR_UPLOAD_CAPACITY;
    size_t index = 0;

    const uint64_t word = instance->generic.data;

    index = ppa_emit(up, index, cap, true, HKR_TE_SHORT);
    index = ppa_emit(up, index, cap, false, HKR_SYNC_US);

    for(int8_t b = HKR_BIT_COUNT - 1; b >= 0; b--) {
        if((word >> b) & 1U) {
            index = ppa_emit(up, index, cap, true, HKR_TE_LONG);
            index = ppa_emit(up, index, cap, false, HKR_TE_SHORT);
        } else {
            index = ppa_emit(up, index, cap, true, HKR_TE_SHORT);
            index = ppa_emit(up, index, cap, false, HKR_TE_LONG);
        }
    }

    index = ppa_emit(up, index, cap, false, HKR_GAP_US);

    if(index > cap) {
        return false;
    }
    instance->encoder.front = 0;
    instance->encoder.size_upload = index;
    return true;
}

void* subghz_protocol_encoder_hyundai_kia_rio_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderHyundaiKiaRio* instance =
        calloc(1, sizeof(SubGhzProtocolEncoderHyundaiKiaRio));
    furi_check(instance);
    instance->base.protocol = &hyundai_kia_rio_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 3;
    ppa_encoder_buffer_ensure(instance, HKR_UPLOAD_CAPACITY);
    return instance;
}

SubGhzProtocolStatus subghz_protocol_encoder_hyundai_kia_rio_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderHyundaiKiaRio* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;

    if(ppa_verify_protocol_name(flipper_format, instance->base.protocol->name) !=
       SubGhzProtocolStatusOk) {
        return SubGhzProtocolStatusError;
    }

    SubGhzProtocolStatus load_st = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, HKR_BIT_COUNT);
    if(load_st != SubGhzProtocolStatusOk) {
        return load_st;
    }

    uint32_t serial = (uint32_t)(instance->generic.data >> 32);
    uint32_t btn = hkr_mask_to_button((uint16_t)((instance->generic.data >> 16) & 0xFFFF));
    uint32_t cnt = 0;
    ppa_encoder_read_fields(flipper_format, &serial, &btn, &cnt, NULL);

    uint16_t mask = hkr_button_to_mask((uint8_t)btn);
    uint16_t ck = hkr_checksum(serial, mask);
    instance->generic.data =
        ((uint64_t)serial << 32) | ((uint64_t)mask << 16) | (uint64_t)ck;
    instance->generic.data_count_bit = HKR_BIT_COUNT;
    instance->generic.serial = serial;
    instance->generic.btn = (uint8_t)btn;

    flipper_format_rewind(flipper_format);
    uint8_t key_data[8];
    ppa_u64_to_bytes_be(instance->generic.data, key_data);
    if(!flipper_format_update_hex(flipper_format, FF_KEY, key_data, sizeof(key_data))) {
        return SubGhzProtocolStatusErrorParserKey;
    }

    instance->encoder.repeat = ppa_encoder_read_repeat(flipper_format, 3);
    if(!hkr_build_upload(instance)) {
        return SubGhzProtocolStatusError;
    }
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

static void enc_hyundai_kia_rio_tr_free(void* context) {
    ppa_encoder_free(context);
}

static void enc_hyundai_kia_rio_tr_stop(void* context) {
    ppa_encoder_stop(context);
}

static LevelDuration enc_hyundai_kia_rio_tr_yield(void* context) {
    return ppa_encoder_yield(context);
}

static const SubGhzProtocolEncoder enc_hyundai_kia_rio_vt = {
    .alloc = subghz_protocol_encoder_hyundai_kia_rio_alloc,
    .free = enc_hyundai_kia_rio_tr_free,
    .deserialize = subghz_protocol_encoder_hyundai_kia_rio_deserialize,
    .stop = enc_hyundai_kia_rio_tr_stop,
    .yield = enc_hyundai_kia_rio_tr_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol hyundai_kia_rio_protocol = {
    .name = HYUNDAI_KIA_RIO_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_hyundai_kia_rio_vt,
};

static const SubGhzEncodePluginEntry enc_hyundai_kia_rio_entry = {.name = HYUNDAI_KIA_RIO_PROTOCOL_NAME, .encoder = &enc_hyundai_kia_rio_vt, .host_slot = &enc_hyundai_kia_rio_host_raw};
static const FlipperAppPluginDescriptor enc_hyundai_kia_rio_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_hyundai_kia_rio_entry};
const FlipperAppPluginDescriptor* enc_hyundai_kia_rio_ep(void) { return &enc_hyundai_kia_rio_descriptor; }
