/* enc_subaru.fal : SD encoder for subaru (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_common_free enc_subaru__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_subaru__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_subaru__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_subaru_alloc enc_subaru__subghz_protocol_encoder_subaru_alloc
#define subghz_protocol_encoder_subaru_deserialize enc_subaru__subghz_protocol_encoder_subaru_deserialize
#define subghz_protocol_subaru enc_subaru__subghz_protocol_subaru
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/subaru.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

struct SubGhzProtocolEncoderSubaru {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    
    uint64_t key;
    uint32_t serial;
    uint8_t button;
    uint16_t count;
};

static const SubGhzBlockConst subghz_protocol_subaru_const = {
    .te_short = 800,
    .te_long = 1600,
    .te_delta = 250,
    .min_count_bit_for_found = 64,
};

static uint8_t subaru_get_button_code(uint8_t custom_btn);
static uint8_t subaru_btn_to_custom(uint8_t btn_code);
static void subaru_decode_count(const uint8_t* KB, uint16_t* count);
static void subaru_encode_count(uint8_t* KB, uint16_t count);
static void subghz_protocol_encoder_subaru_get_upload(SubGhzProtocolEncoderSubaru* instance);

static uint8_t subaru_get_button_code(uint8_t custom_btn) {
    switch(custom_btn) {
        case 1: return 0x01;
        case 2: return 0x02;
        case 3: return 0x03;
        case 4: return 0x04;
        case 5: return 0x08;
        default: return 0x01;
    }
}

static uint8_t subaru_btn_to_custom(uint8_t btn_code) {
    switch(btn_code) {
        case 0x01: return 1;
        case 0x02: return 2;
        case 0x03: return 3;
        case 0x04: return 4;
        case 0x08: return 5;
        default: return 1;
    }
}

static void subaru_decode_count(const uint8_t* KB, uint16_t* count) {
    uint8_t lo = 0;
    if((KB[4] & 0x40) == 0) lo |= 0x01;
    if((KB[4] & 0x80) == 0) lo |= 0x02;
    if((KB[5] & 0x01) == 0) lo |= 0x04;
    if((KB[5] & 0x02) == 0) lo |= 0x08;
    if((KB[6] & 0x01) == 0) lo |= 0x10;
    if((KB[6] & 0x02) == 0) lo |= 0x20;
    if((KB[5] & 0x40) == 0) lo |= 0x40;
    if((KB[5] & 0x80) == 0) lo |= 0x80;
    
    uint8_t REG_SH1 = (KB[7] << 4) & 0xF0;
    if(KB[5] & 0x04) REG_SH1 |= 0x04;
    if(KB[5] & 0x08) REG_SH1 |= 0x08;
    if(KB[6] & 0x80) REG_SH1 |= 0x02;
    if(KB[6] & 0x40) REG_SH1 |= 0x01;
    
    uint8_t REG_SH2 = ((KB[6] << 2) & 0xF0) | ((KB[7] >> 4) & 0x0F);
    
    uint8_t SER0 = KB[3];
    uint8_t SER1 = KB[1];
    uint8_t SER2 = KB[2];
    
    uint8_t total_rot = 4 + lo;
    for(uint8_t i = 0; i < total_rot; ++i) {
        uint8_t t_bit = (SER0 >> 7) & 1;
        SER0 = ((SER0 << 1) & 0xFE) | ((SER1 >> 7) & 1);
        SER1 = ((SER1 << 1) & 0xFE) | ((SER2 >> 7) & 1);
        SER2 = ((SER2 << 1) & 0xFE) | t_bit;
    }
    
    uint8_t T1 = SER1 ^ REG_SH1;
    uint8_t T2 = SER2 ^ REG_SH2;
    
    uint8_t hi = 0;
    if((T1 & 0x10) == 0) hi |= 0x04;
    if((T1 & 0x20) == 0) hi |= 0x08;
    if((T2 & 0x80) == 0) hi |= 0x02;
    if((T2 & 0x40) == 0) hi |= 0x01;
    if((T1 & 0x01) == 0) hi |= 0x40;
    if((T1 & 0x02) == 0) hi |= 0x80;
    if((T2 & 0x08) == 0) hi |= 0x20;
    if((T2 & 0x04) == 0) hi |= 0x10;
    
    *count = ((hi << 8) | lo) & 0xFFFF;
}

static void subaru_encode_count(uint8_t* KB, uint16_t count) {
    uint8_t lo = count & 0xFF;
    uint8_t hi = (count >> 8) & 0xFF;
    
    KB[4] &= ~0xC0;
    KB[5] &= ~0xC3;
    KB[6] &= ~0x03;
    
    if((lo & 0x01) == 0) KB[4] |= 0x40;
    if((lo & 0x02) == 0) KB[4] |= 0x80;
    if((lo & 0x04) == 0) KB[5] |= 0x01;
    if((lo & 0x08) == 0) KB[5] |= 0x02;
    if((lo & 0x10) == 0) KB[6] |= 0x01;
    if((lo & 0x20) == 0) KB[6] |= 0x02;
    if((lo & 0x40) == 0) KB[5] |= 0x40;
    if((lo & 0x80) == 0) KB[5] |= 0x80;
    
    uint8_t SER0 = KB[3];
    uint8_t SER1 = KB[1];
    uint8_t SER2 = KB[2];
    
    uint8_t total_rot = 4 + lo;
    for(uint8_t i = 0; i < total_rot; ++i) {
        uint8_t t_bit = (SER0 >> 7) & 1;
        SER0 = ((SER0 << 1) & 0xFE) | ((SER1 >> 7) & 1);
        SER1 = ((SER1 << 1) & 0xFE) | ((SER2 >> 7) & 1);
        SER2 = ((SER2 << 1) & 0xFE) | t_bit;
    }
    
    uint8_t T1 = 0xFF;
    uint8_t T2 = 0xFF;
    
    if(hi & 0x04) T1 &= ~0x10;
    if(hi & 0x08) T1 &= ~0x20;
    if(hi & 0x02) T2 &= ~0x80;
    if(hi & 0x01) T2 &= ~0x40;
    if(hi & 0x40) T1 &= ~0x01;
    if(hi & 0x80) T1 &= ~0x02;
    if(hi & 0x20) T2 &= ~0x08;
    if(hi & 0x10) T2 &= ~0x04;
    
    uint8_t new_REG_SH1 = T1 ^ SER1;
    uint8_t new_REG_SH2 = T2 ^ SER2;
    
    KB[5] &= ~0x0C;
    KB[6] &= ~0xC0;
    
    KB[7] = (KB[7] & 0xF0) | ((new_REG_SH1 >> 4) & 0x0F);
    
    if(new_REG_SH1 & 0x04) KB[5] |= 0x04;
    if(new_REG_SH1 & 0x08) KB[5] |= 0x08;
    if(new_REG_SH1 & 0x02) KB[6] |= 0x80;
    if(new_REG_SH1 & 0x01) KB[6] |= 0x40;
    
    KB[6] = (KB[6] & 0xC3) | ((new_REG_SH2 >> 2) & 0x3C);
    
    KB[7] = (KB[7] & 0x0F) | ((new_REG_SH2 << 4) & 0xF0);
}

static void subghz_protocol_encoder_subaru_get_upload(SubGhzProtocolEncoderSubaru* instance) {
    furi_assert(instance);
    size_t index = 0;
    
    uint32_t te_short = subghz_protocol_subaru_const.te_short;
    uint32_t te_long = subghz_protocol_subaru_const.te_long;
    uint32_t gap_duration = 2500;
    uint32_t sync_duration = 2500;
    
    for(int i = 0; i < 20; i++) {
        instance->encoder.upload[index++] = level_duration_make(true, te_long);
        instance->encoder.upload[index++] = level_duration_make(false, te_long);
    }
    instance->encoder.upload[index++] = level_duration_make(true, te_long);
    
    instance->encoder.upload[index++] = level_duration_make(false, gap_duration);
    
    instance->encoder.upload[index++] = level_duration_make(true, sync_duration);
    
    instance->encoder.upload[index++] = level_duration_make(false, te_long);
    
    for(int i = 63; i >= 0; i--) {
        bool bit = (instance->key >> i) & 1;
        if(bit) {
            instance->encoder.upload[index++] = level_duration_make(true, te_short);
            instance->encoder.upload[index++] = level_duration_make(false, te_short);
        } else {
            instance->encoder.upload[index++] = level_duration_make(true, te_long);
            instance->encoder.upload[index++] = level_duration_make(false, te_short);
        }
    }
    
    instance->encoder.upload[index++] = level_duration_make(true, te_long);
    instance->encoder.upload[index++] = level_duration_make(false, gap_duration * 2);
    
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

void* subghz_protocol_encoder_subaru_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderSubaru* instance = malloc(sizeof(SubGhzProtocolEncoderSubaru));
    
    instance->base.protocol = &subghz_protocol_subaru;
    instance->generic.protocol_name = instance->base.protocol->name;
    
    instance->encoder.repeat = 10;
    instance->encoder.size_upload = 2048;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    
    return instance;
}

SubGhzProtocolStatus subghz_protocol_encoder_subaru_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderSubaru* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        
        uint64_t original_key = instance->generic.data;
        
        uint8_t b[8];
        for(int i = 0; i < 8; i++) {
            b[i] = (uint8_t)(original_key >> (56 - i * 8));
        }
        
        instance->serial = ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
        instance->button = b[0] & 0x0F;
        subaru_decode_count(b, &instance->count);
        
        uint8_t original_custom_btn = subaru_btn_to_custom(instance->button);
        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(original_custom_btn);
        }
        subghz_custom_btn_set_max(5);
        
        uint8_t selected_custom_btn;
        if(subghz_custom_btn_get() == SUBGHZ_CUSTOM_BTN_OK) {
            selected_custom_btn = subghz_custom_btn_get_original();
        } else {
            selected_custom_btn = subghz_custom_btn_get();
        }
        
        uint8_t new_button = subaru_get_button_code(selected_custom_btn);
        subghz_block_generic_global_button_override_get(&new_button);
        instance->button = new_button;
        
        uint32_t subaru_txraw = 0;
        flipper_format_rewind(flipper_format);
        flipper_format_read_uint32(flipper_format, "TxRaw", &subaru_txraw, 1);

        uint32_t override_cnt = 0;
        if(subghz_block_generic_global_counter_override_get(&override_cnt)) {
            instance->count = override_cnt & 0xFFFF;
        } else if(subaru_txraw == 0) {
            uint32_t mult = furi_hal_subghz_get_rolling_counter_mult();
            instance->count = (instance->count + mult) & 0xFFFF;
        }
        
        b[0] = (b[0] & 0xF0) | (instance->button & 0x0F);
        
        subaru_encode_count(b, instance->count);
        
        instance->key = 0;
        for(int i = 0; i < 8; i++) {
            instance->key = (instance->key << 8) | b[i];
        }
        
        instance->generic.data = instance->key;
        instance->generic.serial = instance->serial;
        instance->generic.btn = instance->button;
        instance->generic.cnt = instance->count;
        
        subghz_protocol_encoder_subaru_get_upload(instance);
        
        if(!flipper_format_rewind(flipper_format)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        
        uint8_t key_data[sizeof(uint64_t)] = {0};
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->key >> (i * 8)) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            ret = SubGhzProtocolStatusErrorParserKey;
            break;
        }

        uint32_t temp_btn = instance->generic.btn;
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_uint32(flipper_format, "Btn", &temp_btn, 1);

        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &instance->generic.cnt, 1);
        
        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
        
    } while(false);
    
    return ret;
}

static const SubGhzProtocolEncoder enc_subaru_vt = {
    .alloc = subghz_protocol_encoder_subaru_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_subaru_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_subaru = {
    .name = SUBGHZ_PROTOCOL_SUBARU_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | 
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | 
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_subaru_vt,
};

static const SubGhzEncodePluginEntry enc_subaru_entry = {.name = SUBGHZ_PROTOCOL_SUBARU_NAME, .encoder = &enc_subaru_vt};
static const FlipperAppPluginDescriptor enc_subaru_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_subaru_entry};
const FlipperAppPluginDescriptor* enc_subaru_ep(void) { return &enc_subaru_descriptor; }
