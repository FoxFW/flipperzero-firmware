/* enc_gm.fal : SD encoder for gm (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_gm__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_yield enc_gm__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_gm_alloc enc_gm__subghz_protocol_encoder_gm_alloc
#define subghz_protocol_encoder_gm_deserialize enc_gm__subghz_protocol_encoder_gm_deserialize
#define subghz_protocol_encoder_gm_stop enc_gm__subghz_protocol_encoder_gm_stop
#define subghz_protocol_gm enc_gm__subghz_protocol_gm
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/gm.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <furi.h>
#include <string.h>
#include <lib/subghz/protocols/common.h>

#define GM_TE_SHORT       300U
#define GM_TE_LONG        500U
#define GM_RESET_US       12000U
#define GM_WIRE_BYTES     14U
#define GM_WIRE_BITS      112U
#define GM_DEFAULT_REPEAT 5U
#define GM_DATA_FIELD     "Data"
#define GM_UPLOAD_CAPACITY (2U + (GM_WIRE_BITS * 2U) + 2U)

struct SubGhzProtocolEncoderGM {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_data[GM_WIRE_BYTES];
};

static uint32_t gm_add_nibbles(const uint8_t* b, size_t n);
static uint32_t gm_add_bytes(const uint8_t* b, size_t n);
static bool gm_frame_valid(const uint8_t raw[GM_WIRE_BYTES]);
static uint32_t gm_id(const uint8_t raw[GM_WIRE_BYTES]);
static uint32_t gm_sequence(const uint8_t raw[GM_WIRE_BYTES]);
static uint8_t gm_button(const uint8_t raw[GM_WIRE_BYTES]);
static bool gm_encoder_build_upload(SubGhzProtocolEncoderGM* instance);

static uint32_t gm_add_nibbles(const uint8_t* b, size_t n) {
    uint32_t sum = 0U;
    for(size_t i = 0U; i < n; i++) {
        sum += (uint32_t)(b[i] >> 4U) + (uint32_t)(b[i] & 0x0FU);
    }
    return sum;
}

static uint32_t gm_add_bytes(const uint8_t* b, size_t n) {
    uint32_t sum = 0U;
    for(size_t i = 0U; i < n; i++) {
        sum += b[i];
    }
    return sum;
}

static bool gm_frame_valid(const uint8_t raw[GM_WIRE_BYTES]) {

    if(raw[0] != 0xFFU) return false;

    const uint32_t btn_ck = gm_add_nibbles(&raw[2], 1U);
    if(btn_ck == 0U || (btn_ck & 0x0FU) != 0U) return false;

    const uint32_t full_ck = gm_add_bytes(&raw[1], 13U);
    if(full_ck == 0U || (full_ck & 0xFFU) != 0U) return false;

    return true;
}

static uint32_t gm_id(const uint8_t raw[GM_WIRE_BYTES]) {
    return ((uint32_t)raw[3] << 24U) | ((uint32_t)raw[4] << 16U) | ((uint32_t)raw[5] << 8U) |
           raw[6];
}

static uint32_t gm_sequence(const uint8_t raw[GM_WIRE_BYTES]) {
    return ((uint32_t)raw[7] << 16U) | ((uint32_t)raw[8] << 8U) | raw[9];
}

static uint8_t gm_button(const uint8_t raw[GM_WIRE_BYTES]) {
    return (uint8_t)(raw[2] & 0x07U);
}

static bool gm_encoder_build_upload(SubGhzProtocolEncoderGM* instance) {
    furi_check(instance);
    LevelDuration* upload = instance->encoder.upload;
    if(!upload) return false;

    size_t index = 0U;

    for(uint8_t bit_index = 0U; bit_index < GM_WIRE_BITS; bit_index++) {
        const bool bit =
            ((instance->raw_data[bit_index >> 3U] >> (7U - (bit_index & 7U))) & 1U) != 0U;

        upload[index++] = level_duration_make(true, GM_TE_SHORT);

        upload[index++] = level_duration_make(false, bit ? GM_TE_LONG : GM_TE_SHORT);
    }

    upload[index++] = level_duration_make(false, GM_RESET_US);

    instance->encoder.size_upload = index;
    instance->encoder.front = 0U;
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

void* subghz_protocol_encoder_gm_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderGM* instance = calloc(1, sizeof(SubGhzProtocolEncoderGM));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_gm;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = GM_DEFAULT_REPEAT;
    instance->encoder.size_upload = 0U;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
    return instance;
}

void subghz_protocol_encoder_gm_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderGM* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_gm_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    furi_check(flipper_format);
    SubGhzProtocolEncoderGM* instance = context;

    instance->encoder.is_running = false;
    instance->encoder.front = 0U;

    flipper_format_rewind(flipper_format);
    FuriString* protocol_name = furi_string_alloc();
    bool protocol_ok = flipper_format_read_string(flipper_format, "Protocol", protocol_name) &&
                       furi_string_equal(protocol_name, instance->base.protocol->name);
    furi_string_free(protocol_name);
    if(!protocol_ok) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint32_t bit_count = 0U;
    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_uint32(flipper_format, "Bit", &bit_count, 1) ||
       bit_count != GM_WIRE_BITS) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }
    instance->generic.data_count_bit = (uint16_t)bit_count;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, GM_DATA_FIELD, instance->raw_data, GM_WIRE_BYTES) ||
       !gm_frame_valid(instance->raw_data)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    instance->generic.serial = gm_id(instance->raw_data);
    instance->generic.btn = gm_button(instance->raw_data);
    instance->generic.cnt = (uint16_t)(gm_sequence(instance->raw_data) & 0xFFFFU);

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->raw_data[2] & 0x07U);
    }
    subghz_custom_btn_set_max(0);

    instance->generic.data =
        ((uint64_t)instance->generic.serial << 16U) | instance->generic.cnt;
    instance->generic.data_count_bit = GM_WIRE_BITS;

    uint32_t repeat = GM_DEFAULT_REPEAT;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
    instance->encoder.repeat = (repeat == 0U) ? GM_DEFAULT_REPEAT : (size_t)repeat;

    if(instance->encoder.upload == NULL) {
        instance->encoder.upload = malloc(GM_UPLOAD_CAPACITY * sizeof(LevelDuration));
        furi_check(instance->encoder.upload);
    }
    if(!gm_encoder_build_upload(instance)) {
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    }

    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

static const SubGhzProtocolEncoder enc_gm_vt = {
    .alloc = subghz_protocol_encoder_gm_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_gm_deserialize,
    .stop = subghz_protocol_encoder_gm_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_gm = {
    .name = GM_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_gm_vt,
};

static const SubGhzEncodePluginEntry enc_gm_entry = {.name = GM_PROTOCOL_NAME, .encoder = &enc_gm_vt};
static const FlipperAppPluginDescriptor enc_gm_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_gm_entry};
const FlipperAppPluginDescriptor* enc_gm_ep(void) { return &enc_gm_descriptor; }
