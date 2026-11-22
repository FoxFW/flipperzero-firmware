
#define subghz_protocol_bmw_v4 enc_bmw_v4__subghz_protocol_bmw_v4

#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/bmw_v4.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define BMW_TE_US             62
#define BMW_TE1_MIN_US        40
#define BMW_TE1_MAX_US        90
#define BMW_TE2_MIN_US        100
#define BMW_TE2_MAX_US        160
#define BMW_GAP_US            400
#define BMW_PREAMBLE_MIN      32
#define BMW_PREAMBLE_MAX      280
#define BMW_PREAMBLE_BITS     212
#define BMW_SYNC              0x599EU
#define BMW_SYNC_INV          0xA661U
#define BMW_HEADER_HUNT_MAX   40
#define BMW_PAYLOAD_BITS      128
#define BMW_FIXED_VALUE       0x89U
#define BMW_CNT_BITS          17
#define BMW_CNT_MASK          0x1FFFFU
#define BMW_HOP_MASK          0x7FFFFFFFFFFFULL
#define BMW_BTN_TEMPLATE      0x01U
#define BMW_BTN_TEMPLATE_MASK 0xE7U
#define BMW_BTN_XX_MASK       0x3U
#define BMW_BTN_UNLOCK        0x1U
#define BMW_BTN_LOCK          0x2U
#define BMW_BTN_TRUNK         0x3U
#define BMW_BTN_MAX           3
#define BMW_REPEAT_GAP_US     200000
#define BMW_REPEAT_DEFAULT    3
#define BMW_UPLOAD_CAPACITY   2048

struct SubGhzProtocolEncoderBmwV4 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static uint8_t bmw_v4_wire_xx(uint8_t btn_byte) {
    return (uint8_t)((btn_byte >> 3) & BMW_BTN_XX_MASK);
}

static uint8_t bmw_v4_btn_byte_from_wire_xx(uint8_t wire_xx) {
    return (uint8_t)(BMW_BTN_TEMPLATE | ((wire_xx & BMW_BTN_XX_MASK) << 3));
}

static uint32_t bmw_v4_wire_cnt(uint64_t lo) {
    return (uint32_t)((((lo >> 48) & 0xFFFFU) << 1) | ((lo >> 47) & 1U));
}

static void bmw_v4_normalize_stored_payload(SubGhzBlockGeneric* instance) {
    if((uint16_t)(instance->data >> 48) == BMW_SYNC_INV) {
        instance->data = ~instance->data;
        instance->data_2 = ~instance->data_2;
    }
}

static void bmw_v4_parse_fields(SubGhzBlockGeneric* instance) {
    const uint64_t hi = instance->data;
    const uint64_t lo = instance->data_2;
    const uint8_t btn_byte = (uint8_t)((hi >> 8) & 0xFFU);

    instance->serial = (uint32_t)(hi >> 16);
    instance->btn = bmw_v4_wire_xx(btn_byte);
    instance->cnt = bmw_v4_wire_cnt(lo) & BMW_CNT_MASK;
}

static void bmw_v4_rebuild_payload(SubGhzBlockGeneric* instance) {
    const uint8_t btn_byte = bmw_v4_btn_byte_from_wire_xx(instance->btn);
    const uint32_t wire_cnt = instance->cnt & BMW_CNT_MASK;
    const uint32_t wire_serial = instance->serial;
    const uint64_t bmw_v4_hop = instance->data_2 & BMW_HOP_MASK;

    instance->data = ((uint64_t)BMW_SYNC << 48) | ((uint64_t)wire_serial << 16) |
                     ((uint64_t)btn_byte << 8) | (uint64_t)BMW_FIXED_VALUE;
    instance->data_2 = ((uint64_t)(wire_cnt >> 1) << 48) | ((uint64_t)(wire_cnt & 1U) << 47) |
                       bmw_v4_hop;
    instance->data_count_bit = BMW_PAYLOAD_BITS;
}

static uint8_t bmw_v4_custom_id_from_code(uint8_t code) {
    switch(code) {
    case BMW_BTN_LOCK: return SUBGHZ_CUSTOM_BTN_UP;
    case BMW_BTN_UNLOCK: return SUBGHZ_CUSTOM_BTN_DOWN;
    case BMW_BTN_TRUNK: return SUBGHZ_CUSTOM_BTN_LEFT;
    default: return SUBGHZ_CUSTOM_BTN_OK;
    }
}

static uint8_t bmw_v4_get_btn_code(uint8_t original_code) {
    switch(subghz_custom_btn_get()) {
    case SUBGHZ_CUSTOM_BTN_UP: return BMW_BTN_LOCK;
    case SUBGHZ_CUSTOM_BTN_DOWN: return BMW_BTN_UNLOCK;
    case SUBGHZ_CUSTOM_BTN_LEFT: return BMW_BTN_TRUNK;
    default: return original_code;
    }
}

static size_t bmw_v4_emit_merge(LevelDuration* up, size_t i, size_t cap, bool level, uint32_t us) {
    if(i > 0 && level_duration_get_level(up[i - 1]) == level) {
        uint32_t prev = level_duration_get_duration(up[i - 1]);
        up[i - 1] = level_duration_make(level, prev + us);
        return i;
    }
    if(i < cap) up[i++] = level_duration_make(level, us);
    return i;
}

static size_t bmw_v4_emit_bit(LevelDuration* up, size_t i, size_t cap, bool bit_value) {
    if(bit_value) {
        i = bmw_v4_emit_merge(up, i, cap, false, BMW_TE_US);
        i = bmw_v4_emit_merge(up, i, cap, true, BMW_TE_US);
    } else {
        i = bmw_v4_emit_merge(up, i, cap, true, BMW_TE_US);
        i = bmw_v4_emit_merge(up, i, cap, false, BMW_TE_US);
    }
    return i;
}

static bool bmw_v4_get_upload(SubGhzProtocolEncoderBmwV4* instance) {
    LevelDuration* up = instance->encoder.upload;
    const size_t cap = BMW_UPLOAD_CAPACITY;
    size_t index = 0;

    bmw_v4_rebuild_payload(&instance->generic);
    if(subghz_block_generic_global_counter_override_get(&instance->generic.cnt)) {
        bmw_v4_rebuild_payload(&instance->generic);
    }
    uint8_t btn = instance->generic.btn;
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(bmw_v4_custom_id_from_code(btn));
    }
    subghz_custom_btn_set_max(BMW_BTN_MAX);
    btn = bmw_v4_get_btn_code(btn);
    subghz_block_generic_global_button_override_get(&btn);
    if(btn != instance->generic.btn) {
        instance->generic.btn = btn;
        bmw_v4_rebuild_payload(&instance->generic);
    }

    for(uint16_t n = 0; n < BMW_PREAMBLE_BITS; n++) {
        index = bmw_v4_emit_bit(up, index, cap, true);
    }
    for(int8_t bit = 63; bit >= 0; bit--) {
        index = bmw_v4_emit_bit(up, index, cap, (instance->generic.data >> bit) & 1);
    }
    for(int8_t bit = 63; bit >= 0; bit--) {
        index = bmw_v4_emit_bit(up, index, cap, (instance->generic.data_2 >> bit) & 1);
    }
    index = bmw_v4_emit_merge(up, index, cap, false, BMW_REPEAT_GAP_US);
    instance->encoder.size_upload = index;
    return index > 0 && index < cap;
}

static void* subghz_protocol_encoder_bmw_v4_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderBmwV4* instance = malloc(sizeof(SubGhzProtocolEncoderBmwV4));
    instance->base.protocol = &subghz_protocol_bmw_v4;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = BMW_REPEAT_DEFAULT;
    instance->encoder.size_upload = BMW_UPLOAD_CAPACITY;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

static void subghz_protocol_encoder_bmw_v4_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderBmwV4* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

static SubGhzProtocolStatus
    subghz_protocol_encoder_bmw_v4_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderBmwV4* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, BMW_PAYLOAD_BITS);
    if(ret != SubGhzProtocolStatusOk) return ret;
    instance->generic.data_2 = 0;
    {
        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(flipper_format_rewind(flipper_format) &&
           flipper_format_read_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
            for(uint8_t i = 0; i < sizeof(uint64_t); i++)
                instance->generic.data_2 = (instance->generic.data_2 << 8) | key_data[i];
        }
    }
    bmw_v4_normalize_stored_payload(&instance->generic);
    bmw_v4_parse_fields(&instance->generic);
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(bmw_v4_custom_id_from_code(instance->generic.btn));
    }
    subghz_custom_btn_set_max(BMW_BTN_MAX);
    flipper_format_read_uint32(flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);
    if(!bmw_v4_get_upload(instance)) return SubGhzProtocolStatusErrorEncoderGetUpload;
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

static void subghz_protocol_encoder_bmw_v4_stop(void* context) {
    SubGhzProtocolEncoderBmwV4* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

static LevelDuration subghz_protocol_encoder_bmw_v4_yield(void* context) {
    SubGhzProtocolEncoderBmwV4* instance = context;
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

static const SubGhzProtocolEncoder enc_bmw_v4_vt = {
    .alloc = subghz_protocol_encoder_bmw_v4_alloc,
    .free = subghz_protocol_encoder_bmw_v4_free,
    .deserialize = subghz_protocol_encoder_bmw_v4_deserialize,
    .stop = subghz_protocol_encoder_bmw_v4_stop,
    .yield = subghz_protocol_encoder_bmw_v4_yield,
};

const SubGhzProtocol subghz_protocol_bmw_v4 = {
    .name = SUBGHZ_PROTOCOL_BMW_V4_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_bmw_v4_vt,
};

static const SubGhzEncodePluginEntry enc_bmw_v4_entry = {.name = SUBGHZ_PROTOCOL_BMW_V4_NAME, .encoder = &enc_bmw_v4_vt};
static const FlipperAppPluginDescriptor enc_bmw_v4_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_bmw_v4_entry};
const FlipperAppPluginDescriptor* enc_bmw_v4_ep(void) { return &enc_bmw_v4_descriptor; }
