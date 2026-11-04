/* enc_ford_v0.fal : SD encoder for ford_v0 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_ford_v0__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_ford_v0__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_ford_v0__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_ford_v0_alloc enc_ford_v0__subghz_protocol_encoder_ford_v0_alloc
#define subghz_protocol_encoder_ford_v0_deserialize enc_ford_v0__subghz_protocol_encoder_ford_v0_deserialize
#define subghz_protocol_ford_v0 enc_ford_v0__subghz_protocol_ford_v0
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/ford_v0.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/subghz/protocols/common.h>

typedef struct SubGhzProtocolEncoderFordV0 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint64_t key1;
    uint16_t key2;
    uint32_t serial;
    uint8_t button;
    uint32_t count;
} 
SubGhzProtocolEncoderFordV0;

static const SubGhzBlockConst subghz_protocol_ford_v0_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

static const uint8_t ford_v0_crc_matrix[64] = {
    0xDA, 0xB5, 0x55, 0x6A, 0xAA, 0xAA, 0xAA, 0xD5,
    0xB6, 0x6C, 0xCC, 0xD9, 0x99, 0x99, 0x99, 0xB3,
    0x71, 0xE3, 0xC3, 0xC7, 0x87, 0x87, 0x87, 0x8F,
    0x0F, 0xE0, 0x3F, 0xC0, 0x7F, 0x80, 0x7F, 0x80,
    0x00, 0x1F, 0xFF, 0xC0, 0x00, 0x7F, 0xFF, 0x80,
    0x00, 0x00, 0x00, 0x3F, 0xFF, 0xFF, 0xFF, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F,
    0x23, 0x12, 0x94, 0x84, 0x35, 0xF4, 0x55, 0x84,
};

static uint8_t ford_v0_popcount8(uint8_t x);
static uint8_t ford_v0_calculate_crc(uint8_t* buf);
static uint8_t ford_v0_calculate_crc_for_tx(uint64_t key1, uint8_t chk);
static uint8_t ford_v0_calculate_chk_from_buf(uint8_t* buf);
static uint8_t ford_v0_get_button_code(uint8_t custom_btn);
static uint8_t ford_v0_btn_to_custom(uint8_t btn_code);
static void decode_ford_v0(uint64_t key1, uint16_t key2, uint32_t* serial, uint8_t* button, uint32_t* count);
static void encode_ford_v0(uint64_t original_key1, uint32_t serial, uint8_t button, uint32_t count, uint8_t* chk_out, uint64_t* key1_out);
static void subghz_protocol_encoder_ford_v0_get_upload(SubGhzProtocolEncoderFordV0* instance);

static uint8_t ford_v0_popcount8(uint8_t x) {
    uint8_t count = 0;
    while(x) {
        count += x & 1;
        x >>= 1;
    }
    return count;
}

static uint8_t ford_v0_calculate_crc(uint8_t* buf) {
    uint8_t crc = 0;
    for(int row = 0; row < 8; row++) {
        uint8_t xor_sum = 0;
        for(int col = 0; col < 8; col++) {
            xor_sum ^= (ford_v0_crc_matrix[row * 8 + col] & buf[col + 1]);
        }
        uint8_t parity = ford_v0_popcount8(xor_sum) & 1;
        if(parity) {
            crc |= (1 << row);
        }
    }
    return crc;
}

static uint8_t ford_v0_calculate_crc_for_tx(uint64_t key1, uint8_t chk) {
    uint8_t buf[16] = {0};
    for(int i = 0; i < 8; ++i) {
        buf[i] = (uint8_t)(key1 >> (56 - i * 8));
    }
    buf[8] = chk;
    uint8_t crc = ford_v0_calculate_crc(buf);
    return crc ^ 0x80;
}

static uint8_t ford_v0_calculate_chk_from_buf(uint8_t* buf) {
    uint8_t checksum = 0;
    for(int i = 1; i <= 7; i++)
        checksum += buf[i];
    return checksum;
}

static uint8_t ford_v0_get_button_code(uint8_t custom_btn) {
    switch(custom_btn) {
        case 1: return 0x01;
        case 2: return 0x02;
        case 3: return 0x04;
        default: return 0x01;
    }
}

static uint8_t ford_v0_btn_to_custom(uint8_t btn_code) {
    switch(btn_code) {
        case 0x01: return 1;
        case 0x02: return 2;
        case 0x04: return 3;
        default: return 1;
    }
}

static void decode_ford_v0(uint64_t key1, uint16_t key2, uint32_t* serial, uint8_t* button, uint32_t* count) {
    uint8_t buf[13] = {0};
    
    for(int i = 0; i < 8; ++i) {
        buf[i] = (uint8_t)(key1 >> (56 - i * 8));
    }
    
    buf[8] = (uint8_t)(key2 >> 8);
    buf[9] = (uint8_t)(key2 & 0xFF);
    
    uint8_t tmp = buf[8];
    uint8_t parity = 0;
    uint8_t parity_any = (tmp != 0);
    while(tmp) {
        parity ^= (tmp & 1);
        tmp >>= 1;
    }
    buf[11] = parity_any ? parity : 0;
    
    uint8_t xor_byte;
    uint8_t limit;
    if(buf[11]) {
        xor_byte = buf[7];
        limit = 7;
    } else {
        xor_byte = buf[6];
        limit = 6;
    }
    
    for(int idx = 1; idx < limit; ++idx) {
        buf[idx] ^= xor_byte;
    }
    
    if(buf[11] == 0) {
        buf[7] ^= xor_byte;
    }
    
    uint8_t orig_b7 = buf[7];
    
    buf[7] = (orig_b7 & 0xAA) | (buf[6] & 0x55);
    uint8_t mixed = (buf[6] & 0xAA) | (orig_b7 & 0x55);
    buf[12] = mixed;
    buf[6] = mixed;
    
    uint32_t serial_le = ((uint32_t)buf[1]) |
                         ((uint32_t)buf[2] << 8) |
                         ((uint32_t)buf[3] << 16) |
                         ((uint32_t)buf[4] << 24);
    
    *serial = ((serial_le & 0xFF) << 24) |
              (((serial_le >> 8) & 0xFF) << 16) |
              (((serial_le >> 16) & 0xFF) << 8) |
              ((serial_le >> 24) & 0xFF);

    *button = (buf[5] >> 4) & 0x0F;

    *count = ((buf[5] & 0x0F) << 16) |
             (buf[6] << 8) |
             buf[7];
}

static void encode_ford_v0(uint64_t original_key1, uint32_t serial, uint8_t button, uint32_t count, uint8_t* chk_out, uint64_t* key1_out) {
    uint8_t buf[13] = {0};
    
    buf[0] = (uint8_t)(original_key1 >> 56);
    
    buf[1] = (serial >> 24) & 0xFF;
    buf[2] = (serial >> 16) & 0xFF;
    buf[3] = (serial >> 8) & 0xFF;
    buf[4] = serial & 0xFF;
    buf[5] = (button << 4) | ((count >> 16) & 0x0F);
    buf[6] = (count >> 8) & 0xFF;
    buf[7] = count & 0xFF;
    
    uint8_t checksum = ford_v0_calculate_chk_from_buf(buf);
    
    uint8_t orig_b7 = buf[7];
    uint8_t orig_b6 = buf[6];
    buf[7] = (orig_b7 & 0xAA) | (orig_b6 & 0x55);
    buf[6] = (orig_b6 & 0xAA) | (orig_b7 & 0x55);
    
    if(chk_out) *chk_out = checksum;
    buf[8] = checksum;
    uint8_t tmp = buf[8];
    uint8_t parity = 0;
    uint8_t parity_any = (tmp != 0);
    while(tmp) {
        parity ^= (tmp & 1);
        tmp >>= 1;
    }
    uint8_t buf_11 = parity_any ? parity : 0;
    
    uint8_t xor_byte;
    uint8_t limit;
    if(buf_11) {
        xor_byte = buf[7];
        limit = 7;
    } else {
        xor_byte = buf[6];
        limit = 6;
    }
    
    if(buf_11 == 0) {
        buf[7] ^= xor_byte;
    }
    
    for(int idx = 1; idx < limit; idx++) {
        buf[idx] ^= xor_byte;
    }
    
    *key1_out = 0;
    for(int i = 0; i < 8; i++) {
        *key1_out = (*key1_out << 8) | buf[i];
    }
}

static void subghz_protocol_encoder_ford_v0_get_upload(SubGhzProtocolEncoderFordV0* instance) {
    furi_assert(instance);
    size_t index = 0;
    
    uint32_t te_short = subghz_protocol_ford_v0_const.te_short;
    uint32_t te_long = subghz_protocol_ford_v0_const.te_long;
    uint32_t gap_duration = 3500;
    
    for(int i = 0; i < 20; i++) {
        instance->encoder.upload[index++] = level_duration_make(true, te_short);
        instance->encoder.upload[index++] = level_duration_make(false, te_long);
        instance->encoder.upload[index++] = level_duration_make(true, te_long);
    }
    
    instance->encoder.upload[index++] = level_duration_make(true, te_short);
    
    instance->encoder.upload[index++] = level_duration_make(false, gap_duration);
    
    uint64_t key1_inv = ~instance->key1;
    uint16_t key2_inv = ~instance->key2;
    
    for(int i = 63; i >= 0; i--) {
        bool bit = (key1_inv >> i) & 1;
        if(bit) {
            instance->encoder.upload[index++] = level_duration_make(true, te_short);
            instance->encoder.upload[index++] = level_duration_make(false, te_short);
        } else {
            instance->encoder.upload[index++] = level_duration_make(false, te_short);
            instance->encoder.upload[index++] = level_duration_make(true, te_short);
        }
    }
    
    for(int i = 15; i >= 0; i--) {
        bool bit = (key2_inv >> i) & 1;
        if(bit) {
            instance->encoder.upload[index++] = level_duration_make(true, te_short);
            instance->encoder.upload[index++] = level_duration_make(false, te_short);
        } else {
            instance->encoder.upload[index++] = level_duration_make(false, te_short);
            instance->encoder.upload[index++] = level_duration_make(true, te_short);
        }
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

void* subghz_protocol_encoder_ford_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderFordV0* instance = malloc(sizeof(SubGhzProtocolEncoderFordV0));
    
    instance->base.protocol = &subghz_protocol_ford_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    
    instance->encoder.repeat = 3;
    instance->encoder.size_upload = 1024;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    
    return instance;
}

SubGhzProtocolStatus subghz_protocol_encoder_ford_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderFordV0* instance = context;
    
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t chk_old = 0, crc_old = 0;
    flipper_format_read_uint32(flipper_format, "CheckSum", &chk_old, 1);
    flipper_format_read_uint32(flipper_format, "CRC", &crc_old, 1);
    uint16_t key2_old = ((chk_old & 0xFF) << 8) | (crc_old & 0xFF);

    decode_ford_v0(instance->generic.data, key2_old, &instance->serial, &instance->button, &instance->count);

    uint8_t custom_btn = ford_v0_btn_to_custom(instance->button);
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(custom_btn);
    }
    subghz_custom_btn_set_max(3);
    
    uint32_t ford_v0_txraw = 0;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "TxRaw", &ford_v0_txraw, 1);
    flipper_format_rewind(flipper_format);
    if(ford_v0_txraw == 0) {
        instance->count = (instance->count + furi_hal_subghz_get_rolling_counter_mult()) & 0xFFFFF;
    }
    
    uint8_t btn = subghz_custom_btn_get() == SUBGHZ_CUSTOM_BTN_OK ?
                      subghz_custom_btn_get_original() :
                      subghz_custom_btn_get();
    instance->button = ford_v0_get_button_code(btn);

    uint8_t new_chk = 0;
    encode_ford_v0(instance->generic.data, instance->serial, instance->button, instance->count, &new_chk, &instance->key1);
    
    uint8_t new_crc = ford_v0_calculate_crc_for_tx(instance->key1, new_chk);
    instance->key2 = ((uint16_t)new_chk << 8) | new_crc;

    subghz_protocol_encoder_ford_v0_get_upload(instance);
    
    flipper_format_rewind(flipper_format);
    uint8_t key_data[8];
    for(int i = 0; i < 8; i++) key_data[7-i] = (instance->key1 >> (i * 8)) & 0xFF;
    flipper_format_update_hex(flipper_format, "Key", key_data, 8);
    flipper_format_update_uint32(flipper_format, "CheckSum", (uint32_t[]){new_chk}, 1);
    flipper_format_update_uint32(flipper_format, "CRC", (uint32_t[]){new_crc}, 1);

    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

static const SubGhzProtocolEncoder enc_ford_v0_vt = {
    .alloc = subghz_protocol_encoder_ford_v0_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_ford_v0_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_ford_v0 = {
    .name = SUBGHZ_PROTOCOL_FORD_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_ford_v0_vt,
};

static const SubGhzEncodePluginEntry enc_ford_v0_entry = {.name = SUBGHZ_PROTOCOL_FORD_V0_NAME, .encoder = &enc_ford_v0_vt};
static const FlipperAppPluginDescriptor enc_ford_v0_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_ford_v0_entry};
const FlipperAppPluginDescriptor* enc_ford_v0_ep(void) { return &enc_ford_v0_descriptor; }
