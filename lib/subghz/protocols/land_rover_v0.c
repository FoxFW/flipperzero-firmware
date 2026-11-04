#include "land_rover_v0.h"

#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

#define TAG "LandRoverV0"

static const SubGhzBlockConst subghz_protocol_land_rover_v0_const = {
    .te_short                = 250,
    .te_long                 = 500,
    .te_delta                = 100,
    .min_count_bit_for_found = 81,
};

#define LAND_ROVER_V0_PREAMBLE_PAIRS     319U
#define LAND_ROVER_V0_MIN_PREAMBLE_PAIRS 64U
#define LAND_ROVER_V0_SYNC_US            750U
#define LAND_ROVER_V0_SYNC_DELTA_US      120U
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

typedef struct SubGhzProtocolDecoderLandRoverV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder        decoder;
    SubGhzBlockGeneric        generic;

    uint16_t preamble_count;
    uint8_t  raw[10];
    uint8_t  bit_count;
    bool     extra_bit;
    bool     previous_bit;
    bool     boundary_pad_skipped;
    bool     pending_short;

    uint64_t key;
    uint16_t tail;
    uint32_t command_signature;
    uint32_t serial;
    uint32_t count;
    uint8_t  button;
    uint8_t  check;
    bool     check_ok;
    bool     tail_ok;
} SubGhzProtocolDecoderLandRoverV0;

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

typedef enum {
    LandRoverV0DecoderStepReset = 0,
    LandRoverV0DecoderStepPreambleLow,
    LandRoverV0DecoderStepPreambleHigh,
    LandRoverV0DecoderStepSyncLow,
    LandRoverV0DecoderStepData,
} LandRoverV0DecoderStep;

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

static inline bool lr_is_short(uint32_t duration) {
    return DURATION_DIFF(duration, subghz_protocol_land_rover_v0_const.te_short) <
           subghz_protocol_land_rover_v0_const.te_delta;
}

static inline bool lr_is_long(uint32_t duration) {
    return DURATION_DIFF(duration, subghz_protocol_land_rover_v0_const.te_long) <
           subghz_protocol_land_rover_v0_const.te_delta;
}

static void lr_ff_write_u32(FlipperFormat* ff, const char* key, uint32_t val) {
    flipper_format_insert_or_update_uint32(ff, key, &val, 1);
}

static bool lr_ff_read_u32(FlipperFormat* ff, const char* key, uint32_t* out) {
    return flipper_format_read_uint32(ff, key, out, 1);
}

static uint8_t     land_rover_v0_calculate_check(uint32_t count);
static bool        land_rover_v0_calculate_tail_msb(uint32_t count);
static uint16_t    land_rover_v0_calculate_tail(uint32_t count);
static void        land_rover_v0_parse_key_fields(
           uint64_t  key,
           uint32_t* signature,
           uint32_t* serial,
           uint32_t* count,
           uint8_t*  button,
           uint8_t*  check);
static bool land_rover_v0_validate_frame(
    uint64_t key,
    uint16_t tail,
    bool     extra_bit,
    bool*    check_ok,
    bool*    tail_ok);
static bool land_rover_v0_add_decoded_bit(
    SubGhzProtocolDecoderLandRoverV0* instance,
    bool                              bit);
static bool land_rover_v0_process_transition(
    SubGhzProtocolDecoderLandRoverV0* instance,
    bool                              level,
    uint32_t                          duration);
static bool land_rover_v0_finish_frame(SubGhzProtocolDecoderLandRoverV0* instance);


const SubGhzProtocolDecoder subghz_protocol_land_rover_v0_decoder = {
    .alloc         = subghz_protocol_decoder_land_rover_v0_alloc,
    .free          = subghz_protocol_decoder_common_free,
    .feed          = subghz_protocol_decoder_land_rover_v0_feed,
    .reset         = subghz_protocol_decoder_land_rover_v0_reset,
    .get_hash_data = subghz_protocol_decoder_land_rover_v0_get_hash_data,
    .serialize     = subghz_protocol_decoder_land_rover_v0_serialize,
    .deserialize   = subghz_protocol_decoder_land_rover_v0_deserialize,
    .get_string    = subghz_protocol_decoder_land_rover_v0_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_land_rover_v0_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_land_rover_v0");

const SubGhzProtocol subghz_protocol_land_rover_v0 = {
    .name    = LAND_ROVER_PROTOCOL_V0_NAME,
    .type    = SubGhzProtocolTypeDynamic,
    .flag    = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
               SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
               SubGhzProtocolFlag_Save      | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_land_rover_v0_decoder,
    .encoder = &subghz_protocol_land_rover_v0_encoder_ext.vtable,
};

static bool land_rover_v0_is_sync(uint32_t duration) {
    return DURATION_DIFF(duration, LAND_ROVER_V0_SYNC_US) < LAND_ROVER_V0_SYNC_DELTA_US;
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

static bool land_rover_v0_validate_frame(
    uint64_t key,
    uint16_t tail,
    bool     extra_bit,
    bool*    check_ok,
    bool*    tail_ok) {

    uint8_t key_bytes[8];
    lr_u64_to_bytes_be(key, key_bytes);

    const uint32_t count          = ((uint32_t)key_bytes[6] << 1) | ((key_bytes[7] >> 7) & 1U);
    const uint8_t  expected_check = land_rover_v0_calculate_check(count);
    const uint16_t expected_tail  = land_rover_v0_calculate_tail(count);

    const bool local_check_ok =
        ((key_bytes[7] & 0x78U) == 0U) && ((key_bytes[7] & 0x07U) == expected_check);
    const bool local_tail_ok = (tail == expected_tail) && extra_bit;

    if(check_ok) *check_ok = local_check_ok;
    if(tail_ok)  *tail_ok  = local_tail_ok;

    return local_check_ok && local_tail_ok;
}

static bool land_rover_v0_add_decoded_bit(
    SubGhzProtocolDecoderLandRoverV0* instance,
    bool                              bit) {

    if(instance->bit_count < 80U) {
        const uint8_t byte_index = instance->bit_count / 8U;
        const uint8_t bit_index  = 7U - (instance->bit_count % 8U);
        if(bit) instance->raw[byte_index] |= (uint8_t)(1U << bit_index);
    } else if(instance->bit_count == 80U) {
        instance->extra_bit = bit;
    } else {
        return false;
    }

    instance->bit_count++;
    return true;
}

static bool land_rover_v0_finish_frame(SubGhzProtocolDecoderLandRoverV0* instance) {
    const uint64_t key  = lr_bytes_to_u64_be(instance->raw);
    const uint16_t tail =
        ((uint16_t)instance->raw[8] << 8) | instance->raw[9];

    if(!land_rover_v0_validate_frame(
           key, tail, instance->extra_bit,
           &instance->check_ok, &instance->tail_ok)) {
        return false;
    }

    instance->key  = key;
    instance->tail = tail;

    land_rover_v0_parse_key_fields(
        key,
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

    return true;
}

static bool land_rover_v0_process_transition(
    SubGhzProtocolDecoderLandRoverV0* instance,
    bool                              level,
    uint32_t                          duration) {

    if(!instance->boundary_pad_skipped) {
        if(level && lr_is_short(duration)) {
            instance->boundary_pad_skipped = true;
            return true;
        }
        instance->boundary_pad_skipped = true;
    }

    if(instance->pending_short) {
        if(!instance->previous_bit && !level && lr_is_short(duration)) {
            instance->pending_short = false;
            return land_rover_v0_add_decoded_bit(instance, false);
        } else if(instance->previous_bit && level && lr_is_short(duration)) {
            instance->pending_short = false;
            return land_rover_v0_add_decoded_bit(instance, true);
        }
        return false;
    }

    if(!instance->previous_bit) {
        if(level && lr_is_long(duration)) {
            instance->previous_bit = true;
            return land_rover_v0_add_decoded_bit(instance, true);
        } else if(level && lr_is_short(duration)) {
            instance->pending_short = true;
            return true;
        }
        return false;
    }

    if(!level && lr_is_long(duration)) {
        instance->previous_bit = false;
        return land_rover_v0_add_decoded_bit(instance, false);
    } else if(!level && lr_is_short(duration)) {
        instance->pending_short = true;
        return true;
    }

    return false;
}

void* subghz_protocol_decoder_land_rover_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderLandRoverV0* instance =
        calloc(1, sizeof(SubGhzProtocolDecoderLandRoverV0));
    furi_check(instance);
    instance->base.protocol         = &subghz_protocol_land_rover_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_land_rover_v0_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderLandRoverV0* instance = context;

    instance->decoder.parser_step     = LandRoverV0DecoderStepReset;
    instance->decoder.te_last         = 0;
    instance->preamble_count          = 0;
    memset(instance->raw, 0, sizeof(instance->raw));
    instance->bit_count               = 0;
    instance->extra_bit               = false;
    instance->previous_bit            = true;
    instance->boundary_pad_skipped    = false;
    instance->pending_short           = false;
}

void subghz_protocol_decoder_land_rover_v0_feed(
    void*    context,
    bool     level,
    uint32_t duration) {

    furi_check(context);
    SubGhzProtocolDecoderLandRoverV0* instance = context;

    switch(instance->decoder.parser_step) {
    case LandRoverV0DecoderStepReset:
        if(level && lr_is_short(duration)) {
            instance->preamble_count      = 0;
            instance->decoder.parser_step = LandRoverV0DecoderStepPreambleLow;
        }
        break;

    case LandRoverV0DecoderStepPreambleLow:
        if(!level && lr_is_short(duration)) {
            instance->preamble_count++;
            instance->decoder.parser_step = LandRoverV0DecoderStepPreambleHigh;
        } else {
            instance->decoder.parser_step = LandRoverV0DecoderStepReset;
        }
        break;

    case LandRoverV0DecoderStepPreambleHigh:
        if(level && lr_is_short(duration)) {
            instance->decoder.parser_step = LandRoverV0DecoderStepPreambleLow;
        } else if(level && land_rover_v0_is_sync(duration) &&
                  instance->preamble_count >= LAND_ROVER_V0_MIN_PREAMBLE_PAIRS) {
            instance->decoder.parser_step = LandRoverV0DecoderStepSyncLow;
        } else {
            instance->decoder.parser_step = LandRoverV0DecoderStepReset;
        }
        break;

    case LandRoverV0DecoderStepSyncLow:
        if(!level && land_rover_v0_is_sync(duration)) {
            memset(instance->raw, 0, sizeof(instance->raw));
            instance->bit_count            = 0;
            instance->extra_bit            = false;
            instance->previous_bit         = true;
            instance->boundary_pad_skipped = false;
            instance->pending_short        = false;
            land_rover_v0_add_decoded_bit(instance, true);
            instance->decoder.parser_step  = LandRoverV0DecoderStepData;
        } else {
            instance->decoder.parser_step = LandRoverV0DecoderStepReset;
        }
        break;

    case LandRoverV0DecoderStepData:
        if(!land_rover_v0_process_transition(instance, level, duration)) {
            instance->decoder.parser_step = LandRoverV0DecoderStepReset;
            break;
        }
        if(instance->bit_count ==
           subghz_protocol_land_rover_v0_const.min_count_bit_for_found) {
            if(land_rover_v0_finish_frame(instance) && instance->base.callback) {
                instance->base.callback(&instance->base, instance->base.context);
            }
            instance->decoder.parser_step = LandRoverV0DecoderStepReset;
        }
        break;
    }

    instance->decoder.te_last = duration;
}

uint8_t subghz_protocol_decoder_land_rover_v0_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderLandRoverV0* instance = context;

    SubGhzBlockDecoder decoder = {
        .decode_data      = instance->key,
        .decode_count_bit = 64,
    };
    uint8_t hash = subghz_protocol_blocks_get_hash_data(&decoder, 9);
    hash ^= (uint8_t)(instance->tail >> 8);
    hash ^= (uint8_t)instance->tail;
    hash ^= instance->extra_bit ? 1U : 0U;
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_land_rover_v0_serialize(
    void*              context,
    FlipperFormat*     flipper_format,
    SubGhzRadioPreset* preset) {

    furi_check(context);
    SubGhzProtocolDecoderLandRoverV0* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        uint8_t key_bytes[8];
        lr_u64_to_bytes_be(instance->key, key_bytes);

        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_hex(
            flipper_format, LR_FF_KEY, key_bytes, sizeof(key_bytes));
        lr_ff_write_u32(flipper_format, LR_FF_SERIAL,             instance->serial);
        lr_ff_write_u32(flipper_format, LR_FF_BTN,                instance->button);
        lr_ff_write_u32(flipper_format, LAND_ROVER_V0_FF_BTNSIG,  instance->command_signature);
        lr_ff_write_u32(flipper_format, LR_FF_CNT,                instance->count);
        lr_ff_write_u32(flipper_format, LAND_ROVER_V0_FF_CHECK,   instance->check);
        lr_ff_write_u32(flipper_format, LAND_ROVER_V0_FF_TAIL,    instance->tail);
        lr_ff_write_u32(flipper_format, LAND_ROVER_V0_FF_EXTRA_BIT,
                        instance->extra_bit ? 1U : 0U);
    }

    return ret;
}

SubGhzProtocolStatus subghz_protocol_decoder_land_rover_v0_deserialize(
    void*          context,
    FlipperFormat* flipper_format) {

    furi_check(context);
    SubGhzProtocolDecoderLandRoverV0* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_land_rover_v0_const.min_count_bit_for_found);

    if(ret == SubGhzProtocolStatusOk) {
        uint8_t key_bytes[8] = {0};
        bool    have_key     = false;

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_hex(
               flipper_format, LR_FF_KEY, key_bytes, sizeof(key_bytes))) {
            instance->key = lr_bytes_to_u64_be(key_bytes);
            have_key      = true;
        }

        if(!have_key) {
            instance->key = instance->generic.data;
            lr_u64_to_bytes_be(instance->key, key_bytes);
        }

        uint32_t temp = 0;

        flipper_format_rewind(flipper_format);
        if(lr_ff_read_u32(flipper_format, LAND_ROVER_V0_FF_TAIL, &temp)) {
            instance->tail = (uint16_t)(temp & 0xFFFFU);
        } else {
            const uint32_t count =
                ((uint32_t)key_bytes[6] << 1) | ((key_bytes[7] >> 7) & 1U);
            instance->tail = land_rover_v0_calculate_tail(count);
        }

        flipper_format_rewind(flipper_format);
        if(lr_ff_read_u32(flipper_format, LAND_ROVER_V0_FF_EXTRA_BIT, &temp)) {
            instance->extra_bit = (temp & 1U) != 0;
        } else {
            instance->extra_bit = true;
        }

        land_rover_v0_validate_frame(
            instance->key, instance->tail, instance->extra_bit,
            &instance->check_ok, &instance->tail_ok);

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

        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(instance->generic.btn);
        }
        subghz_custom_btn_set_max(2);
    }

    return ret;
}

void subghz_protocol_decoder_land_rover_v0_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderLandRoverV0* instance = context;
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}


/* Layout asserts placed at EOF: these structs typedef the bare name
 * later in this file, so the assert must follow all type definitions. */
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderLandRoverV0);
