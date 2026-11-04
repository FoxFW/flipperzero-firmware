/* enc_land_rover_v0.fal : SD encoder for land_rover_v0 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_land_rover_v0__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_land_rover_v0__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_land_rover_v0_alloc enc_land_rover_v0__subghz_protocol_encoder_land_rover_v0_alloc
#define subghz_protocol_encoder_land_rover_v0_deserialize enc_land_rover_v0__subghz_protocol_encoder_land_rover_v0_deserialize
#define subghz_protocol_encoder_land_rover_v0_yield enc_land_rover_v0__subghz_protocol_encoder_land_rover_v0_yield
#define subghz_protocol_land_rover_v0 enc_land_rover_v0__subghz_protocol_land_rover_v0
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/land_rover_v0.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>
#include <lib/subghz/protocols/common.h>

#define LAND_ROVER_V0_PREAMBLE_PAIRS     319U
#define LAND_ROVER_V0_SYNC_US            750U
#define LAND_ROVER_V0_UPLOAD_CAPACITY    1024U
#define LAND_ROVER_V0_GAP_US             50000U
#define LAND_ROVER_V0_BTN_UNKNOWN 0x00U
#define LAND_ROVER_V0_BTN_LOCK    0x02U
#define LAND_ROVER_V0_BTN_UNLOCK  0x04U
#define LAND_ROVER_V0_SIG_UNLOCK 0xA285E3UL
#define LAND_ROVER_V0_SIG_LOCK   0xC20363UL
#define LAND_ROVER_V0_FF_BTNSIG    "BtnSig"
#define LAND_ROVER_V0_FF_CHECK     "Check"
#define LAND_ROVER_V0_FF_TAIL      "Tail"
#define LAND_ROVER_V0_FF_EXTRA_BIT "ExtraBit"
#define LR_FF_KEY    "Key"
#define LR_FF_SERIAL "Serial"
#define LR_FF_BTN    "Btn"
#define LR_FF_CNT    "Cnt"

typedef struct SubGhzProtocolEncoderLandRoverV0 {
    SubGhzProtocolEncoderBase  base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric         generic;

    uint64_t key;
    uint16_t tail;
    uint32_t command_signature;
    uint32_t serial;
    uint32_t count;
    uint8_t  button;
    uint8_t  check;
} 
SubGhzProtocolEncoderLandRoverV0;

static const SubGhzBlockConst subghz_protocol_land_rover_v0_const = {
    .te_short                = 250,
    .te_long                 = 500,
    .te_delta                = 100,
    .min_count_bit_for_found = 81,
};

static inline void lr_u64_to_bytes_be(uint64_t val, uint8_t out[8]);
static inline uint64_t lr_bytes_to_u64_be(const uint8_t in[8]);
static void lr_ff_write_u32(FlipperFormat* ff, const char* key, uint32_t val);
static bool lr_ff_read_u32(FlipperFormat* ff, const char* key, uint32_t* out);
static SubGhzProtocolStatus lr_verify_protocol_name(
    FlipperFormat* ff,
    const char*    expected_name);
static uint16_t lr_encoder_read_repeat(FlipperFormat* ff, uint16_t default_val);
static uint8_t land_rover_v0_button_from_signature(uint32_t signature);
static uint8_t land_rover_v0_calculate_check(uint32_t count);
static bool land_rover_v0_calculate_tail_msb(uint32_t count);
static uint16_t land_rover_v0_calculate_tail(uint32_t count);
static void land_rover_v0_parse_key_fields(
    uint64_t  key,
    uint32_t* signature,
    uint32_t* serial,
    uint32_t* count,
    uint8_t*  button,
    uint8_t*  check);
static bool land_rover_v0_encoder_add_level(
    SubGhzProtocolEncoderLandRoverV0* instance,
    size_t*                           index,
    bool                              level,
    uint32_t                          duration);
static bool land_rover_v0_encoder_add_bit(
    SubGhzProtocolEncoderLandRoverV0* instance,
    size_t*                           index,
    bool*                             previous_bit,
    bool                              bit);
static bool land_rover_v0_build_upload(SubGhzProtocolEncoderLandRoverV0* instance);
static uint32_t land_rover_v0_signature_from_button(uint8_t button);
static uint64_t land_rover_v0_build_key(
    uint32_t signature,
    uint32_t serial,
    uint32_t count);

static inline void lr_u64_to_bytes_be(uint64_t val, uint8_t out[8]) {
    for(int i = 7; i >= 0; i--) {
        out[i] = (uint8_t)(val & 0xFFU);
        val >>= 8;
    }
}

static inline uint64_t lr_bytes_to_u64_be(const uint8_t in[8]) {
    uint64_t val = 0;
    for(int i = 0; i < 8; i++) {
        val = (val << 8) | in[i];
    }
    return val;
}

static void lr_ff_write_u32(FlipperFormat* ff, const char* key, uint32_t val) {
    flipper_format_insert_or_update_uint32(ff, key, &val, 1);
}

static bool lr_ff_read_u32(FlipperFormat* ff, const char* key, uint32_t* out) {
    return flipper_format_read_uint32(ff, key, out, 1);
}

static SubGhzProtocolStatus lr_verify_protocol_name(
    FlipperFormat* ff,
    const char*    expected_name) {
    FuriString* name = furi_string_alloc();
    bool        ok   = false;
    if(flipper_format_read_string(ff, "Protocol", name)) {
        ok = furi_string_equal_str(name, expected_name);
    }
    furi_string_free(name);
    return ok ? SubGhzProtocolStatusOk : SubGhzProtocolStatusErrorProtocolNotFound;
}

static uint16_t lr_encoder_read_repeat(FlipperFormat* ff, uint16_t default_val) {
    uint32_t repeat = default_val;
    flipper_format_rewind(ff);
    if(!flipper_format_read_uint32(ff, "Repeat", &repeat, 1)) {
        repeat = default_val;
    }
    return (uint16_t)repeat;
}

static uint8_t land_rover_v0_button_from_signature(uint32_t signature) {
    if(signature == LAND_ROVER_V0_SIG_UNLOCK) return LAND_ROVER_V0_BTN_UNLOCK;
    if(signature == LAND_ROVER_V0_SIG_LOCK)   return LAND_ROVER_V0_BTN_LOCK;
    return LAND_ROVER_V0_BTN_UNKNOWN;
}

static uint8_t land_rover_v0_calculate_check(uint32_t count) {
    const uint8_t c0 =
        ((count >> 1) ^ (count >> 2) ^ (count >> 3) ^ (count >> 4) ^ (count >> 6)) & 1U;
    const uint8_t c1 =
        ((count >> 0) ^ (count >> 2) ^ (count >> 3) ^ (count >> 4) ^ (count >> 5) ^
         (count >> 6) ^ 1U) & 1U;
    const uint8_t c2 =
        ((count >> 1) ^ (count >> 3) ^ (count >> 4) ^ (count >> 5) ^ (count >> 6)) & 1U;
    return (uint8_t)(c0 | (c1 << 1) | (c2 << 2));
}

static bool land_rover_v0_calculate_tail_msb(uint32_t count) {
    const uint8_t tail =
        ((count >> 0) ^ (count >> 2) ^ (count >> 4) ^ (count >> 5)) & 1U;
    return tail != 0U;
}

static uint16_t land_rover_v0_calculate_tail(uint32_t count) {
    return land_rover_v0_calculate_tail_msb(count) ? 0xFFFFU : 0x7FFFU;
}

static void land_rover_v0_parse_key_fields(
    uint64_t  key,
    uint32_t* signature,
    uint32_t* serial,
    uint32_t* count,
    uint8_t*  button,
    uint8_t*  check) {

    uint8_t key_bytes[8];
    lr_u64_to_bytes_be(key, key_bytes);

    const uint32_t sig =
        ((uint32_t)key_bytes[0] << 16) | ((uint32_t)key_bytes[1] << 8) | key_bytes[2];
    const uint32_t sn =
        ((uint32_t)key_bytes[3] << 16) | ((uint32_t)key_bytes[4] << 8) | key_bytes[5];
    const uint32_t cnt =
        ((uint32_t)key_bytes[6] << 1) | ((key_bytes[7] >> 7) & 1U);

    if(signature) *signature = sig;
    if(serial)    *serial    = sn;
    if(count)     *count     = cnt;
    if(button)    *button    = land_rover_v0_button_from_signature(sig);
    if(check)     *check     = key_bytes[7] & 0x07U;
}

static bool land_rover_v0_encoder_add_level(
    SubGhzProtocolEncoderLandRoverV0* instance,
    size_t*                           index,
    bool                              level,
    uint32_t                          duration) {

    if(*index >= LAND_ROVER_V0_UPLOAD_CAPACITY) return false;
    instance->encoder.upload[(*index)++] = level_duration_make(level, duration);
    return true;
}

static bool land_rover_v0_encoder_add_bit(
    SubGhzProtocolEncoderLandRoverV0* instance,
    size_t*                           index,
    bool*                             previous_bit,
    bool                              bit) {

    const uint32_t te_short = subghz_protocol_land_rover_v0_const.te_short;
    const uint32_t te_long  = subghz_protocol_land_rover_v0_const.te_long;

    if(!*previous_bit && !bit) {

        if(!land_rover_v0_encoder_add_level(instance, index, true,  te_short) ||
           !land_rover_v0_encoder_add_level(instance, index, false, te_short))
            return false;
    } else if(!*previous_bit && bit) {

        if(!land_rover_v0_encoder_add_level(instance, index, true, te_long))
            return false;
    } else if(*previous_bit && !bit) {

        if(!land_rover_v0_encoder_add_level(instance, index, false, te_long))
            return false;
    } else {

        if(!land_rover_v0_encoder_add_level(instance, index, false, te_short) ||
           !land_rover_v0_encoder_add_level(instance, index, true,  te_short))
            return false;
    }

    *previous_bit = bit;
    return true;
}

static bool land_rover_v0_build_upload(SubGhzProtocolEncoderLandRoverV0* instance) {
    furi_check(instance);

    size_t         index    = 0;
    const uint32_t te_short = subghz_protocol_land_rover_v0_const.te_short;

    uint8_t key_bytes[8];
    lr_u64_to_bytes_be(instance->key, key_bytes);

    for(uint16_t i = 0; i < LAND_ROVER_V0_PREAMBLE_PAIRS; i++) {
        if(!land_rover_v0_encoder_add_level(instance, &index, true,  te_short) ||
           !land_rover_v0_encoder_add_level(instance, &index, false, te_short))
            return false;
    }

    if(!land_rover_v0_encoder_add_level(instance, &index, true,  LAND_ROVER_V0_SYNC_US) ||
       !land_rover_v0_encoder_add_level(instance, &index, false, LAND_ROVER_V0_SYNC_US) ||
       !land_rover_v0_encoder_add_level(instance, &index, true,  te_short))
        return false;

    bool previous_bit = true;
    if(!land_rover_v0_encoder_add_bit(instance, &index, &previous_bit, false))
        return false;

    for(uint8_t bit_index = 2; bit_index < 64; bit_index++) {
        const uint8_t byte_index  = bit_index / 8U;
        const uint8_t bit_in_byte = 7U - (bit_index % 8U);
        const bool    bit         = (key_bytes[byte_index] >> bit_in_byte) & 1U;
        if(!land_rover_v0_encoder_add_bit(instance, &index, &previous_bit, bit))
            return false;
    }

    instance->tail = land_rover_v0_calculate_tail(instance->count);
    for(uint8_t bit_index = 0; bit_index < 16; bit_index++) {
        const bool bit = (instance->tail >> (15U - bit_index)) & 1U;
        if(!land_rover_v0_encoder_add_bit(instance, &index, &previous_bit, bit))
            return false;
    }

    if(!land_rover_v0_encoder_add_bit(instance, &index, &previous_bit, true))
        return false;

    if(!land_rover_v0_encoder_add_level(instance, &index, false, LAND_ROVER_V0_GAP_US))
        return false;

    instance->encoder.front       = 0;
    instance->encoder.size_upload = index;
    return true;
}

static uint32_t land_rover_v0_signature_from_button(uint8_t button) {
    switch(button) {
    case LAND_ROVER_V0_BTN_LOCK:   return LAND_ROVER_V0_SIG_LOCK;
    case LAND_ROVER_V0_BTN_UNLOCK: return LAND_ROVER_V0_SIG_UNLOCK;
    default:                        return 0;
    }
}

static uint64_t land_rover_v0_build_key(
    uint32_t signature,
    uint32_t serial,
    uint32_t count) {

    uint8_t key_bytes[8] = {0};
    key_bytes[0] = (uint8_t)((signature >> 16) & 0xFFU);
    key_bytes[1] = (uint8_t)((signature >>  8) & 0xFFU);
    key_bytes[2] = (uint8_t)( signature        & 0xFFU);
    key_bytes[3] = (uint8_t)((serial    >> 16) & 0xFFU);
    key_bytes[4] = (uint8_t)((serial    >>  8) & 0xFFU);
    key_bytes[5] = (uint8_t)( serial           & 0xFFU);
    key_bytes[6] = (uint8_t)((count     >>  1) & 0xFFU);

    const bool    counter_lsb = (count & 1U) != 0;
    const uint8_t check       = land_rover_v0_calculate_check(count);
    key_bytes[7]              = (counter_lsb ? 0x80U : 0x00U) | check;

    return lr_bytes_to_u64_be(key_bytes);
}

/* vendored from lib/subghz/protocols/common.c (not exported) */
void subghz_protocol_encoder_common_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderCommon* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

/* vendored from lib/subghz/protocols/common.c (not exported) */
void subghz_protocol_encoder_common_stop(void* context) {
    SubGhzProtocolEncoderCommon* instance = context;
    instance->encoder.is_running = false;
}

void* subghz_protocol_encoder_land_rover_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderLandRoverV0* instance =
        calloc(1, sizeof(SubGhzProtocolEncoderLandRoverV0));
    furi_check(instance);

    instance->base.protocol         = &subghz_protocol_land_rover_v0;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.upload =
        malloc(LAND_ROVER_V0_UPLOAD_CAPACITY * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);

    instance->encoder.repeat      = 10;
    instance->encoder.size_upload = 0;
    instance->encoder.front       = 0;
    instance->encoder.is_running  = false;

    return instance;
}

SubGhzProtocolStatus subghz_protocol_encoder_land_rover_v0_deserialize(
    void*          context,
    FlipperFormat* flipper_format) {

    furi_check(context);
    SubGhzProtocolEncoderLandRoverV0* instance = context;
    SubGhzProtocolStatus              ret      = SubGhzProtocolStatusError;

    instance->encoder.is_running = false;
    instance->encoder.front      = 0;
    instance->encoder.repeat     = 10;

    do {
        if(lr_verify_protocol_name(flipper_format, instance->base.protocol->name) !=
           SubGhzProtocolStatusOk)
            break;

        SubGhzProtocolStatus load_status =
            subghz_block_generic_deserialize_check_count_bit(
                &instance->generic,
                flipper_format,
                subghz_protocol_land_rover_v0_const.min_count_bit_for_found);
        if(load_status != SubGhzProtocolStatusOk) break;

        instance->serial = instance->generic.serial & 0xFFFFFFU;
        instance->button = instance->generic.btn;
        instance->count  = instance->generic.cnt    & 0x1FFU;

        uint8_t key_bytes[8] = {0};
        bool    have_key     = false;

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_hex(
               flipper_format, LR_FF_KEY, key_bytes, sizeof(key_bytes))) {
            instance->key = lr_bytes_to_u64_be(key_bytes);
            have_key      = true;
        }

        if(have_key) {
            land_rover_v0_parse_key_fields(
                instance->key,
                &instance->command_signature,
                &instance->serial,
                &instance->count,
                &instance->button,
                &instance->check);
        }

        uint32_t u32         = 0;
        bool     have_button = false;

        flipper_format_rewind(flipper_format);
        if(lr_ff_read_u32(flipper_format, LR_FF_SERIAL, &u32))
            instance->serial = u32 & 0xFFFFFFU;

        flipper_format_rewind(flipper_format);
        if(lr_ff_read_u32(flipper_format, LR_FF_CNT, &u32))
            instance->count = u32 & 0x1FFU;

        flipper_format_rewind(flipper_format);
        if(lr_ff_read_u32(flipper_format, LR_FF_BTN, &u32)) {
            instance->button = (uint8_t)u32;
            have_button      = true;
        }

        flipper_format_rewind(flipper_format);
        if(lr_ff_read_u32(flipper_format, LAND_ROVER_V0_FF_BTNSIG, &u32))
            instance->command_signature = u32 & 0xFFFFFFU;

        {
            const uint8_t original_btn = instance->button;
            if(subghz_custom_btn_get_original() == 0) {
                subghz_custom_btn_set_original(original_btn);
            }
            subghz_custom_btn_set_max(2);
            uint8_t custom_btn_id = subghz_custom_btn_get();
            uint8_t new_btn = original_btn;
            switch(custom_btn_id) {
            case SUBGHZ_CUSTOM_BTN_UP: new_btn = LAND_ROVER_V0_BTN_LOCK;   break;
            case SUBGHZ_CUSTOM_BTN_OK: new_btn = LAND_ROVER_V0_BTN_UNLOCK; break;
            default: break;
            }
            if(new_btn != original_btn && new_btn != 0U) {
                instance->button = new_btn;
                have_button = true;
            }
        }

        if(have_button) {
            const uint32_t sig = land_rover_v0_signature_from_button(instance->button);
            if(sig != 0U) instance->command_signature = sig;
        }

        if(instance->command_signature == 0U) break;

        instance->key = land_rover_v0_build_key(
            instance->command_signature, instance->serial, instance->count);

        lr_u64_to_bytes_be(instance->key, key_bytes);
        instance->tail = land_rover_v0_calculate_tail(instance->count);

        land_rover_v0_parse_key_fields(
            instance->key,
            &instance->command_signature,
            &instance->serial,
            &instance->count,
            &instance->button,
            &instance->check);

        instance->generic.data           = instance->key;
        instance->generic.data_count_bit =
            subghz_protocol_land_rover_v0_const.min_count_bit_for_found;
        instance->generic.serial = instance->serial;
        instance->generic.btn    = instance->button;
        instance->generic.cnt    = instance->count;

        flipper_format_rewind(flipper_format);
        instance->encoder.repeat = lr_encoder_read_repeat(flipper_format, 10);

        if(!land_rover_v0_build_upload(instance) ||
           instance->encoder.size_upload == 0U)
            break;

        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_hex(
            flipper_format, LR_FF_KEY, key_bytes, sizeof(key_bytes));
        lr_ff_write_u32(flipper_format, LR_FF_SERIAL,             instance->serial);
        lr_ff_write_u32(flipper_format, LR_FF_BTN,                instance->button);
        lr_ff_write_u32(flipper_format, LAND_ROVER_V0_FF_BTNSIG,  instance->command_signature);
        lr_ff_write_u32(flipper_format, LR_FF_CNT,                instance->count);
        lr_ff_write_u32(flipper_format, LAND_ROVER_V0_FF_CHECK,   instance->check);
        lr_ff_write_u32(flipper_format, LAND_ROVER_V0_FF_TAIL,    instance->tail);
        lr_ff_write_u32(flipper_format, LAND_ROVER_V0_FF_EXTRA_BIT, 1U);

        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

LevelDuration subghz_protocol_encoder_land_rover_v0_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderLandRoverV0* instance = context;

    if(instance->encoder.front >= instance->encoder.size_upload) {

        if(instance->encoder.repeat > 0 && !subghz_block_generic_global.endless_tx) {
            instance->encoder.repeat--;
        }
        instance->encoder.front = 0;

        if(instance->encoder.repeat == 0) {
            instance->encoder.is_running = false;
            return level_duration_reset();
        }
    }

    return instance->encoder.upload[instance->encoder.front++];
}

static const SubGhzProtocolEncoder enc_land_rover_v0_vt = {
    .alloc = subghz_protocol_encoder_land_rover_v0_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_land_rover_v0_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_land_rover_v0_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_land_rover_v0 = {
    .name = LAND_ROVER_PROTOCOL_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
               SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
               SubGhzProtocolFlag_Save      | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_land_rover_v0_vt,
};

static const SubGhzEncodePluginEntry enc_land_rover_v0_entry = {.name = LAND_ROVER_PROTOCOL_V0_NAME, .encoder = &enc_land_rover_v0_vt};
static const FlipperAppPluginDescriptor enc_land_rover_v0_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_land_rover_v0_entry};
const FlipperAppPluginDescriptor* enc_land_rover_v0_ep(void) { return &enc_land_rover_v0_descriptor; }
