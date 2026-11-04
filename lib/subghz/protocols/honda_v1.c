#include "honda_v1.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <string.h>
#include <lib/toolbox/level_duration.h>
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

#define HONDA_V1_BIT_COUNT             68
#define HONDA_V1_TE_SHORT              1000
#define HONDA_V1_TE_LONG               2000
#define HONDA_V1_TE_DELTA              400
#define HONDA_V1_TE_SHORT_MIN          600
#define HONDA_V1_TE_END                3500
#define HONDA_V1_VALID_MAX             0x4B
#define HONDA_V1_NIBBLE_MASK           0x0FU
#define HONDA_V1_SERIAL_MASK           0x0FFFFFFFU
#define HONDA_V1_COUNTER_MASK          0xFFFFU
#define HONDA_V1_LOW32_MASK            0xFFFFFFFFULL
#define HONDA_V1_BUTTON_MAX            10U
#define HONDA_V1_BUTTON_VALID_MASK     0x701U
#define HONDA_V1_BUTTON_FALLBACK_CODE  0x00088888U
#define HONDA_V1_UPLOAD_CAPACITY       2048U
#define HONDA_V1_PREAMBLE_UPLOAD_COUNT 180U
#define HONDA_V1_FRAME_SYMBOLS         80U
#define HONDA_V1_FRAME_START           12U
#define HONDA_V1_FRAME_SYNC_DROP       2U
#define HONDA_V1_FRAME_REPEAT_PER_CRC  2U
#define HONDA_V1_FRAME_BYTES           9U
#define HONDA_V1_FRAME_CRC_INDEX       8U
#define HONDA_V1_FRAME_GAP_US          5000U
#define HONDA_V1_FRAME_GENERATED_MAX   (HONDA_V1_FRAME_SYMBOLS * 2U)
#define HONDA_V1_FRAME_TAIL_MAX        3U
#define HONDA_V1_DECODE_BUFFER_BYTES   12U
#define HONDA_V1_KEY_BYTES             8U
#define HONDA_V1_CRC_FIELD             "Crc"
#define HONDA_V1_KEY_2_FIELD           "Key_2"

typedef enum {
    HondaV1DecoderStepReset = 0,
    HondaV1DecoderStepPreamble,
    HondaV1DecoderStepData,
} HondaV1DecoderStep;

typedef enum {
    HondaV1ButtonUnlock = 0,
    HondaV1ButtonLock = 8,
    HondaV1ButtonTrunk = 9,
    HondaV1ButtonPanic = 10,
} HondaV1Button;

static const char* const honda_v1_button_names[HONDA_V1_BUTTON_MAX + 1U] = {
    [HondaV1ButtonUnlock] = "Unlock",
    [HondaV1ButtonLock] = "Lock",
    [HondaV1ButtonTrunk] = "Trunk",
    [HondaV1ButtonPanic] = "Panic",
};

struct SubGhzProtocolDecoderHondaV1 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockGeneric generic;

    uint8_t step;
    uint8_t preamble_count;
    bool preamble_has_long;
    bool data_pending;
    bool last_level;
    uint8_t bits[HONDA_V1_DECODE_BUFFER_BYTES];
    uint8_t bit_count;
    uint32_t pending;
    bool pending_valid;
    uint8_t k2;
};

struct SubGhzProtocolEncoderHondaV1 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t k2;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderHondaV1);


static uint64_t honda_v1_bytes_to_u64_be(const uint8_t bytes[8]) {
    uint64_t data = 0;
    for(size_t index = 0; index < 8; index++) {
        data = (data << 8U) | bytes[index];
    }
    return data;
}

static bool honda_v1_button_valid(uint8_t b) {
    if(b > HONDA_V1_BUTTON_MAX) return false;
    return ((HONDA_V1_BUTTON_VALID_MASK >> b) & 1U) != 0U;
}

static const char* honda_v1_button_name(uint8_t b) {
    if((b < COUNT_OF(honda_v1_button_names)) && (honda_v1_button_names[b] != NULL)) {
        return honda_v1_button_names[b];
    }
    return "Unknown";
}

static bool honda_v1_duration_is(uint32_t d, uint32_t t) {
    return (d >= t) ? ((d - t) <= HONDA_V1_TE_DELTA) : ((t - d) <= HONDA_V1_TE_DELTA);
}

static uint8_t honda_v1_crc_fold(uint16_t v) {
    const uint8_t lo = (uint8_t)(v & HONDA_V1_NIBBLE_MASK);
    const uint16_t hi = (uint16_t)(v >> 4U);
    int32_t s = (hi & 1U) ? (int32_t)lo : -(int32_t)lo;
    uint8_t out = (uint8_t)((s - (int32_t)hi) & 7);
    out |= (uint8_t)(((v >> 3U) & 1U) << 3U);
    if(((v >> 1U) & 1U) && (((v >> 4U) ^ (v >> 5U)) & 1U)) {
        out ^= 0x04U;
    }
    return (uint8_t)(out & HONDA_V1_NIBBLE_MASK);
}

static uint8_t honda_v1_checksum_base(uint64_t data) {
    const uint8_t a = honda_v1_crc_fold((uint16_t)(data & HONDA_V1_COUNTER_MASK));
    const uint8_t b = honda_v1_crc_fold((uint8_t)((data >> 40U) & 0xFFU));
    return (uint8_t)((a ^ b ^ 1U) & HONDA_V1_NIBBLE_MASK);
}

static uint8_t honda_v1_checksum_alternate(uint8_t checksum) {
    uint8_t mask = 0x09U;
    if((checksum & 1U) == 0U) {
        mask = (checksum & 2U) ? 0x0BU : HONDA_V1_NIBBLE_MASK;
    }
    return (uint8_t)((checksum ^ mask) & HONDA_V1_NIBBLE_MASK);
}

static void honda_v1_checksum_wire_order(uint64_t data, uint8_t* first, uint8_t* second) {
    const uint8_t checksum = honda_v1_checksum_base(data);
    const uint8_t other = honda_v1_checksum_alternate(checksum);
    if((checksum & 0x08U) != 0U) {
        *first = other;
        *second = checksum;
    } else {
        *first = checksum;
        *second = other;
    }
}

static void honda_v1_decode_fields(SubGhzBlockGeneric* generic) {
    const uint32_t low = (uint32_t)(generic->data & HONDA_V1_LOW32_MASK);
    generic->serial = (uint32_t)((generic->data >> 36U) & HONDA_V1_SERIAL_MASK);
    generic->btn = (uint8_t)((low >> 28U) & HONDA_V1_NIBBLE_MASK);
    generic->cnt = low & HONDA_V1_COUNTER_MASK;
    generic->data_count_bit = HONDA_V1_BIT_COUNT;
}

static void honda_v1_state_reset(SubGhzProtocolDecoderHondaV1* instance) {
    instance->step = HondaV1DecoderStepReset;
    instance->preamble_count = 0U;
    instance->preamble_has_long = false;
    instance->data_pending = false;
    instance->last_level = false;
    instance->bit_count = 0U;
    memset(instance->bits, 0, sizeof(instance->bits));
}

static void honda_v1_add_bit(SubGhzProtocolDecoderHondaV1* instance, bool bit) {
    if(instance->bit_count > HONDA_V1_VALID_MAX) return;
    if(bit) {
        instance->bits[instance->bit_count >> 3U] |=
            (uint8_t)(1U << (((uint8_t)~instance->bit_count) & 0x07U));
    }
    instance->bit_count++;
}

static bool honda_v1_commit(SubGhzProtocolDecoderHondaV1* instance) {
    if(instance->bit_count < HONDA_V1_BIT_COUNT) return false;

    uint8_t aligned[sizeof(instance->bits)];
    memcpy(aligned, instance->bits, sizeof(aligned));

    uint8_t shift_count = instance->bit_count - HONDA_V1_BIT_COUNT;
    if(shift_count < 1U) shift_count = 1U;

    for(uint8_t shift = 0U; shift < shift_count; shift++) {
        for(size_t i = 0; i < sizeof(aligned) - 1U; i++) {
            aligned[i] = (uint8_t)((aligned[i] << 1U) | (aligned[i + 1U] >> 7U));
        }
        aligned[sizeof(aligned) - 1U] <<= 1U;
    }

    const uint8_t button = (uint8_t)(aligned[4] >> 4U);
    if(!honda_v1_button_valid(button)) return false;

    instance->generic.data = honda_v1_bytes_to_u64_be(aligned);
    instance->k2 = (uint8_t)(aligned[8] >> 4U);
    honda_v1_decode_fields(&instance->generic);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
    return true;
}

static void honda_v1_symbol(SubGhzProtocolDecoderHondaV1* instance, bool level, uint32_t duration) {
    const bool sh = honda_v1_duration_is(duration, HONDA_V1_TE_SHORT);
    const bool lg = honda_v1_duration_is(duration, HONDA_V1_TE_LONG);

    if(!sh && !lg) {
        if(!level && (duration > HONDA_V1_TE_END) && (instance->step == HondaV1DecoderStepData)) {
            honda_v1_commit(instance);
        }
        honda_v1_state_reset(instance);
        return;
    }

    if(instance->step == HondaV1DecoderStepReset) {
        if(level) {
            instance->step = HondaV1DecoderStepPreamble;
            instance->preamble_count = 1U;
            instance->last_level = level;
        }
        return;
    }

    if(instance->step == HondaV1DecoderStepPreamble) {
        if(lg) {
            if(instance->preamble_count < 0xFFU) instance->preamble_count++;
            instance->preamble_has_long = true;
            instance->last_level = level;
            return;
        }

        if(sh) {
            if(instance->preamble_has_long && (instance->preamble_count > 5U)) {
                instance->step = HondaV1DecoderStepData;
                instance->bit_count = 0U;
                memset(instance->bits, 0, sizeof(instance->bits));
                instance->data_pending = true;
                instance->last_level = level;
                return;
            }
            if(instance->preamble_count < 0xFFU) instance->preamble_count++;
            instance->last_level = level;
            return;
        }

        honda_v1_state_reset(instance);
        return;
    }

    if(sh) {
        if(instance->data_pending) {
            honda_v1_add_bit(instance, level);
            instance->data_pending = false;
            instance->last_level = level;
            return;
        }
        instance->data_pending = true;
        instance->last_level = level;
    } else {
        if(instance->data_pending) {
            honda_v1_add_bit(instance, level);
        } else {
            honda_v1_add_bit(instance, instance->last_level);
        }
        instance->last_level = level;
    }
}

const SubGhzProtocolDecoder subghz_protocol_honda_v1_decoder = {
    .alloc = subghz_protocol_decoder_honda_v1_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_honda_v1_feed,
    .reset = subghz_protocol_decoder_honda_v1_reset,
    .get_hash_data = subghz_protocol_decoder_honda_v1_get_hash_data,
    .serialize = subghz_protocol_decoder_honda_v1_serialize,
    .deserialize = subghz_protocol_decoder_honda_v1_deserialize,
    .get_string = subghz_protocol_decoder_honda_v1_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_honda_v1_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_honda_v1");

const SubGhzProtocol honda_v1_protocol = {
    .name = HONDA_V1_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = &subghz_protocol_honda_v1_decoder,
    .encoder = &subghz_protocol_honda_v1_encoder_ext.vtable,
};

void* subghz_protocol_decoder_honda_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolDecoderHondaV1* instance = malloc(sizeof(SubGhzProtocolDecoderHondaV1));
    furi_check(instance);
    memset(instance, 0, sizeof(*instance));

    instance->base.protocol = &honda_v1_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;

    return instance;
}



void subghz_protocol_decoder_honda_v1_reset(void* context) {
    furi_check(context);

    SubGhzProtocolDecoderHondaV1* instance = context;
    instance->pending = 0U;
    instance->pending_valid = false;
    honda_v1_state_reset(instance);
}

void subghz_protocol_decoder_honda_v1_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);

    SubGhzProtocolDecoderHondaV1* instance = context;

    if(duration < HONDA_V1_TE_DELTA) {
        instance->pending += duration;
        instance->pending_valid = true;
        return;
    }

    if(instance->pending_valid) {
        const uint32_t p = instance->pending;
        if(level) {
            instance->pending = p + duration;
            instance->pending_valid = true;
            return;
        }
        if(p >= HONDA_V1_TE_SHORT_MIN) honda_v1_symbol(instance, true, p);
        instance->pending = 0U;
        instance->pending_valid = false;
    }

    if(level) {
        instance->pending = duration;
        instance->pending_valid = true;
        return;
    }

    honda_v1_symbol(instance, false, duration);
}

uint8_t subghz_protocol_decoder_honda_v1_get_hash_data(void* context) {
    furi_check(context);

    SubGhzProtocolDecoderHondaV1* instance = context;
    const uint64_t data = instance->generic.data;

    return (uint8_t)(data ^ (data >> 8U) ^ (data >> 16U) ^ (data >> 24U) ^ (data >> 32U) ^
                     (data >> 40U) ^ (data >> 48U) ^ (data >> 56U));
}

SubGhzProtocolStatus subghz_protocol_decoder_honda_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);

    SubGhzProtocolDecoderHondaV1* instance = context;
    honda_v1_decode_fields(&instance->generic);

    SubGhzProtocolStatus status =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(status != SubGhzProtocolStatusOk) {
        return status;
    }

    if(!flipper_format_write_uint32(flipper_format, "Serial", &instance->generic.serial, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    uint32_t btn_u32 = instance->generic.btn;
    if(!flipper_format_write_uint32(flipper_format, "Btn", &btn_u32, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    if(!flipper_format_write_uint32(flipper_format, "Cnt", &instance->generic.cnt, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint32_t crc = instance->k2 & HONDA_V1_NIBBLE_MASK;
    if(!flipper_format_write_uint32(flipper_format, HONDA_V1_CRC_FIELD, &crc, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    if(!flipper_format_write_uint32(flipper_format, HONDA_V1_KEY_2_FIELD, &crc, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    FuriString* display = furi_string_alloc();
    furi_string_printf(
        display, "%s - %s", instance->generic.protocol_name, honda_v1_button_name(instance->generic.btn));
    status = SubGhzProtocolStatusOk;
    if(!flipper_format_write_string_cstr(flipper_format, "Disp", furi_string_get_cstr(display))) {
        status = SubGhzProtocolStatusErrorParserOthers;
    }
    furi_string_free(display);
    return status;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_honda_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);

    SubGhzProtocolDecoderHondaV1* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, HONDA_V1_BIT_COUNT);
    if(status != SubGhzProtocolStatusOk) {
        return status;
    }

    flipper_format_rewind(flipper_format);
    uint32_t crc = 0U;
    bool crc_found = flipper_format_read_uint32(flipper_format, HONDA_V1_KEY_2_FIELD, &crc, 1);
    if(!crc_found) {
        flipper_format_rewind(flipper_format);
        crc_found = flipper_format_read_uint32(flipper_format, HONDA_V1_CRC_FIELD, &crc, 1);
    }
    if(crc_found) {
        instance->k2 = (uint8_t)(crc & HONDA_V1_NIBBLE_MASK);
    } else {
        uint8_t first = 0U;
        uint8_t second = 0U;
        honda_v1_checksum_wire_order(instance->generic.data, &first, &second);
        instance->k2 = first;
    }

    instance->generic.protocol_name = instance->base.protocol->name;
    honda_v1_decode_fields(&instance->generic);

    return SubGhzProtocolStatusOk;
}

void subghz_protocol_decoder_honda_v1_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderHondaV1* instance = context;
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}

