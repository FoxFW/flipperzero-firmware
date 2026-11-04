#include "ford_v2.h"
#include <lib/subghz/blocks/custom_btn_i.h>
#include <furi.h>
#include <string.h>
#include "common.h"
#include "../encode_plugin.h"

#define FORD_V2_TE_SHORT               200U
#define FORD_V2_TE_LONG                400U
#define FORD_V2_TE_DELTA               260U
#define FORD_V2_INTER_BURST_GAP_US     15000U
#define FORD_V2_PREAMBLE_MIN           64U
#define FORD_V2_DATA_BITS              104U
#define FORD_V2_DATA_BYTES             13U
#define FORD_V2_SYNC_0                 0x7FU
#define FORD_V2_SYNC_1                 0xA7U
#define FORD_V2_ENC_TE_SHORT           240U
#define FORD_V2_ENC_PREAMBLE_PAIRS     70U
#define FORD_V2_ENC_BURST_COUNT        6U
#define FORD_V2_ENC_INTER_BURST_GAP_US 16000U
#define FORD_V2_ENC_SEPARATOR_ELEMS    2U
#define FORD_V2_ENC_PREAMBLE_ELEMS     (FORD_V2_ENC_PREAMBLE_PAIRS * 2U)
#define FORD_V2_ENC_DATA_ELEMS         ((FORD_V2_DATA_BITS - 1U) * 2U)
#define FORD_V2_ENC_BURST_ELEMS \
    (FORD_V2_ENC_PREAMBLE_ELEMS + FORD_V2_ENC_SEPARATOR_ELEMS + FORD_V2_ENC_DATA_ELEMS)
#define FORD_V2_ENC_UPLOAD_ELEMS \
    (FORD_V2_ENC_BURST_COUNT * FORD_V2_ENC_BURST_ELEMS + (FORD_V2_ENC_BURST_COUNT - 1U))
#define FORD_V2_ENC_SYNC_LO_US 476U

#define FORD_V2_SYNC_BITS                  16U
#define FORD_V2_POST_SYNC_DECODE_COUNT_BIT 16U
#define FORD_V2_KEY_BYTE_COUNT             8U
#define FORD_V2_TAIL_RAW_BYTE_COUNT        5U
#define FORD_V2_PREAMBLE_COUNT_MAX         0xFFFFU
#define FORD_V2_ENCODER_DEFAULT_REPEAT     10U

static const uint16_t ford_v2_sync_shift16_inv =
    (uint16_t)(~(((uint16_t)FORD_V2_SYNC_0 << 8) | (uint16_t)FORD_V2_SYNC_1));

static const SubGhzBlockConst subghz_protocol_ford_v2_const = {
    .te_short = FORD_V2_TE_SHORT,
    .te_long = FORD_V2_TE_LONG,
    .te_delta = FORD_V2_TE_DELTA,
    .min_count_bit_for_found = FORD_V2_DATA_BITS,
};

typedef enum {
    FordV2DecoderStepReset = 0,
    FordV2DecoderStepPreamble = 1,
    FordV2DecoderStepSync = 2,
    FordV2DecoderStepData = 3,
} FordV2DecoderStep;

typedef struct SubGhzProtocolDecoderFordV2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint16_t preamble_count;

    uint8_t raw_bytes[FORD_V2_DATA_BYTES];
    uint8_t byte_count;

    uint16_t sync_shift;
    uint8_t sync_bit_count;

    uint64_t extra_data;
    uint16_t counter16;
    uint32_t tail31;
    bool structure_ok;
} SubGhzProtocolDecoderFordV2;

typedef struct SubGhzProtocolEncoderFordV2 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint64_t extra_data;
    uint8_t raw_bytes[FORD_V2_DATA_BYTES];
} 
SubGhzProtocolEncoderFordV2;

static void ford_v2_decoder_manchester_feed_event(
    SubGhzProtocolDecoderFordV2* instance,
    ManchesterEvent event);

static void ford_v2_decoder_reset_state(SubGhzProtocolDecoderFordV2* instance) {
    instance->decoder.parser_step = FordV2DecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->decoder.te_last = 0;

    instance->byte_count = 0;
    instance->sync_shift = 0;
    instance->sync_bit_count = 0;
    instance->preamble_count = 0;
    instance->counter16 = 0;
    instance->tail31 = 0;
    instance->structure_ok = false;

    memset(instance->raw_bytes, 0, sizeof(instance->raw_bytes));

    manchester_advance(
        instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);
}

static bool ford_v2_duration_is_short(uint32_t duration) {
    return DURATION_DIFF(duration, FORD_V2_TE_SHORT) < (int32_t)FORD_V2_TE_DELTA;
}

static bool ford_v2_duration_is_long(uint32_t duration) {
    return DURATION_DIFF(duration, FORD_V2_TE_LONG) < (int32_t)FORD_V2_TE_DELTA;
}

static bool ford_v2_button_is_valid(uint8_t btn) {
    switch(btn) {
    case 0x10:
    case 0x11:
    case 0x13:
    case 0x14:
    case 0x15:
        return true;
    default:
        return false;
    }
}

static void ford_v2_decoder_extract_from_raw(SubGhzProtocolDecoderFordV2* instance) {
    const uint8_t* k = instance->raw_bytes;

    instance->generic.serial = ((uint32_t)k[2] << 24) | ((uint32_t)k[3] << 16) |
                               ((uint32_t)k[4] << 8) | (uint32_t)k[5];

    instance->generic.btn = k[6];

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->generic.btn);
    }
    subghz_custom_btn_set_max(5);

    instance->counter16 = (uint16_t)((((uint16_t)(k[7] & 0x7FU)) << 9) | (((uint16_t)k[8]) << 1) |
                                     ((uint16_t)(k[9] >> 7)));

    instance->generic.cnt = instance->counter16;

    instance->tail31 = (((uint32_t)(k[9] & 0x7FU)) << 24) | ((uint32_t)k[10] << 16) |
                       ((uint32_t)k[11] << 8) | (uint32_t)k[12];

    instance->structure_ok = true;

    if(k[0] != FORD_V2_SYNC_0) instance->structure_ok = false;
    if(k[1] != FORD_V2_SYNC_1) instance->structure_ok = false;
    if(!ford_v2_button_is_valid(k[6])) instance->structure_ok = false;

    if((k[7] & 0x7FU) != (uint8_t)((instance->counter16 >> 9) & 0x7FU)) {
        instance->structure_ok = false;
    }

    if(k[8] != (uint8_t)((instance->counter16 >> 1) & 0xFFU)) {
        instance->structure_ok = false;
    }

    if(((k[9] >> 7) & 1U) != (uint8_t)(instance->counter16 & 1U)) {
        instance->structure_ok = false;
    }

    instance->generic.data = 0;
    for(uint8_t i = 0; i < FORD_V2_KEY_BYTE_COUNT; i++) {
        instance->generic.data = (instance->generic.data << 8) | (uint64_t)k[i];
    }

    instance->generic.data_count_bit = FORD_V2_DATA_BITS;

    instance->extra_data = 0;
    for(uint8_t i = 0; i < FORD_V2_TAIL_RAW_BYTE_COUNT; i++) {
        instance->extra_data = (instance->extra_data << 8) | (uint64_t)k[8U + i];
    }
}

static bool ford_v2_decoder_commit_frame(SubGhzProtocolDecoderFordV2* instance) {
    if(instance->raw_bytes[0] != FORD_V2_SYNC_0 || instance->raw_bytes[1] != FORD_V2_SYNC_1) {
        return false;
    }

    ford_v2_decoder_extract_from_raw(instance);

    if(!instance->structure_ok) {
        return false;
    }

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }

    return true;
}

static void ford_v2_decoder_sync_enter_data(SubGhzProtocolDecoderFordV2* instance) {
    memset(instance->raw_bytes, 0, sizeof(instance->raw_bytes));
    instance->raw_bytes[0] = FORD_V2_SYNC_0;
    instance->raw_bytes[1] = FORD_V2_SYNC_1;
    instance->byte_count = 2U;
    instance->decoder.parser_step = FordV2DecoderStepData;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = FORD_V2_POST_SYNC_DECODE_COUNT_BIT;
}

static bool
    ford_v2_decoder_sync_feed_event(SubGhzProtocolDecoderFordV2* instance, ManchesterEvent event) {
    bool data_bit;

    if(!manchester_advance(
           instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
        return false;
    }

    instance->sync_shift = (uint16_t)((instance->sync_shift << 1) | (data_bit ? 1U : 0U));
    if(instance->sync_bit_count < FORD_V2_SYNC_BITS) {
        instance->sync_bit_count++;
    }

    return instance->sync_bit_count >= FORD_V2_SYNC_BITS &&
           instance->sync_shift == ford_v2_sync_shift16_inv;
}

static void ford_v2_decoder_manchester_feed_event(
    SubGhzProtocolDecoderFordV2* instance,
    ManchesterEvent event) {
    bool data_bit;

    if(instance->decoder.parser_step == FordV2DecoderStepSync) {
        if(ford_v2_decoder_sync_feed_event(instance, event)) {
            ford_v2_decoder_sync_enter_data(instance);
        }
        return;
    }

    if(!manchester_advance(
           instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
        return;
    }

    if(instance->decoder.parser_step != FordV2DecoderStepData) {
        return;
    }

    data_bit = !data_bit;

    instance->decoder.decode_data = (instance->decoder.decode_data << 1) | (data_bit ? 1U : 0U);
    instance->decoder.decode_count_bit++;

    if((instance->decoder.decode_count_bit & 7U) == 0U) {
        uint8_t byte_val = (uint8_t)(instance->decoder.decode_data & 0xFFU);

        if(instance->byte_count < FORD_V2_DATA_BYTES) {
            instance->raw_bytes[instance->byte_count] = byte_val;
            instance->byte_count++;
        }

        instance->decoder.decode_data = 0;

        if(instance->byte_count == FORD_V2_DATA_BYTES) {
            (void)ford_v2_decoder_commit_frame(instance);
            ford_v2_decoder_reset_state(instance);
        }
    }
}

static bool ford_v2_decoder_manchester_feed_pulse(
    SubGhzProtocolDecoderFordV2* instance,
    bool level,
    uint32_t duration) {
    if(ford_v2_duration_is_short(duration)) {
        ManchesterEvent ev = level ? ManchesterEventShortHigh : ManchesterEventShortLow;
        ford_v2_decoder_manchester_feed_event(instance, ev);
        return true;
    }

    if(ford_v2_duration_is_long(duration)) {
        ManchesterEvent ev = level ? ManchesterEventLongHigh : ManchesterEventLongLow;
        ford_v2_decoder_manchester_feed_event(instance, ev);
        return true;
    }

    return false;
}

static void ford_v2_decoder_enter_sync_from_preamble(
    SubGhzProtocolDecoderFordV2* instance,
    bool level,
    uint32_t duration) {
    instance->decoder.parser_step = FordV2DecoderStepSync;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->byte_count = 0;
    instance->sync_shift = 0;
    instance->sync_bit_count = 0;
    memset(instance->raw_bytes, 0, sizeof(instance->raw_bytes));

    manchester_advance(
        instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);

    if(ford_v2_duration_is_short(duration)) {
        ManchesterEvent ev = level ? ManchesterEventShortHigh : ManchesterEventShortLow;
        if(ev == ManchesterEventShortLow || ev == ManchesterEventLongLow) {
            instance->manchester_state = ManchesterStateMid0;
        }
        ford_v2_decoder_manchester_feed_event(instance, ev);
    } else if(ford_v2_duration_is_long(duration)) {
        ManchesterEvent ev = level ? ManchesterEventLongHigh : ManchesterEventLongLow;
        if(ev == ManchesterEventShortLow || ev == ManchesterEventLongLow) {
            instance->manchester_state = ManchesterStateMid0;
        }
        ford_v2_decoder_manchester_feed_event(instance, ev);
    } else {
        ford_v2_decoder_reset_state(instance);
    }
}

static void ford_v2_decoder_rebuild_raw_buffer(SubGhzProtocolDecoderFordV2* instance) {
    for(uint8_t i = 0; i < FORD_V2_KEY_BYTE_COUNT; i++) {
        instance->raw_bytes[i] = (uint8_t)(instance->generic.data >> (56U - i * 8U));
    }

    for(uint8_t i = 0; i < FORD_V2_TAIL_RAW_BYTE_COUNT; i++) {
        instance->raw_bytes[8U + i] = (uint8_t)(instance->extra_data >> (32U - i * 8U));
    }
}

void* subghz_protocol_decoder_ford_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolDecoderFordV2* instance = calloc(1, sizeof(SubGhzProtocolDecoderFordV2));
    furi_check(instance);

    instance->base.protocol = &ford_protocol_v2;
    instance->generic.protocol_name = instance->base.protocol->name;

    return instance;
}



void subghz_protocol_decoder_ford_v2_reset(void* context) {
    furi_check(context);
    ford_v2_decoder_reset_state((SubGhzProtocolDecoderFordV2*)context);
}

void subghz_protocol_decoder_ford_v2_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);

    SubGhzProtocolDecoderFordV2* instance = context;

    switch(instance->decoder.parser_step) {
    case FordV2DecoderStepReset:
        if(ford_v2_duration_is_short(duration)) {
            instance->preamble_count = 1U;
            instance->decoder.parser_step = FordV2DecoderStepPreamble;
        }
        break;

    case FordV2DecoderStepPreamble:
        if(ford_v2_duration_is_short(duration)) {
            if(instance->preamble_count < FORD_V2_PREAMBLE_COUNT_MAX) {
                instance->preamble_count++;
            }
        } else if(!level && ford_v2_duration_is_long(duration)) {
            if(instance->preamble_count >= FORD_V2_PREAMBLE_MIN) {
                ford_v2_decoder_enter_sync_from_preamble(instance, level, duration);
            } else {
                ford_v2_decoder_reset_state(instance);
            }
        } else {
            ford_v2_decoder_reset_state(instance);
        }
        break;

    case FordV2DecoderStepSync:
    case FordV2DecoderStepData:
        if(ford_v2_decoder_manchester_feed_pulse(instance, level, duration)) {
        } else {
            if(instance->decoder.parser_step == FordV2DecoderStepSync &&
               duration >= FORD_V2_INTER_BURST_GAP_US) {
                ford_v2_decoder_reset_state(instance);
                break;
            }
            if(instance->decoder.parser_step == FordV2DecoderStepSync) {
                ford_v2_decoder_reset_state(instance);
                break;
            }
            if(instance->decoder.parser_step == FordV2DecoderStepData) {
                if(duration >= FORD_V2_INTER_BURST_GAP_US) {
                    ford_v2_decoder_reset_state(instance);
                    break;
                }
            }
            ford_v2_decoder_reset_state(instance);
        }

        instance->decoder.te_last = duration;
        break;
    }
}

uint8_t subghz_protocol_decoder_ford_v2_get_hash_data(void* context) {
    furi_check(context);

    SubGhzProtocolDecoderFordV2* instance = context;
    const uint8_t* k = instance->raw_bytes;

    const uint16_t cnt = (uint16_t)((((uint16_t)(k[7] & 0x7FU)) << 9) | (((uint16_t)k[8]) << 1) |
                                    ((uint16_t)(k[9] >> 7)));
    const uint32_t tail = (((uint32_t)(k[9] & 0x7FU)) << 24) | ((uint32_t)k[10] << 16) |
                          ((uint32_t)k[11] << 8) | (uint32_t)k[12];

    uint32_t mix = ((uint32_t)k[2] << 24) | ((uint32_t)k[3] << 16) | ((uint32_t)k[4] << 8) |
                   (uint32_t)k[5];
    mix ^= (uint32_t)k[6] << 16;
    mix ^= (uint32_t)cnt << 8;
    mix ^= tail;

    return (uint8_t)((mix >> 0) ^ (mix >> 8) ^ (mix >> 16) ^ (mix >> 24) ^ (uint8_t)(cnt >> 8) ^
                     (uint8_t)(tail >> 16));
}

SubGhzProtocolStatus subghz_protocol_decoder_ford_v2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);

    SubGhzProtocolDecoderFordV2* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_uint32(
            flipper_format, "Serial", &instance->generic.serial, 1);

        uint32_t btn = instance->generic.btn;
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_uint32(flipper_format, "Btn", &btn, 1);

        uint32_t cnt = instance->generic.cnt;
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &cnt, 1);

        uint32_t tail31 = instance->tail31;
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_uint32(flipper_format, "Tail31", &tail31, 1);

        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_hex(flipper_format, "TailRaw", &instance->raw_bytes[8], 5);
    }

    return ret;
}

static void ford_v2_decoder_read_tail_raw_if_present(
    SubGhzProtocolDecoderFordV2* instance,
    FlipperFormat* flipper_format) {
    uint8_t tail_raw[FORD_V2_TAIL_RAW_BYTE_COUNT] = {0};
    if(flipper_format_read_hex(flipper_format, "TailRaw", tail_raw, sizeof(tail_raw))) {
        instance->extra_data = 0;
        for(uint8_t i = 0; i < FORD_V2_TAIL_RAW_BYTE_COUNT; i++) {
            instance->extra_data = (instance->extra_data << 8) | (uint64_t)tail_raw[i];
        }
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_ford_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);

    SubGhzProtocolDecoderFordV2* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_ford_v2_const.min_count_bit_for_found);

    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    if(instance->generic.data_count_bit != FORD_V2_DATA_BITS) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }

    flipper_format_rewind(flipper_format);
    ford_v2_decoder_read_tail_raw_if_present(instance, flipper_format);

    ford_v2_decoder_rebuild_raw_buffer(instance);
    ford_v2_decoder_extract_from_raw(instance);

    if(!instance->structure_ok) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    return ret;
}

const SubGhzProtocolDecoder subghz_protocol_ford_v2_decoder = {
    .alloc = subghz_protocol_decoder_ford_v2_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_ford_v2_feed,
    .reset = subghz_protocol_decoder_ford_v2_reset,
    .get_hash_data = subghz_protocol_decoder_ford_v2_get_hash_data,
    .serialize = subghz_protocol_decoder_ford_v2_serialize,
    .deserialize = subghz_protocol_decoder_ford_v2_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_ford_v2_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_ford_v2");

const SubGhzProtocol ford_protocol_v2 = {
    .name = FORD_PROTOCOL_V2_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_ford_v2_decoder,
    .encoder = &subghz_protocol_ford_v2_encoder_ext.vtable,
};

/* Layout asserts placed at EOF: these structs typedef the bare name
 * later in this file, so the assert must follow all type definitions. */
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderFordV2);
