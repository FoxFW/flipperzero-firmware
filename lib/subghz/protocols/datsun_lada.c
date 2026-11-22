#include "datsun_lada.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

#define TAG "DatsunLada"

#define DL_FRAME_BYTES 11
#define DL_FRAME_BITS  88
#define DL_GLITCH_US   50U
#define DL_PREAMBLE_MIN_SHORTS 64U

typedef enum {
    DLStepReset = 0,
    DLStepData,
} DLStep;

struct SubGhzProtocolDecoderDatsunLada {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t step;
    uint32_t te_last;
    uint16_t decode_count_bit;
    uint8_t receive_data[DL_FRAME_BYTES];
    bool marker;
    bool glue;
    uint16_t short_run;
    uint16_t sync_run;

    uint8_t frame[DL_FRAME_BYTES];
    bool crc_valid;
    uint16_t counter;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderDatsunLada);

static const SubGhzBlockConst subghz_protocol_datsun_lada_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 110,
    .min_count_bit_for_found = DL_FRAME_BITS,
};

static inline uint8_t dl_bit(uint8_t v, uint8_t i) {
    return (v >> i) & 1;
}
static inline uint8_t dl_mask(uint8_t v) {
    return (uint8_t)(1u << (v & 7));
}

static uint8_t dl_predicate(const uint8_t* r) {
    uint8_t acc, flag;
    acc = (uint8_t)(dl_bit(r[10], 5) | (dl_bit(r[10], 4) << 1) | (dl_bit(r[10], 2) << 2));
    flag = (dl_mask(acc) & (dl_bit(r[10], 1) ? 0x2C : 0x79)) ? 1u : 0u;
    acc = (uint8_t)(dl_bit(r[9], 7) | (dl_bit(r[9], 3) << 1) | (dl_bit(r[9], 1) << 2));
    if(dl_mask(acc) & (dl_bit(r[9], 0) ? 0x66 : 0x71)) flag |= 2;
    acc = (uint8_t)(dl_bit(r[8], 6) | (dl_bit(r[8], 2) << 1) | (dl_bit(r[8], 0) << 2));
    if(dl_mask(acc) & (dl_bit(r[7], 5) ? 0x66 : 0x71)) flag |= 4;
    acc = (uint8_t)(dl_bit(r[7], 3) | (dl_bit(r[7], 2) << 1) | (dl_bit(r[7], 0) << 2));
    if(dl_mask(acc) & (dl_bit(r[6], 6) ? 0x66 : 0x71)) flag |= 8;
    acc = (uint8_t)(dl_bit(r[6], 5) | (dl_bit(r[5], 4) << 1) | (dl_bit(r[5], 3) << 2));
    uint8_t slot;
    if(dl_mask(acc) & (dl_bit(r[5], 1) ? 0x2C : 0x79))
        slot = dl_bit(flag, 3) ? 0x79 : 0x07;
    else
        slot = dl_bit(flag, 3) ? 0x28 : 0x7B;
    return (dl_mask(flag) & slot) ? 1u : 0u;
}

static uint8_t dl_loop_carry(const uint8_t* r) {
    uint8_t mixed =
        (uint8_t)((r[10] & 0xB3) ^ (r[9] & 0x80) ^ (r[8] & 0x83) ^ (r[7] & 0x22) ^ (r[5] & 0x73));
    uint8_t carry = (dl_mask(mixed) & (dl_bit(mixed, 3) ? 0x69 : 0x96)) ? 1u : 0u;
    mixed = (uint8_t)(mixed >> 4);
    if(dl_mask(mixed) & (dl_bit(mixed, 3) ? 0x69 : 0x96)) carry ^= 1;
    return carry;
}

static uint32_t dl_read32(const uint8_t* r, int off) {
    return (uint32_t)r[off] | ((uint32_t)r[off + 1] << 8) | ((uint32_t)r[off + 2] << 16) |
           ((uint32_t)r[off + 3] << 24);
}

static void dl_shift_left(uint8_t* r, int first, int last, uint8_t carry) {
    for(int i = first; i <= last; i++) {
        uint8_t next = (uint8_t)(r[i] >> 7);
        r[i] = (uint8_t)((r[i] << 1) | (carry & 1));
        carry = next;
    }
}
static void dl_shift_right(uint8_t* r) {
    for(int p = 0; p < 2; p++) {
        uint8_t carry = 0;
        for(int i = 6; i >= 1; i--) {
            uint8_t next = (uint8_t)(r[i] & 1);
            r[i] = (uint8_t)((r[i] >> 1) | (carry << 7));
            carry = next;
        }
    }
}

static inline uint32_t dl_rotl32(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}

static void dl_rounds(uint8_t* r, uint32_t* stack, uint32_t* state) {
    for(int i = 0; i < 32; i++) {
        uint8_t hit = (uint8_t)(dl_predicate(r) ^ (uint8_t)(*stack >> 31) ^ (uint8_t)(*state >> 31));
        dl_shift_left(r, 5, 10, hit);
        *stack <<= 1;
        *state = dl_rotl32(*state, 1);
    }
    for(int i = 0; i < 32; i++) {
        *stack = (*stack << 1) | dl_predicate(r);
        uint8_t carry = dl_loop_carry(r);
        dl_shift_left(r, 5, 10, carry);
    }
}

static bool dl_checksum(const uint8_t* f) {
    uint8_t x = 0;
    for(int i = 0; i < 10; i++) x ^= f[i];
    return x == f[10];
}

static bool dl_learn(const uint8_t* frame, uint32_t* state_out) {
    uint8_t r[DL_FRAME_BYTES];
    memcpy(r, frame, DL_FRAME_BYTES);
    dl_shift_right(r);
    uint32_t state = (uint32_t)r[5] | ((uint32_t)(r[6] & 3) << 8) |
                     ((uint32_t)((uint8_t)(r[6] << 2) & 0xF0) << 24);
    uint32_t stack = dl_read32(r, 7);
    r[6] = 0x46;
    r[5] = 6;
    dl_rounds(r, &stack, &state);
    *state_out = state;
    return stack == dl_read32(r, 1);
}

static uint16_t dl_counter_of(uint32_t state) {
    return (uint16_t)((state & 0xFF) | ((state >> 2) & 0x3F00));
}

static inline bool dl_short_width(uint32_t w) {
    return w > 140 && w < 340;
}
static inline bool dl_long_width(uint32_t w) {
    return w > 360 && w < 750;
}

static void dl_emit(SubGhzProtocolDecoderDatsunLada* instance) {
    const uint8_t* r = instance->receive_data;
    uint32_t state = 0;
    bool learnable = dl_learn(r, &state);

    if(!(instance->sync_run >= DL_PREAMBLE_MIN_SHORTS || learnable)) return;

    memcpy(instance->frame, r, DL_FRAME_BYTES);
    instance->crc_valid = learnable;
    instance->counter = learnable ? dl_counter_of(state) : 0;

    uint64_t data = 0;
    for(int i = 10; i >= 3; i--) data = (data << 8) | r[i];
    instance->generic.data = data;
    instance->generic.data_count_bit = DL_FRAME_BITS;
    instance->generic.serial =
        ((uint32_t)r[10] << 24) | ((uint32_t)r[9] << 16) | ((uint32_t)r[8] << 8) | r[7];
    instance->generic.btn = (uint8_t)(r[6] & 0xF0);
    instance->generic.cnt = instance->counter;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = DL_FRAME_BITS;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_datsun_lada_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderDatsunLada* instance = calloc(1, sizeof(SubGhzProtocolDecoderDatsunLada));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_datsun_lada;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_datsun_lada_free(void* context) {
    furi_check(context);
    free(context);
}

void subghz_protocol_decoder_datsun_lada_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderDatsunLada* instance = context;
    instance->step = DLStepReset;
    instance->te_last = 0;
    instance->decode_count_bit = 0;
    memset(instance->receive_data, 0, DL_FRAME_BYTES);
    instance->marker = false;
    instance->glue = false;
    instance->short_run = 0;
    instance->sync_run = 0;
}

void subghz_protocol_decoder_datsun_lada_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderDatsunLada* instance = context;

    if(duration < DL_GLITCH_US) {
        instance->glue = true;
        return;
    }
    uint32_t previous = instance->te_last;
    if(instance->glue) {
        duration += previous;
        instance->glue = false;
    }
    instance->te_last = duration;
    level = !level;

    uint16_t run_before = instance->short_run;
    instance->short_run =
        dl_short_width(duration) ? (uint16_t)(instance->short_run + (instance->short_run < 0xFFFF ? 1 : 0)) : 0;

    if(instance->step == DLStepReset) {
        instance->decode_count_bit = 0;
        if(level && dl_short_width(previous) && dl_long_width(duration)) {
            instance->step = DLStepData;
            instance->marker = false;
            memset(instance->receive_data, 0, DL_FRAME_BYTES);
            instance->sync_run = run_before;
        }
        return;
    }

    if(instance->decode_count_bit == 0 && dl_long_width(previous) && dl_short_width(duration)) {
        instance->marker = false;
        return;
    }

    if(dl_short_width(duration) || dl_long_width(duration)) {
        if(dl_short_width(duration) && instance->marker) {
            instance->marker = false;
            return;
        }
        if(instance->decode_count_bit >= DL_FRAME_BITS) {
            instance->step = DLStepReset;
            instance->decode_count_bit = 0;
            return;
        }
        int byte = 10 - (instance->decode_count_bit >> 3);
        if(level)
            instance->receive_data[byte] |= (uint8_t)(1 << (7 - (instance->decode_count_bit & 7)));
        instance->decode_count_bit++;
        instance->marker = true;
        return;
    }

    bool found =
        (instance->decode_count_bit == DL_FRAME_BITS) && dl_checksum(instance->receive_data);
    instance->step = DLStepReset;
    instance->decode_count_bit = 0;
    if(found) dl_emit(instance);
}

SubGhzProtocolStatus subghz_protocol_decoder_datsun_lada_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderDatsunLada* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;
    if(!flipper_format_write_hex(flipper_format, "Frame", instance->frame, DL_FRAME_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_datsun_lada_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderDatsunLada* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format,
        subghz_protocol_datsun_lada_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;
    flipper_format_rewind(flipper_format);
    flipper_format_read_hex(flipper_format, "Frame", instance->frame, DL_FRAME_BYTES);
    uint32_t state = 0;
    instance->crc_valid = dl_learn(instance->frame, &state);
    instance->counter = instance->crc_valid ? dl_counter_of(state) : 0;
    instance->generic.cnt = instance->counter;
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

void subghz_protocol_decoder_datsun_lada_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderDatsunLada* instance = context;
    const uint64_t extra[1] = {instance->crc_valid ? 1u : 0u};
    const SubGhzDecodeInput input = {
        .generic = &instance->generic,
        .extra = extra,
        .extra_count = 1,
    };
    subghz_decode_plugin_get_string("Datsun Lada", &input, output);
}

const SubGhzProtocolDecoder subghz_protocol_datsun_lada_decoder = {
    .alloc = subghz_protocol_decoder_datsun_lada_alloc,
    .free = subghz_protocol_decoder_datsun_lada_free,
    .feed = subghz_protocol_decoder_datsun_lada_feed,
    .reset = subghz_protocol_decoder_datsun_lada_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_datsun_lada_serialize,
    .deserialize = subghz_protocol_decoder_datsun_lada_deserialize,
    .get_string = subghz_protocol_decoder_datsun_lada_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_datsun_lada_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_datsun_lada");

const SubGhzProtocol subghz_protocol_datsun_lada = {
    .name = SUBGHZ_PROTOCOL_DATSUN_LADA_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_datsun_lada_decoder,
    .encoder = &subghz_protocol_datsun_lada_encoder_ext.vtable,
};
