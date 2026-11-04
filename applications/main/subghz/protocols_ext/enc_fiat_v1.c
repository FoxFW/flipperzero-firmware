/* enc_fiat_v1.fal : SD encoder for fiat_v1 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define fiat_v1_protocol enc_fiat_v1__fiat_v1_protocol
#define subghz_protocol_encoder_common_free enc_fiat_v1__subghz_protocol_encoder_common_free
#define subghz_protocol_encoder_common_yield enc_fiat_v1__subghz_protocol_encoder_common_yield
#define subghz_protocol_encoder_fiat_v1_alloc enc_fiat_v1__subghz_protocol_encoder_fiat_v1_alloc
#define subghz_protocol_encoder_fiat_v1_deserialize enc_fiat_v1__subghz_protocol_encoder_fiat_v1_deserialize
#define subghz_protocol_encoder_fiat_v1_stop enc_fiat_v1__subghz_protocol_encoder_fiat_v1_stop
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/fiat_v1.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>
#include <lib/subghz/protocols/common.h>

#define TAG "FiatProtocolV1"
#define FIAT_V1_TE_SHORT          250U
#define FIAT_V1_TE_LONG           500U
#define FIAT_V1_TE_DELTA          100U
#define FIAT_V1_WIRE_BITS         104U
#define FIAT_V1_WIRE_BYTES        13U
#define FIAT_V1_LOGICAL_BITS      102U
#define FIAT_V1_DEFAULT_TAIL_BITS 2U
#define FIAT_V1_RAW_FIELD         "Raw"
#define FIAT_V1_ENC_LEAD_US        2033U
#define FIAT_V1_ENC_GAP_US         3252U
#define FIAT_V1_ENC_DEFAULT_REPEAT 6U
#define FIAT_V1_UPLOAD_CAPACITY    240U

struct SubGhzProtocolEncoderFiatV1 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_data[FIAT_V1_WIRE_BYTES];
    uint8_t hitag2_key[6];
    uint32_t epoch;
    uint32_t hop;
    uint8_t tail_bits;
    uint8_t frame_xor;
};

static const SubGhzBlockConst subghz_protocol_fiat_v1_const = {
    .te_short = FIAT_V1_TE_SHORT,
    .te_long = FIAT_V1_TE_LONG,
    .te_delta = FIAT_V1_TE_DELTA,
    .min_count_bit_for_found = FIAT_V1_LOGICAL_BITS,
};

static bool fiat_v1_button_valid(uint8_t button);
static uint8_t fiat_v1_frame_xor(const uint8_t raw[FIAT_V1_WIRE_BYTES]);
static void fiat_v1_build_raw(
    uint8_t raw[FIAT_V1_WIRE_BYTES],
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    uint32_t auth,
    uint8_t tail_bits);
static uint32_t fiat_v1_uid(const uint8_t raw[FIAT_V1_WIRE_BYTES]);
static uint32_t fiat_v1_counter(const uint8_t raw[FIAT_V1_WIRE_BYTES]);
static bool fiat_v1_frame_valid(const uint8_t raw[FIAT_V1_WIRE_BYTES]);
static uint8_t fiat_v1_truth(uint32_t table, uint8_t index);
static uint8_t fiat_v1_filter_index(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
static uint8_t fiat_v1_byte_bit(uint8_t byte, uint8_t bit);
static uint8_t fiat_v1_bcm_hitag2_filter(const uint8_t state[6]);
static uint8_t fiat_v1_parity8(uint8_t value);
static uint8_t fiat_v1_bcm_hitag2_feedback(const uint8_t state[6]);
static void fiat_v1_bcm_hitag2_shift(uint8_t state[6], uint8_t input);
static uint8_t fiat_v1_input_bit_u32_be(uint32_t value, uint8_t index);
static uint8_t fiat_v1_input_bit_bytes_be(const uint8_t* bytes, uint8_t index);
static uint32_t fiat_v1_bcm_generate_authenticator(
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    const uint8_t key[6],
    uint32_t epoch);
static bool fiat_v1_encoder_build_upload(SubGhzProtocolEncoderFiatV1* instance);

static bool fiat_v1_button_valid(uint8_t button) {
    return button == 0x1U || button == 0x2U || button == 0x4U || button == 0x8U;
}

static uint8_t fiat_v1_frame_xor(const uint8_t raw[FIAT_V1_WIRE_BYTES]) {
    uint8_t value = 0x01U;
    for(uint8_t i = 0U; i < FIAT_V1_WIRE_BYTES - 1U; i++) {
        value ^= raw[i];
    }
    return value;
}

static void fiat_v1_build_raw(
    uint8_t raw[FIAT_V1_WIRE_BYTES],
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    uint32_t auth,
    uint8_t tail_bits) {
    memset(raw, 0, FIAT_V1_WIRE_BYTES);
    raw[1] = 0x01U;
    raw[2] = (uint8_t)(uid >> 24U);
    raw[3] = (uint8_t)(uid >> 16U);
    raw[4] = (uint8_t)(uid >> 8U);
    raw[5] = (uint8_t)uid;
    raw[6] = (uint8_t)(((button & 0x0FU) << 4U) | ((control >> 6U) & 0x0FU));
    raw[7] = (uint8_t)(((control & 0x3FU) << 2U) | ((auth >> 30U) & 0x03U));
    raw[8] = (uint8_t)(auth >> 22U);
    raw[9] = (uint8_t)(auth >> 14U);
    raw[10] = (uint8_t)(auth >> 6U);
    raw[11] = (uint8_t)((auth << 2U) | (tail_bits & 0x03U));
    raw[12] = fiat_v1_frame_xor(raw);
}

static uint32_t fiat_v1_uid(const uint8_t raw[FIAT_V1_WIRE_BYTES]) {
    return ((uint32_t)raw[2] << 24U) | ((uint32_t)raw[3] << 16U) |
           ((uint32_t)raw[4] << 8U) | raw[5];
}

static uint32_t fiat_v1_counter(const uint8_t raw[FIAT_V1_WIRE_BYTES]) {
    return ((uint32_t)(raw[6] & 0x0FU) << 6U) | (raw[7] >> 2U);
}

static bool fiat_v1_frame_valid(const uint8_t raw[FIAT_V1_WIRE_BYTES]) {
    if(raw[0] != 0x00U || raw[1] != 0x01U) {
        return false;
    }
    if(fiat_v1_frame_xor(raw) != raw[12]) {
        return false;
    }
    if(!fiat_v1_button_valid(raw[6] >> 4U)) {
        return false;
    }

    const uint32_t uid = fiat_v1_uid(raw);
    return uid != 0U && uid != UINT32_MAX;
}

static uint8_t fiat_v1_truth(uint32_t table, uint8_t index) {
    return (uint8_t)((table >> index) & 1U);
}

static uint8_t fiat_v1_filter_index(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (uint8_t)((a << 3U) | (b << 2U) | (c << 1U) | d);
}

static uint8_t fiat_v1_byte_bit(uint8_t byte, uint8_t bit) {
    return (uint8_t)((byte >> bit) & 1U);
}

static uint8_t fiat_v1_bcm_hitag2_filter(const uint8_t state[6]) {
    uint8_t group = 0U;
    group |= fiat_v1_truth(
        0x2c79U,
        fiat_v1_filter_index(
            fiat_v1_byte_bit(state[0], 1U),
            fiat_v1_byte_bit(state[0], 2U),
            fiat_v1_byte_bit(state[0], 4U),
            fiat_v1_byte_bit(state[0], 5U)));
    group |= (uint8_t)(fiat_v1_truth(
                           0x6671U,
                           fiat_v1_filter_index(
                               fiat_v1_byte_bit(state[1], 0U),
                               fiat_v1_byte_bit(state[1], 1U),
                               fiat_v1_byte_bit(state[1], 3U),
                               fiat_v1_byte_bit(state[1], 7U)))
                       << 1U);
    group |= (uint8_t)(fiat_v1_truth(
                           0x6671U,
                           fiat_v1_filter_index(
                               fiat_v1_byte_bit(state[3], 5U),
                               fiat_v1_byte_bit(state[2], 0U),
                               fiat_v1_byte_bit(state[2], 2U),
                               fiat_v1_byte_bit(state[2], 6U)))
                       << 2U);
    group |= (uint8_t)(fiat_v1_truth(
                           0x6671U,
                           fiat_v1_filter_index(
                               fiat_v1_byte_bit(state[4], 6U),
                               fiat_v1_byte_bit(state[3], 0U),
                               fiat_v1_byte_bit(state[3], 2U),
                               fiat_v1_byte_bit(state[3], 3U)))
                       << 3U);
    group |= (uint8_t)(fiat_v1_truth(
                           0x2c79U,
                           fiat_v1_filter_index(
                               fiat_v1_byte_bit(state[5], 1U),
                               fiat_v1_byte_bit(state[5], 3U),
                               fiat_v1_byte_bit(state[5], 4U),
                               fiat_v1_byte_bit(state[4], 5U)))
                       << 4U);
    return fiat_v1_truth(0x7907287bUL, group);
}

static uint8_t fiat_v1_parity8(uint8_t value) {
    value ^= (uint8_t)(value >> 4U);
    value ^= (uint8_t)(value >> 2U);
    value ^= (uint8_t)(value >> 1U);
    return value & 1U;
}

static uint8_t fiat_v1_bcm_hitag2_feedback(const uint8_t state[6]) {
    static const uint8_t masks[6] = {0xb3U, 0x80U, 0x83U, 0x22U, 0x00U, 0x73U};
    uint8_t feedback = 0U;
    for(uint8_t i = 0U; i < 6U; i++) {
        feedback ^= fiat_v1_parity8((uint8_t)(state[i] & masks[i]));
    }
    return feedback & 1U;
}

static void fiat_v1_bcm_hitag2_shift(uint8_t state[6], uint8_t input) {
    for(uint8_t i = 0U; i < 5U; i++) {
        state[i] = (uint8_t)((state[i] << 1U) | (state[i + 1U] >> 7U));
    }
    state[5] = (uint8_t)((state[5] << 1U) | (input & 1U));
}

static uint8_t fiat_v1_input_bit_u32_be(uint32_t value, uint8_t index) {
    return (uint8_t)((value >> (31U - index)) & 1U);
}

static uint8_t fiat_v1_input_bit_bytes_be(const uint8_t* bytes, uint8_t index) {
    return (uint8_t)((bytes[index >> 3U] >> (7U - (index & 7U))) & 1U);
}

static uint32_t fiat_v1_bcm_generate_authenticator(
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    const uint8_t key[6],
    uint32_t epoch) {
    uint8_t state[6] = {
        (uint8_t)(uid >> 24U),
        (uint8_t)(uid >> 16U),
        (uint8_t)(uid >> 8U),
        (uint8_t)uid,
        key[4],
        key[5],
    };

    const uint32_t iv =
        ((epoch & 0x3FFFFUL) << 14U) | (((uint32_t)control & 0x03FFUL) << 4U) |
        ((uint32_t)button & 0x0FUL);

    for(uint8_t i = 0U; i < 32U; i++) {
        const uint8_t input = fiat_v1_input_bit_u32_be(iv, i) ^
                              fiat_v1_input_bit_bytes_be(key, i) ^
                              fiat_v1_bcm_hitag2_filter(state);
        fiat_v1_bcm_hitag2_shift(state, input);
    }

    uint32_t authenticator = 0U;
    for(uint8_t i = 0U; i < 32U; i++) {
        authenticator = (authenticator << 1U) | fiat_v1_bcm_hitag2_filter(state);
        fiat_v1_bcm_hitag2_shift(state, fiat_v1_bcm_hitag2_feedback(state));
    }
    return authenticator;
}

static bool fiat_v1_encoder_build_upload(SubGhzProtocolEncoderFiatV1* instance) {
    furi_check(instance);
    LevelDuration* upload = instance->encoder.upload;
    if(!upload) {
        return false;
    }

    size_t index = 0U;

    upload[index++] = level_duration_make(true, FIAT_V1_ENC_LEAD_US);

    for(uint8_t bit_index = 0U; bit_index < FIAT_V1_WIRE_BITS; bit_index++) {
        const bool bit =
            ((instance->raw_data[bit_index >> 3U] >> (7U - (bit_index & 7U))) & 1U) != 0U;
        upload[index++] = level_duration_make(bit, FIAT_V1_TE_SHORT);
        upload[index++] = level_duration_make(!bit, FIAT_V1_TE_SHORT);
    }

    upload[index++] = level_duration_make(false, FIAT_V1_ENC_GAP_US);
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

void* subghz_protocol_encoder_fiat_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderFiatV1* instance = calloc(1, sizeof(SubGhzProtocolEncoderFiatV1));
    furi_check(instance);

    instance->base.protocol = &fiat_v1_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->tail_bits = FIAT_V1_DEFAULT_TAIL_BITS;
    instance->encoder.repeat = FIAT_V1_ENC_DEFAULT_REPEAT;
    return instance;
}

void subghz_protocol_encoder_fiat_v1_stop(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderFiatV1* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_fiat_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    furi_check(flipper_format);
    SubGhzProtocolEncoderFiatV1* instance = context;

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
       bit_count != FIAT_V1_LOGICAL_BITS) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }
    instance->generic.data_count_bit = bit_count;

    uint32_t serial = 0U;
    uint32_t button = 0U;
    uint32_t control = 0U;
    uint8_t raw_from_file[FIAT_V1_WIRE_BYTES] = {0};

    flipper_format_rewind(flipper_format);
    if(flipper_format_read_hex(flipper_format, FIAT_V1_RAW_FIELD, raw_from_file, sizeof(raw_from_file)) &&
       fiat_v1_frame_valid(raw_from_file)) {
        serial = fiat_v1_uid(raw_from_file);
        button = raw_from_file[6] >> 4U;
        control = fiat_v1_counter(raw_from_file);
        instance->tail_bits = raw_from_file[11] & 0x03U;
    } else {
        SubGhzBlockGeneric generic = {0};
        flipper_format_rewind(flipper_format);
        if(subghz_block_generic_deserialize_check_count_bit(
               &generic, flipper_format, subghz_protocol_fiat_v1_const.min_count_bit_for_found) ==
           SubGhzProtocolStatusOk) {
            serial = (uint32_t)(generic.data >> 32U);
            button = generic.btn;
            control = generic.cnt;
        }
    }

    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Serial", &serial, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Btn", &button, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Cnt", &control, 1);
    if(serial == 0U || serial == UINT32_MAX || !fiat_v1_button_valid((uint8_t)button)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    flipper_format_rewind(flipper_format);
    bool have_hitag2 = flipper_format_read_hex(
        flipper_format, FIAT_V1_HITAG2_KEY_FIELD, instance->hitag2_key, 6U);

    uint32_t epoch = 0U;
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, FIAT_V1_HITAG2_EPOCH_FIELD, &epoch, 1U)) {
        instance->epoch = epoch & 0x3FFFFUL;
    } else {
        instance->epoch = 0U;
    }

    if(have_hitag2) {
        const uint8_t original_btn = (uint8_t)(button & 0x0FU);
        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(original_btn);
        }
        subghz_custom_btn_set_max(4);
        const uint8_t custom_btn_id = subghz_custom_btn_get();

        switch(custom_btn_id) {
        case SUBGHZ_CUSTOM_BTN_UP:
            button = 0x8U;
            break;
        case SUBGHZ_CUSTOM_BTN_DOWN:
            button = 0x4U;
            break;
        case SUBGHZ_CUSTOM_BTN_LEFT:
            button = 0x2U;
            break;
        case SUBGHZ_CUSTOM_BTN_RIGHT:
            button = 0x1U;
            break;
        case SUBGHZ_CUSTOM_BTN_OK:
        default:

            button = original_btn;
            break;
        }

        {
            uint32_t fiat_v1_txraw = 0;
            flipper_format_rewind(flipper_format);
            flipper_format_read_uint32(flipper_format, "TxRaw", &fiat_v1_txraw, 1);
            flipper_format_rewind(flipper_format);
            uint32_t override_cnt = 0U;
            if(subghz_block_generic_global_counter_override_get(&override_cnt)) {
                control = override_cnt;
            } else if(fiat_v1_txraw == 0) {
                control += (uint32_t)furi_hal_subghz_get_rolling_counter_mult();
            }
        }

        control &= 0x03FFU;
        button &= 0x0FU;
        instance->generic.serial = serial;
        instance->generic.btn = (uint8_t)button;
        instance->generic.cnt = control;
        instance->hop = fiat_v1_bcm_generate_authenticator(
            serial, (uint8_t)button, (uint16_t)control, instance->hitag2_key, instance->epoch);
        instance->generic.data = ((uint64_t)serial << 32U) | instance->hop;
        instance->generic.data_count_bit = FIAT_V1_LOGICAL_BITS;

        fiat_v1_build_raw(
            instance->raw_data,
            serial,
            (uint8_t)button,
            (uint16_t)control,
            instance->hop,
            instance->tail_bits);
        instance->frame_xor = instance->raw_data[12];
    } else {
        if(!fiat_v1_frame_valid(raw_from_file)) {
            return SubGhzProtocolStatusErrorParserOthers;
        }
        memcpy(instance->raw_data, raw_from_file, FIAT_V1_WIRE_BYTES);
        instance->generic.serial = fiat_v1_uid(raw_from_file);
        instance->generic.btn = (uint8_t)(raw_from_file[6] >> 4U);
        instance->generic.cnt = fiat_v1_counter(raw_from_file);
        instance->generic.data = ((uint64_t)instance->generic.serial << 32U);
        instance->generic.data_count_bit = FIAT_V1_LOGICAL_BITS;
        instance->hop = 0U;
        instance->frame_xor = instance->raw_data[12];
    }

    flipper_format_rewind(flipper_format);
    flipper_format_insert_or_update_hex(
        flipper_format, FIAT_V1_RAW_FIELD, instance->raw_data, FIAT_V1_WIRE_BYTES);
    flipper_format_rewind(flipper_format);
    {
        uint32_t cnt_store = control;
        flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &cnt_store, 1);
    }

    uint32_t repeat = FIAT_V1_ENC_DEFAULT_REPEAT;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
    instance->encoder.repeat = (repeat == 0U) ? FIAT_V1_ENC_DEFAULT_REPEAT : (size_t)repeat;

    instance->encoder.upload = malloc(FIAT_V1_UPLOAD_CAPACITY * sizeof(LevelDuration));
    if(!fiat_v1_encoder_build_upload(instance)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    instance->encoder.is_running = true;

    FURI_LOG_I(
        TAG,
        "TX UID:%08lX Btn:%02lX Cnt:%03lX Auth:%08lX Epoch:%05lX XOR:%02X",
        (unsigned long)serial,
        (unsigned long)button,
        (unsigned long)control,
        (unsigned long)instance->hop,
        (unsigned long)instance->epoch,
        instance->frame_xor);

    return SubGhzProtocolStatusOk;
}

static const SubGhzProtocolEncoder enc_fiat_v1_vt = {
    .alloc = subghz_protocol_encoder_fiat_v1_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_fiat_v1_deserialize,
    .stop = subghz_protocol_encoder_fiat_v1_stop,
    .yield = subghz_protocol_encoder_common_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol fiat_v1_protocol = {
    .name = FIAT_V1_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = NULL,
    .encoder = &enc_fiat_v1_vt,
};

static const SubGhzEncodePluginEntry enc_fiat_v1_entry = {.name = FIAT_V1_PROTOCOL_NAME, .encoder = &enc_fiat_v1_vt};
static const FlipperAppPluginDescriptor enc_fiat_v1_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_fiat_v1_entry};
const FlipperAppPluginDescriptor* enc_fiat_v1_ep(void) { return &enc_fiat_v1_descriptor; }
