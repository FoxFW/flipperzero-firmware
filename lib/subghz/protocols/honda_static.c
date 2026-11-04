#include "honda_static.h"
#include "../blocks/const.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <string.h>
#include <lib/toolbox/level_duration.h>
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

#define HONDA_STATIC_BIT_COUNT       64
#define HONDA_STATIC_MIN_SYMBOLS     36
#define HONDA_STATIC_SHORT_BASE_US   28
#define HONDA_STATIC_SHORT_SPAN_US   70
#define HONDA_STATIC_LONG_BASE_US    61
#define HONDA_STATIC_LONG_SPAN_US    130
#define HONDA_STATIC_SYNC_TIME_US    700
#define HONDA_STATIC_ELEMENT_TIME_US 63
#define HONDA_STATIC_UPLOAD_CAPACITY \
    (1U + HONDA_STATIC_PREAMBLE_ALTERNATING_COUNT + (2U * HONDA_STATIC_BIT_COUNT) + 1U)
#define HONDA_STATIC_SYMBOL_CAPACITY            512
#define HONDA_STATIC_PREAMBLE_ALTERNATING_COUNT 160
#define HONDA_STATIC_PREAMBLE_MAX_TRANSITIONS   19
#define HONDA_STATIC_SYMBOL_BYTE_COUNT          ((HONDA_STATIC_SYMBOL_CAPACITY + 7U) / 8U)

typedef struct {
    uint8_t button;
    uint32_t serial;
    uint32_t counter;
    uint8_t checksum;
} HondaStaticFields;

struct SubGhzProtocolDecoderHondaStatic {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockGeneric generic;

    uint8_t symbols[HONDA_STATIC_SYMBOL_BYTE_COUNT];
    uint16_t symbols_count;
};

struct SubGhzProtocolEncoderHondaStatic {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    HondaStaticFields decoded;
    uint8_t tx_button;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderHondaStatic);


static void honda_static_decoder_commit(
    SubGhzProtocolDecoderHondaStatic* instance,
    const HondaStaticFields* decoded);

static void honda_static_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]) {
    for(size_t index = 0; index < 8; index++) {
        bytes[index] = (data >> ((7U - index) * 8U)) & 0xFFU;
    }
}

static uint64_t honda_static_bytes_to_u64_be(const uint8_t bytes[8]) {
    uint64_t data = 0;
    for(size_t index = 0; index < 8; index++) {
        data = (data << 8U) | bytes[index];
    }
    return data;
}

static uint8_t honda_static_reverse_bits8(uint8_t x) {
    x = (uint8_t)(((x & 0xAAU) >> 1U) | ((x & 0x55U) << 1U));
    x = (uint8_t)(((x & 0xCCU) >> 2U) | ((x & 0x33U) << 2U));
    return (uint8_t)((x >> 4U) | (x << 4U));
}

static uint8_t honda_static_get_bits(const uint8_t* data, uint8_t start, uint8_t count) {
    uint32_t value = 0;
    for(uint8_t i = 0; i < count; i++) {
        const uint8_t bit_index = start + i;
        const uint8_t byte = data[bit_index >> 3U];
        const uint8_t shift = (uint8_t)(~bit_index) & 0x07U;
        value = (value << 1U) | ((byte >> shift) & 1U);
    }
    return (uint8_t)value;
}

static uint32_t honda_static_get_bits_u32(const uint8_t* data, uint8_t start, uint8_t count) {
    uint32_t value = 0;
    for(uint8_t i = 0; i < count; i++) {
        const uint8_t bit_index = start + i;
        const uint8_t byte = data[bit_index >> 3U];
        const uint8_t shift = (uint8_t)(~bit_index) & 0x07U;
        value = (value << 1U) | ((byte >> shift) & 1U);
    }
    return value;
}

static uint8_t honda_static_level_u8(bool level) {
    return level ? 1U : 0U;
}

static void honda_static_symbol_set(uint8_t* buf, uint16_t index, uint8_t v) {
    const uint8_t byte_index = (uint8_t)(index >> 3U);
    const uint8_t shift = (uint8_t)(~index) & 0x07U;
    const uint8_t mask = (uint8_t)(1U << shift);
    if(v) {
        buf[byte_index] |= mask;
    } else {
        buf[byte_index] &= (uint8_t)~mask;
    }
}

static uint8_t honda_static_symbol_get(const uint8_t* buf, uint16_t index) {
    const uint8_t byte_index = (uint8_t)(index >> 3U);
    const uint8_t shift = (uint8_t)(~index) & 0x07U;
    return (uint8_t)((buf[byte_index] >> shift) & 1U);
}

static bool honda_static_is_valid_button(uint8_t button) {
    if(button > 9U) return false;
    return ((0x336U >> button) & 1U) != 0U;
}

static bool honda_static_is_valid_serial(uint32_t serial) {
    return (serial != 0U) && (serial != 0x0FFFFFFFU);
}

static uint8_t honda_static_compact_bytes_checksum(const uint8_t compact[8]) {
    const uint8_t canonical[7] = {
        (uint8_t)((compact[0] << 4U) | (compact[1] >> 4U)),
        (uint8_t)((compact[1] << 4U) | (compact[2] >> 4U)),
        (uint8_t)((compact[2] << 4U) | (compact[3] >> 4U)),
        (uint8_t)((compact[3] << 4U) | (compact[4] >> 4U)),
        compact[5],
        compact[6],
        compact[7],
    };
    uint8_t checksum = 0U;
    for(size_t i = 0; i < COUNT_OF(canonical); i++) {
        checksum ^= canonical[i];
    }
    return checksum;
}

static void honda_static_unpack_compact(uint64_t key, HondaStaticFields* fields) {
    uint8_t compact[8];
    honda_static_u64_to_bytes_be(key, compact);

    memset(fields, 0, sizeof(*fields));
    fields->button = compact[0] & 0x0FU;
    fields->serial = ((uint32_t)compact[1] << 20U) | ((uint32_t)compact[2] << 12U) |
                     ((uint32_t)compact[3] << 4U) | ((uint32_t)compact[4] >> 4U);
    fields->counter = ((uint32_t)compact[5] << 16U) | ((uint32_t)compact[6] << 8U) |
                      (uint32_t)compact[7];
    fields->checksum = honda_static_compact_bytes_checksum(compact);
}

static uint64_t honda_static_pack_compact(const HondaStaticFields* fields) {
    uint8_t compact[8];
    compact[0] = fields->button & 0x0FU;
    compact[1] = (uint8_t)(fields->serial >> 20U);
    compact[2] = (uint8_t)(fields->serial >> 12U);
    compact[3] = (uint8_t)(fields->serial >> 4U);
    compact[4] = (uint8_t)(fields->serial << 4U);
    compact[5] = (uint8_t)(fields->counter >> 16U);
    compact[6] = (uint8_t)(fields->counter >> 8U);
    compact[7] = (uint8_t)fields->counter;
    return honda_static_bytes_to_u64_be(compact);
}

static bool honda_static_validate_forward_packet(const uint8_t packet[9], HondaStaticFields* fields) {
    const uint8_t button = honda_static_get_bits(packet, 0, 4);
    const uint32_t serial = honda_static_get_bits_u32(packet, 4, 28);
    const uint32_t counter = honda_static_get_bits_u32(packet, 32, 24);
    const uint8_t checksum = honda_static_get_bits(packet, 56, 8);

    uint8_t checksum_calc = 0U;
    for(size_t i = 0; i < 7; i++) {
        checksum_calc ^= packet[i];
    }
    if(checksum != checksum_calc) return false;
    if(!honda_static_is_valid_button(button)) return false;
    if(!honda_static_is_valid_serial(serial)) return false;

    fields->button = button;
    fields->serial = serial;
    fields->counter = counter;
    fields->checksum = checksum;
    return true;
}

static bool honda_static_validate_reverse_packet(const uint8_t packet[9], HondaStaticFields* fields) {
    uint8_t reversed[9];
    for(size_t i = 0; i < COUNT_OF(reversed); i++) {
        reversed[i] = honda_static_reverse_bits8(packet[i]);
    }

    const uint8_t button = honda_static_get_bits(reversed, 0, 4);
    const uint32_t serial = honda_static_get_bits_u32(reversed, 4, 28);
    const uint32_t counter = honda_static_get_bits_u32(reversed, 32, 24);

    uint8_t checksum = 0U;
    for(size_t i = 0; i < 7; i++) {
        checksum ^= reversed[i];
    }
    // The reverse/fallback path must enforce the stored checksum just like the
    // forward path does; without this gate it accepted ~256x more false
    // positives (button+serial only). (Fix 2026-10-03.)
    if(checksum != honda_static_get_bits(reversed, 56, 8)) return false;
    if(!honda_static_is_valid_button(button)) return false;
    if(!honda_static_is_valid_serial(serial)) return false;

    fields->button = button;
    fields->serial = serial;
    fields->counter = counter;
    fields->checksum = checksum;
    return true;
}

static bool honda_static_manchester_pack_64(
    const uint8_t* symbol_bits,
    uint16_t count,
    uint16_t start_pos,
    bool inverted,
    uint8_t packet[9],
    uint16_t* out_bit_count) {
    memset(packet, 0, 9);

    uint16_t pos = start_pos;
    uint16_t bit_count = 0U;

    while((uint16_t)(pos + 1U) < count) {
        if(bit_count >= HONDA_STATIC_BIT_COUNT) break;

        const uint8_t a = honda_static_symbol_get(symbol_bits, pos);
        const uint8_t b = honda_static_symbol_get(symbol_bits, pos + 1U);

        if(a == b) {
            pos++;
            continue;
        }

        bool bit = false;
        if(inverted) {
            bit = (a == 0U) && (b == 1U);
        } else {
            bit = (a == 1U) && (b == 0U);
        }

        if(bit) {
            packet[bit_count >> 3U] |= (uint8_t)(1U << (((uint8_t)~bit_count) & 0x07U));
        }

        bit_count++;
        pos += 2U;
    }

    if(out_bit_count) {
        *out_bit_count = bit_count;
    }
    return bit_count >= HONDA_STATIC_BIT_COUNT;
}

static bool honda_static_parse_symbols(SubGhzProtocolDecoderHondaStatic* instance, bool inverted) {
    const uint16_t count = instance->symbols_count;
    const uint8_t* symbol_bits = instance->symbols;
    HondaStaticFields decoded;

    uint16_t index = 1U;
    uint16_t transitions = 0U;

    while(index < count) {
        if(honda_static_symbol_get(symbol_bits, index) !=
           honda_static_symbol_get(symbol_bits, index - 1U)) {
            transitions++;
        } else {
            if(transitions > HONDA_STATIC_PREAMBLE_MAX_TRANSITIONS) break;
            transitions = 0U;
        }
        index++;
    }

    if(index >= count) return false;

    while(((uint16_t)(index + 1U) < count) && (honda_static_symbol_get(symbol_bits, index) ==
                                                honda_static_symbol_get(symbol_bits, index + 1U))) {
        index++;
    }

    const uint16_t data_start = index;

    uint8_t packet[9] = {0};
    uint16_t bit_count = 0U;

    if(!honda_static_manchester_pack_64(
           symbol_bits, count, data_start, inverted, packet, &bit_count)) {
        return false;
    }

    if(honda_static_validate_forward_packet(packet, &decoded)) {
        honda_static_decoder_commit(instance, &decoded);
        return true;
    }

    if(inverted) return false;

    if(honda_static_validate_reverse_packet(packet, &decoded)) {
        honda_static_decoder_commit(instance, &decoded);
        return true;
    }

    return false;
}

static void honda_static_decoder_commit(
    SubGhzProtocolDecoderHondaStatic* instance,
    const HondaStaticFields* decoded) {
    instance->generic.data_count_bit = HONDA_STATIC_BIT_COUNT;
    instance->generic.data = honda_static_pack_compact(decoded);
    instance->generic.serial = decoded->serial;
    instance->generic.cnt = decoded->counter;
    instance->generic.btn = decoded->button;

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

const SubGhzProtocolDecoder honda_static_decoder = {
    .alloc = subghz_protocol_decoder_honda_static_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_honda_static_feed,
    .reset = subghz_protocol_decoder_honda_static_reset,
    .get_hash_data = subghz_protocol_decoder_honda_static_get_hash_data,
    .serialize = subghz_protocol_decoder_honda_static_serialize,
    .deserialize = subghz_protocol_decoder_honda_static_deserialize,
    .get_string = subghz_protocol_decoder_honda_static_get_string,
};

static const SubGhzEncoderExternal honda_static_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_honda_static");

const SubGhzProtocol honda_static_protocol = {
    .name = HONDA_STATIC_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 |
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Send,
    .decoder = &honda_static_decoder,
    .encoder = &honda_static_encoder_ext.vtable,
};

void* subghz_protocol_decoder_honda_static_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolDecoderHondaStatic* instance = malloc(sizeof(SubGhzProtocolDecoderHondaStatic));
    furi_check(instance);
    memset(instance, 0, sizeof(*instance));

    instance->base.protocol = &honda_static_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;

    return instance;
}



void subghz_protocol_decoder_honda_static_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHondaStatic* instance = context;
    instance->symbols_count = 0U;
}

void subghz_protocol_decoder_honda_static_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);

    SubGhzProtocolDecoderHondaStatic* instance = context;
    const uint8_t sym = honda_static_level_u8(level);

    if((duration >= HONDA_STATIC_SHORT_BASE_US) &&
       ((duration - HONDA_STATIC_SHORT_BASE_US) <= HONDA_STATIC_SHORT_SPAN_US)) {
        if(instance->symbols_count < HONDA_STATIC_SYMBOL_CAPACITY) {
            honda_static_symbol_set(instance->symbols, instance->symbols_count, sym);
            instance->symbols_count++;
        }
        return;
    }

    if((duration >= HONDA_STATIC_LONG_BASE_US) &&
       ((duration - HONDA_STATIC_LONG_BASE_US) <= HONDA_STATIC_LONG_SPAN_US)) {
        if((uint16_t)(instance->symbols_count + 2U) <= HONDA_STATIC_SYMBOL_CAPACITY) {
            honda_static_symbol_set(instance->symbols, instance->symbols_count, sym);
            instance->symbols_count++;
            honda_static_symbol_set(instance->symbols, instance->symbols_count, sym);
            instance->symbols_count++;
        }
        return;
    }

    const uint16_t sc = instance->symbols_count;

    if(sc >= HONDA_STATIC_MIN_SYMBOLS) {
        if(!honda_static_parse_symbols(instance, true)) {
            honda_static_parse_symbols(instance, false);
        }
    }

    instance->symbols_count = 0U;
}

uint8_t subghz_protocol_decoder_honda_static_get_hash_data(void* context) {
    furi_check(context);

    SubGhzProtocolDecoderHondaStatic* instance = context;
    const uint64_t data = instance->generic.data;

    return (uint8_t)(data ^ (data >> 8U) ^ (data >> 16U) ^ (data >> 24U) ^ (data >> 32U) ^
                     (data >> 40U) ^ (data >> 48U) ^ (data >> 56U));
}

void subghz_protocol_decoder_honda_static_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderHondaStatic* instance = context;
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}


SubGhzProtocolStatus subghz_protocol_decoder_honda_static_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);

    SubGhzProtocolDecoderHondaStatic* instance = context;
    instance->generic.data_count_bit = HONDA_STATIC_BIT_COUNT;
    HondaStaticFields decoded;
    honda_static_unpack_compact(instance->generic.data, &decoded);

    SubGhzProtocolStatus status =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(status != SubGhzProtocolStatusOk) {
        return status;
    }

    if(!flipper_format_write_uint32(flipper_format, "Serial", &decoded.serial, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    uint32_t btn_u32 = decoded.button;
    if(!flipper_format_write_uint32(flipper_format, "Btn", &btn_u32, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    if(!flipper_format_write_uint32(flipper_format, "Cnt", &decoded.counter, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint32_t temp = decoded.checksum;
    if(!flipper_format_write_uint32(flipper_format, "Checksum", &temp, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_honda_static_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);

    SubGhzProtocolDecoderHondaStatic* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, HONDA_STATIC_BIT_COUNT);
    if(status != SubGhzProtocolStatusOk) {
        return status;
    }

    HondaStaticFields decoded;
    honda_static_unpack_compact(instance->generic.data, &decoded);
    uint32_t s = decoded.serial;
    uint32_t b = decoded.button;
    uint32_t c = decoded.counter;

    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Serial", &s, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Btn", &b, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Cnt", &c, 1);

    decoded.serial = s;
    decoded.button = (uint8_t)b;
    decoded.counter = c & 0x00FFFFFFU;

    instance->generic.data = honda_static_pack_compact(&decoded);
    instance->generic.serial = decoded.serial;
    instance->generic.cnt = decoded.counter;
    instance->generic.btn = decoded.button;

    return SubGhzProtocolStatusOk;
}
