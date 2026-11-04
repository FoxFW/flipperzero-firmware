/* enc_mazda_v0.fal : SD encoder for mazda_v0 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_mazda_v0__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_mazda_v0__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_mazda_v0__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_mazda_v0_alloc enc_mazda_v0__subghz_protocol_encoder_mazda_v0_alloc
#define subghz_protocol_encoder_mazda_v0_deserialize enc_mazda_v0__subghz_protocol_encoder_mazda_v0_deserialize
#define subghz_protocol_mazda_v0 enc_mazda_v0__subghz_protocol_mazda_v0
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/mazda_v0.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>
#include <lib/subghz/protocols/common.h>

#define MAZDA_V0_UPLOAD_CAPACITY 0x184
#define MAZDA_V0_GAP_US          0xCB20
#define MAZDA_V0_SYNC_BYTE       0xD7
#define MAZDA_V0_TAIL_BYTE       0x5A

typedef struct SubGhzProtocolEncoderMazdaV0 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t serial;
    uint8_t button;
    uint32_t count;
} 
SubGhzProtocolEncoderMazdaV0;

static const SubGhzBlockConst subghz_protocol_mazda_v0_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

static uint8_t mazda_v0_popcount8(uint8_t x);
static void mazda_v0_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]);
static uint64_t mazda_v0_bytes_to_u64_be(const uint8_t bytes[8]);
static uint8_t mazda_v0_calculate_checksum(uint32_t serial, uint8_t button, uint32_t counter);
static void mazda_v0_decode_key(SubGhzBlockGeneric* generic);
static uint64_t mazda_v0_encode_key(uint32_t serial, uint8_t button, uint32_t counter);
static bool mazda_v0_encoder_add_level(
    SubGhzProtocolEncoderMazdaV0* instance,
    size_t* index,
    bool level,
    uint32_t duration);
static bool
    mazda_v0_append_byte(SubGhzProtocolEncoderMazdaV0* instance, size_t* index, uint8_t value);
static bool mazda_v0_build_upload(SubGhzProtocolEncoderMazdaV0* instance);

static uint8_t mazda_v0_popcount8(uint8_t x) {
    uint8_t count = 0;
    while(x) {
        count += x & 1;
        x >>= 1;
    }
    return count;
}

static void mazda_v0_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]) {
    for(size_t i = 0; i < 8; i++) {
        bytes[i] = (uint8_t)((data >> ((7 - i) * 8)) & 0xFF);
    }
}

static uint64_t mazda_v0_bytes_to_u64_be(const uint8_t bytes[8]) {
    uint64_t data = 0;
    for(size_t i = 0; i < 8; i++) {
        data = (data << 8) | bytes[i];
    }
    return data;
}

static uint8_t mazda_v0_calculate_checksum(uint32_t serial, uint8_t button, uint32_t counter) {
    counter &= 0xFFFFFU;
    return (uint8_t)(((serial >> 24) & 0xFF) + ((serial >> 16) & 0xFF) + ((serial >> 8) & 0xFF) +
                     (serial & 0xFF) + ((counter >> 8) & 0xFF) + (counter & 0xFF) +
                     ((((counter >> 16) & 0x0F) | ((button & 0x0F) << 4)) & 0xFF));
}

static void mazda_v0_decode_key(SubGhzBlockGeneric* generic) {
    uint8_t data[8];
    mazda_v0_u64_to_bytes_be(generic->data, data);

    const bool parity = (mazda_v0_popcount8(data[7]) & 1) != 0;
    const uint8_t limit = parity ? 6 : 5;
    const uint8_t mask = data[limit];

    for(uint8_t i = 0; i < limit; i++) {
        data[i] ^= mask;
    }

    if(!parity) {
        data[6] ^= mask;
    }

    const uint8_t counter_lo = (data[5] & 0x55) | (data[6] & 0xAA);
    const uint8_t counter_mid = (data[6] & 0x55) | (data[5] & 0xAA);

    generic->serial = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                      ((uint32_t)data[2] << 8) | (uint32_t)data[3];
    generic->btn = (data[4] >> 4) & 0x0F;
    generic->cnt = (((uint32_t)data[4] & 0x0F) << 16) | ((uint32_t)counter_mid << 8) |
                   (uint32_t)counter_lo;
    generic->data_count_bit = subghz_protocol_mazda_v0_const.min_count_bit_for_found;
}

static uint64_t mazda_v0_encode_key(uint32_t serial, uint8_t button, uint32_t counter) {
    uint8_t data[8];

    counter &= 0xFFFFFU;
    button &= 0x0F;

    data[0] = (serial >> 24) & 0xFF;
    data[1] = (serial >> 16) & 0xFF;
    data[2] = (serial >> 8) & 0xFF;
    data[3] = serial & 0xFF;
    data[4] = (button << 4) | ((counter >> 16) & 0x0F);
    data[5] = (counter >> 8) & 0xFF;
    data[6] = counter & 0xFF;
    data[7] = mazda_v0_calculate_checksum(serial, button, counter);

    const uint8_t stored_5 = (data[6] & 0x55) | (data[5] & 0xAA);
    const uint8_t stored_6 = (data[6] & 0xAA) | (data[5] & 0x55);
    const uint8_t xor_mask = stored_5 ^ stored_6;
    const bool replace_second = ((~mazda_v0_popcount8(data[7])) & 1) != 0;
    const uint8_t forward_mask = replace_second ? stored_5 : stored_6;

    data[5] = replace_second ? stored_5 : xor_mask;
    data[6] = replace_second ? xor_mask : stored_6;

    for(size_t i = 0; i < 5; i++) {
        data[i] ^= forward_mask;
    }

    return mazda_v0_bytes_to_u64_be(data);
}

static bool mazda_v0_encoder_add_level(
    SubGhzProtocolEncoderMazdaV0* instance,
    size_t* index,
    bool level,
    uint32_t duration) {
    if(*index >= MAZDA_V0_UPLOAD_CAPACITY) {
        return false;
    }
    instance->encoder.upload[(*index)++] = level_duration_make(level, duration);
    return true;
}

static bool
    mazda_v0_append_byte(SubGhzProtocolEncoderMazdaV0* instance, size_t* index, uint8_t value) {
    if(*index + 16 > MAZDA_V0_UPLOAD_CAPACITY) {
        return false;
    }

    const uint32_t te = subghz_protocol_mazda_v0_const.te_short;

    for(int8_t bit = 7; bit >= 0; bit--) {
        const bool bit_value = ((value >> bit) & 1) != 0;
        if(!bit_value) {
            if(!mazda_v0_encoder_add_level(instance, index, false, te)) {
                return false;
            }
            if(!mazda_v0_encoder_add_level(instance, index, true, te)) {
                return false;
            }
        } else {
            if(!mazda_v0_encoder_add_level(instance, index, true, te)) {
                return false;
            }
            if(!mazda_v0_encoder_add_level(instance, index, false, te)) {
                return false;
            }
        }
    }

    return true;
}

static bool mazda_v0_build_upload(SubGhzProtocolEncoderMazdaV0* instance) {
    furi_check(instance);

    size_t index = 0;
    const uint64_t key64 = instance->generic.data;

    for(size_t r = 0; r < 12; r++) {
        if(!mazda_v0_append_byte(instance, &index, 0xFF)) {
            return false;
        }
    }

    if(!mazda_v0_encoder_add_level(instance, &index, false, MAZDA_V0_GAP_US)) {
        return false;
    }

    if(!mazda_v0_append_byte(instance, &index, 0xFF) ||
       !mazda_v0_append_byte(instance, &index, 0xFF) ||
       !mazda_v0_append_byte(instance, &index, MAZDA_V0_SYNC_BYTE)) {
        return false;
    }

    for(int bi = 0; bi < 8; bi++) {
        const uint8_t raw = (uint8_t)((key64 >> (56 - bi * 8)) & 0xFF);
        const uint8_t air = (uint8_t)~raw;
        if(!mazda_v0_append_byte(instance, &index, air)) {
            return false;
        }
    }

    if(!mazda_v0_append_byte(instance, &index, MAZDA_V0_TAIL_BYTE)) {
        return false;
    }

    if(!mazda_v0_encoder_add_level(instance, &index, false, MAZDA_V0_GAP_US)) {
        return false;
    }

    instance->encoder.front = 0;
    instance->encoder.size_upload = index;

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
void subghz_protocol_encoder_common_stop(void* context) {
    SubGhzProtocolEncoderCommon* instance = context;
    instance->encoder.is_running = false;
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

void* subghz_protocol_encoder_mazda_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolEncoderMazdaV0* instance = calloc(1, sizeof(SubGhzProtocolEncoderMazdaV0));
    furi_check(instance);

    instance->base.protocol = &subghz_protocol_mazda_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 10;
    instance->encoder.size_upload = 0;
    instance->encoder.front = 0;
    instance->encoder.is_running = false;
    instance->encoder.upload = malloc(MAZDA_V0_UPLOAD_CAPACITY * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);

    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_mazda_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderMazdaV0* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    instance->encoder.repeat = 10;

    do {
        FuriString* temp_str = furi_string_alloc();
        if(!temp_str) {
            break;
        }

        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_string(flipper_format, "Protocol", temp_str)) {
            furi_string_free(temp_str);
            break;
        }

        if(!furi_string_equal(temp_str, instance->base.protocol->name)) {
            furi_string_free(temp_str);
            break;
        }
        furi_string_free(temp_str);

        flipper_format_rewind(flipper_format);
        SubGhzProtocolStatus load_st = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_mazda_v0_const.min_count_bit_for_found);
        if(load_st != SubGhzProtocolStatusOk) {
            break;
        }

        mazda_v0_decode_key(&instance->generic);

        uint32_t u32 = 0;

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Serial", &u32, 1)) {
            instance->generic.serial = u32;
        }
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Btn", &u32, 1)) {
            instance->generic.btn = (uint8_t)u32;
        }
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Cnt", &u32, 1)) {
            instance->generic.cnt = u32;
        }

        instance->serial = instance->generic.serial;
        instance->button = instance->generic.btn;
        instance->count = instance->generic.cnt;

        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_uint32(
               flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1)) {
            instance->encoder.repeat = 10;
        }

        {
            const uint8_t original_btn = (uint8_t)(instance->generic.btn & 0x0FU);
            if(subghz_custom_btn_get_original() == 0) {
                subghz_custom_btn_set_original(original_btn);
            }
            subghz_custom_btn_set_max(4);
            uint8_t custom_btn_id = subghz_custom_btn_get();
            switch(custom_btn_id) {
            case SUBGHZ_CUSTOM_BTN_UP:    instance->generic.btn = 0x1U; break;
            case SUBGHZ_CUSTOM_BTN_OK:    instance->generic.btn = 0x2U; break;
            case SUBGHZ_CUSTOM_BTN_DOWN:  instance->generic.btn = 0x4U; break;
            case SUBGHZ_CUSTOM_BTN_RIGHT: instance->generic.btn = 0x8U; break;
            default:                      instance->generic.btn = original_btn; break;
            }
        }

        instance->generic.btn &= 0x0FU;

        {
            uint32_t mazda_v0_txraw = 0;
            flipper_format_rewind(flipper_format);
            flipper_format_read_uint32(flipper_format, "TxRaw", &mazda_v0_txraw, 1);
            flipper_format_rewind(flipper_format);
            if(mazda_v0_txraw == 0) {
                uint32_t mult = furi_hal_subghz_get_rolling_counter_mult();
                if(mult == 0U) mult = 1U;
                instance->generic.cnt = (instance->generic.cnt + mult);
            }
        }
        instance->generic.cnt &= 0xFFFFFU;

        instance->generic.data = mazda_v0_encode_key(
            instance->generic.serial, instance->generic.btn, instance->generic.cnt);
        instance->generic.data_count_bit = subghz_protocol_mazda_v0_const.min_count_bit_for_found;

        instance->serial = instance->generic.serial;
        instance->button = instance->generic.btn;
        instance->count = instance->generic.cnt;

        if(!mazda_v0_build_upload(instance)) {
            break;
        }

        if(instance->encoder.size_upload == 0) {
            break;
        }

        flipper_format_rewind(flipper_format);
        uint8_t key_data[sizeof(uint64_t)];
        mazda_v0_u64_to_bytes_be(instance->generic.data, key_data);
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(key_data))) {
            break;
        }

        uint32_t chk =
            mazda_v0_calculate_checksum(instance->serial, instance->button, instance->count);
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_uint32(flipper_format, "Checksum", &chk, 1);

        instance->encoder.is_running = true;

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

static const SubGhzProtocolEncoder enc_mazda_v0_vt = {
    .alloc = subghz_protocol_encoder_mazda_v0_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_mazda_v0_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_mazda_v0 = {
    .name = MAZDA_PROTOCOL_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_mazda_v0_vt,
};

static const SubGhzEncodePluginEntry enc_mazda_v0_entry = {.name = MAZDA_PROTOCOL_V0_NAME, .encoder = &enc_mazda_v0_vt};
static const FlipperAppPluginDescriptor enc_mazda_v0_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_mazda_v0_entry};
const FlipperAppPluginDescriptor* enc_mazda_v0_ep(void) { return &enc_mazda_v0_descriptor; }
