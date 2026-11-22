

#define subghz_protocol_belkorn enc_belkorn__subghz_protocol_belkorn
#define subghz_protocol_encoder_belkorn_alloc enc_belkorn__subghz_protocol_encoder_belkorn_alloc
#define subghz_protocol_encoder_belkorn_deserialize \
    enc_belkorn__subghz_protocol_encoder_belkorn_deserialize
#define subghz_protocol_encoder_common_alloc enc_belkorn__subghz_protocol_encoder_common_alloc
#define subghz_protocol_encoder_common_free enc_belkorn__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_stop enc_belkorn__subghz_protocol_encoder_common_stop
#define subghz_protocol_encoder_common_yield enc_belkorn__subghz_protocol_encoder_common_yield
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/belkorn.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define TAG "BelKorn"

static const SubGhzBlockConst subghz_protocol_belkorn_const = {
    .te_short = 481,
    .te_long = 962,
    .te_delta = 180,
    .min_count_bit_for_found = 118,
};

#define BELKORN_SYNC 0x801ULL

#define BELKORN_L24_MASK   0xFFFFFFUL
#define BELKORN_STEP_A     0x810UL
#define BELKORN_STEP_B     0x1028UL
#define BELKORN_WRAP_A     0x1C282CUL
#define BELKORN_WRAP_B     0x80800UL
#define BELKORN_B3_STEP_A  0x10
#define BELKORN_B34_WRAP_B 0x108
#define BELKORN_B6_BIAS    0xBA
#define BELKORN_B4_BASE    0x5A
#define BELKORN_L24_AB     0x0748CCUL
#define BELKORN_B3_AB      0x30
#define BELKORN_L24_ORIGIN 0x0000E0UL

#define BELKORN_CNT_MASK     0xFFFFU
#define BELKORN_FRAME_LEVELS 120
#define BELKORN_FRAME_REPEAT 3
#define BELKORN_UPLOAD_SIZE  (BELKORN_FRAME_REPEAT * BELKORN_FRAME_LEVELS)

static const uint8_t subghz_protocol_belkorn_btn_codes[] = {0x01, 0x02, 0x04, 0x20};

typedef struct {
    uint8_t serial_hi;
    uint32_t l24;
    uint8_t b3;
} BelKornOrigin;

struct SubGhzProtocolEncoderBelKorn {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

static uint32_t belkorn_get(uint64_t data, uint64_t data_2, uint8_t pos, uint8_t len) {
    const uint64_t mask = ((uint64_t)1 << len) - 1;
    return (pos + len <= 64) ? (uint32_t)((data >> (64 - pos - len)) & mask) :
                               (uint32_t)((data_2 >> (118 - pos - len)) & mask);
}

static void belkorn_put(
    uint8_t serial_hi,
    uint32_t serial,
    uint32_t l24,
    uint8_t b3,
    uint8_t b4,
    uint8_t b5,
    uint8_t b6,
    uint8_t c,
    uint8_t btn,
    uint64_t* data,
    uint64_t* data_2) {
    *data = (BELKORN_SYNC << 52) | ((uint64_t)(serial_hi & 0xF) << 48) | ((uint64_t)serial << 16) |
            (uint64_t)((l24 & 0xFF) << 8) | (uint64_t)((l24 >> 8) & 0xFF);
    *data_2 = ((uint64_t)((l24 >> 16) & 0xFF) << 46) | ((uint64_t)b3 << 38) |
              ((uint64_t)b4 << 30) | ((uint64_t)b5 << 22) | ((uint64_t)b6 << 14) |
              ((uint64_t)c << 6) | (btn & 0x3F);
}

static uint16_t subghz_protocol_belkorn_b6_wraps(uint16_t n) {
    return (n >> 8) + (((n & 0xFF) > 0x45) ? 1 : 0);
}

static uint32_t subghz_protocol_belkorn_jitter(uint8_t lap, uint8_t c) {
    const uint16_t v = (uint16_t)((c >> 1) - 3);
    uint32_t jitter = (uint32_t)lap << 21;
    for(uint8_t i = 0; i < 8; i++) {
        if(!((lap >> i) & 1)) {
            continue;
        }
        if(!((v >> (i + 1)) & 1)) {
            jitter += 257UL << (5 + i);
        }
        if(i && !((v >> (i - 1)) & 1)) {
            jitter += 1UL << (3 + i);
        }
    }
    return jitter;
}

static void subghz_protocol_belkorn_make_frame(
    const BelKornOrigin* origin,
    uint32_t serial,
    uint16_t n,
    uint8_t btn,
    uint64_t* data,
    uint64_t* data_2) {
    const uint8_t c = n & 0xFF;
    const uint8_t lap = n >> 8;
    const uint16_t k = n >> 1;

    if(c & 1) {
        const uint16_t wraps = subghz_protocol_belkorn_b6_wraps(n);
        const uint32_t l24 =
            (origin->l24 + BELKORN_L24_AB + (uint32_t)k * BELKORN_STEP_B -
             (uint32_t)wraps * BELKORN_WRAP_B + subghz_protocol_belkorn_jitter(lap, c)) &
            BELKORN_L24_MASK;
        const uint16_t b34 =
            (uint16_t)((((uint16_t)BELKORN_B4_BASE << 8) | (uint8_t)(origin->b3 - BELKORN_B3_AB)) +
                       wraps * BELKORN_B34_WRAP_B);
        belkorn_put(
            origin->serial_hi, serial, l24, b34 & 0xFF, b34 >> 8, lap,
            (uint8_t)(c + BELKORN_B6_BIAS), c, btn, data, data_2);
    } else {
        const uint32_t l24 =
            (origin->l24 + (uint32_t)k * BELKORN_STEP_A + (uint32_t)lap * BELKORN_WRAP_A) &
            BELKORN_L24_MASK;
        belkorn_put(
            origin->serial_hi, serial, l24, (uint8_t)(origin->b3 + k * BELKORN_B3_STEP_A), c, lap,
            lap, c, btn, data, data_2);
    }
}

static void subghz_protocol_belkorn_check_remote_controller(
    SubGhzBlockGeneric* instance,
    BelKornOrigin* origin) {
    const uint64_t data = instance->data;
    const uint64_t data_2 = instance->data_2;

    origin->serial_hi = belkorn_get(data, data_2, 12, 4);
    instance->serial = belkorn_get(data, data_2, 16, 32);
    instance->btn = belkorn_get(data, data_2, 112, 6);

    const uint32_t l24 = belkorn_get(data, data_2, 48, 8) |
                         (belkorn_get(data, data_2, 56, 8) << 8) |
                         (belkorn_get(data, data_2, 64, 8) << 16);
    const uint8_t b3 = belkorn_get(data, data_2, 72, 8);
    const uint8_t b4 = belkorn_get(data, data_2, 80, 8);
    const uint8_t b5 = belkorn_get(data, data_2, 88, 8);
    const uint8_t b6 = belkorn_get(data, data_2, 96, 8);
    const uint8_t c = belkorn_get(data, data_2, 104, 8);

    const uint16_t n = (uint16_t)(((uint16_t)((c & 1) ? b5 : b6) << 8) | c);
    instance->cnt = n;

    if(c & 1) {
        const uint16_t wraps = subghz_protocol_belkorn_b6_wraps(n);
        origin->l24 =
            (l24 - (uint32_t)(n >> 1) * BELKORN_STEP_B + (uint32_t)wraps * BELKORN_WRAP_B -
             subghz_protocol_belkorn_jitter(n >> 8, c) - BELKORN_L24_AB) &
            BELKORN_L24_MASK;
        origin->b3 = (uint8_t)((((uint16_t)b3 | (uint16_t)b4 << 8) - wraps * BELKORN_B34_WRAP_B) +
                               BELKORN_B3_AB);
    } else {
        origin->l24 =
            (l24 - (uint32_t)(n >> 1) * BELKORN_STEP_A - (uint32_t)(n >> 8) * BELKORN_WRAP_A) &
            BELKORN_L24_MASK;
        origin->b3 = (uint8_t)(b3 - (n >> 1) * BELKORN_B3_STEP_A);
    }

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(3);
}

static uint8_t subghz_protocol_belkorn_get_btn_code(void) {
    const uint8_t original_btn_code = subghz_custom_btn_get_original();
    const uint8_t custom_btn_id = subghz_custom_btn_get();

    if((custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) && (original_btn_code != 0)) {
        return original_btn_code;
    }
    uint8_t index = 0;
    while(index < COUNT_OF(subghz_protocol_belkorn_btn_codes) &&
          subghz_protocol_belkorn_btn_codes[index] != original_btn_code) {
        index++;
    }
    return subghz_protocol_belkorn_btn_codes
        [(index + custom_btn_id) % COUNT_OF(subghz_protocol_belkorn_btn_codes)];
}

static void subghz_protocol_belkorn_encoder_get_upload(
    SubGhzProtocolEncoderBelKorn* instance,
    size_t* index,
    uint64_t data,
    uint64_t data_2,
    uint32_t gap) {
    size_t index_local = *index;
    for(uint8_t i = 0; i < subghz_protocol_belkorn_const.min_count_bit_for_found; i++) {
        const bool set = (i < 64) ? bit_read(data, 63 - i) : bit_read(data_2, 117 - i);
        instance->encoder.upload[index_local++] = level_duration_make(
            (i & 1) == 0,
            set ? (uint32_t)subghz_protocol_belkorn_const.te_long :
                  (uint32_t)subghz_protocol_belkorn_const.te_short);
    }
    instance->encoder.upload[index_local++] =
        level_duration_make(true, (uint32_t)subghz_protocol_belkorn_const.te_short);
    instance->encoder.upload[index_local++] = level_duration_make(false, gap);
    *index = index_local;
}

static void
    subghz_protocol_belkorn_encoder_prepare_packets(SubGhzProtocolEncoderBelKorn* instance) {
    furi_assert(instance);

    BelKornOrigin origin;
    subghz_protocol_belkorn_check_remote_controller(&instance->generic, &origin);

    if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF) {
        if(!subghz_block_generic_global_counter_override_get(&instance->generic.cnt)) {
            instance->generic.cnt += furi_hal_subghz_get_rolling_counter_mult();
        }
    } else {
        instance->generic.cnt += 1;
    }
    instance->generic.cnt &= BELKORN_CNT_MASK;

    instance->generic.btn = subghz_protocol_belkorn_get_btn_code();
    if(subghz_block_generic_global_button_override_get(&instance->generic.btn)) {
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", instance->generic.btn);
    }

    const uint16_t n = (uint16_t)instance->generic.cnt;
    const uint32_t gap = (uint32_t)subghz_protocol_belkorn_const.te_short * 16;
    size_t index = 0;
    uint64_t data = 0;
    uint64_t data_2 = 0;

    subghz_protocol_belkorn_make_frame(
        &origin, instance->generic.serial, n, instance->generic.btn, &data, &data_2);
    for(uint8_t i = 0; i < BELKORN_FRAME_REPEAT; i++) {
        subghz_protocol_belkorn_encoder_get_upload(instance, &index, data, data_2, gap);
    }

    instance->generic.data = data;
    instance->generic.data_2 = data_2;
    instance->encoder.size_upload = index;
}

void subghz_protocol_encoder_common_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderCommon* instance = context;
    free(instance->encoder.upload);
    free(instance);
}
void subghz_protocol_encoder_common_stop(void* context) {
    SubGhzProtocolEncoderCommon* instance = context;
    instance->encoder.is_running = false;
}
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
void* subghz_protocol_encoder_common_alloc(
    size_t instance_size,
    const SubGhzProtocol* protocol,
    size_t repeat,
    size_t size_upload) {
    SubGhzProtocolEncoderCommonGeneric* instance = malloc(instance_size);
    instance->common.base.protocol = protocol;
    instance->generic.protocol_name = protocol->name;
    instance->common.encoder.repeat = repeat;
    instance->common.encoder.size_upload = size_upload;
    instance->common.encoder.upload = malloc(size_upload * sizeof(LevelDuration));
    instance->common.encoder.is_running = false;
    return instance;
}

void* subghz_protocol_encoder_belkorn_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderBelKorn), &subghz_protocol_belkorn, 3, BELKORN_UPLOAD_SIZE);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_belkorn_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderBelKorn* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        if(SubGhzProtocolStatusOk != subghz_block_generic_deserialize_check_count_bit(
                                         &instance->generic,
                                         flipper_format,
                                         subghz_protocol_belkorn_const.min_count_bit_for_found)) {
            FURI_LOG_E(TAG, "Deserialize error");
            break;
        }
        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(!flipper_format_read_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Missing Data");
            break;
        }
        instance->generic.data_2 = 0;
        for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
            instance->generic.data_2 = instance->generic.data_2 << 8 | key_data[i];
        }
        subghz_protocol_belkorn_encoder_prepare_packets(instance);
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data >> i * 8) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to update Key");
            break;
        }
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data_2 >> i * 8) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to update Data");
            break;
        }
        instance->encoder.is_running = true;
        res = SubGhzProtocolStatusOk;
    } while(false);
    return res;
}

static const SubGhzProtocolEncoder enc_belkorn_vt = {
    .alloc = subghz_protocol_encoder_belkorn_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_belkorn_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

const SubGhzProtocol subghz_protocol_belkorn = {
    .name = SUBGHZ_PROTOCOL_BELKORN_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_belkorn_vt,
};

static const SubGhzEncodePluginEntry enc_belkorn_entry = {
    .name = SUBGHZ_PROTOCOL_BELKORN_NAME,
    .encoder = &enc_belkorn_vt};
static const FlipperAppPluginDescriptor enc_belkorn_descriptor = {
    .appid = SUBGHZ_ENCODE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION,
    .entry_point = &enc_belkorn_entry};
const FlipperAppPluginDescriptor* enc_belkorn_ep(void) {
    return &enc_belkorn_descriptor;
}
