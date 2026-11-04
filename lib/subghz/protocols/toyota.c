#include "toyota.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

#define TAG "SubGhzProtocolToyota"

static const SubGhzBlockConst toyota_const_a = {
    .te_short                = 400,
    .te_long                 = 800,
    .te_delta                = 100,
    .min_count_bit_for_found = 68,
};

static const SubGhzBlockConst toyota_const_b = {
    .te_short                = 200,
    .te_long                 = 390,
    .te_delta                = 60,
    .min_count_bit_for_found = 67,
};

#define TOYOTA_B_NRZ_MIDPOINT   287u

#define TOYOTA_B_SYNC_GAP_MIN   1700u
#define TOYOTA_B_SYNC_GAP_MAX   2200u

#define TOYOTA_INTER_FRAME_GAP  5000u

#define TOYOTA_B_PREAMBLE_MIN   10u
#define TOYOTA_A_PREAMBLE_MIN   10u

#define TOYOTA_B_DATA_PULSE_MIN 100u
#define TOYOTA_B_DATA_PULSE_MAX 500u

#define TOYOTA_A_BITS  68u
#define TOYOTA_B_BITS  67u

#define TOYOTA_VARIANT_THRESH  310u

#define TOYOTA_A_KIA_SYNC_MIN  901u
#define TOYOTA_A_KIA_SYNC_MAX  1600u

#define TOYOTA_C_BITS           66u
#define TOYOTA_C_FRAME_BITS     68u
#define TOYOTA_C_PREAMBLE_MIN   6u
#define TOYOTA_C_SYNC_MIN       1050u
#define TOYOTA_C_SYNC_MAX       1500u
#define TOYOTA_C_HIGH_MIDPOINT  600u
#define TOYOTA_C_PRE_HIGH_MIN   200u
#define TOYOTA_C_PRE_HIGH_MAX   550u
#define TOYOTA_C_PRE_LOW_MIN    200u
#define TOYOTA_C_PRE_LOW_MAX    650u
#define TOYOTA_C_DATA_HIGH_MIN  200u
#define TOYOTA_C_DATA_HIGH_MAX  1050u
#define TOYOTA_C_END_LOW        1000u

#define TOYOTA_A_BTN_LOCK    0x08
#define TOYOTA_A_BTN_UNLOCK  0x01

#define TOYOTA_B_BTN_LOCK    0x0A
#define TOYOTA_B_BTN_UNLOCK  0x05

#define TOYOTA_C_BTN_LOCK    0x0B
#define TOYOTA_C_BTN_UNLOCK  0x0E
#define TOYOTA_C_BTN_TRUNK   0x0D
#define TOYOTA_C_BTN_PANIC   0x07

typedef enum {
    ToyotaStepReset = 0,
    ToyotaStepPreambleA,
    ToyotaStepDataA,
    ToyotaStepPreambleB,
    ToyotaStepDataB,
    ToyotaStepPreambleC,
    ToyotaStepDataC,
} ToyotaDecoderStep;

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder        decoder;
    SubGhzBlockGeneric        generic;

    uint64_t bits_lo;
    uint8_t  bits_hi;
    uint8_t  bit_count;

    uint32_t te_last;
    bool     have_high;
    uint16_t preamble_count;

    uint8_t  variant;

    uint32_t hop;
    uint32_t serial;
    uint8_t  button;

} SubGhzProtocolDecoderToyota;

const SubGhzProtocolDecoder subghz_protocol_toyota_decoder = {
    .alloc         = subghz_protocol_decoder_toyota_alloc,
    .free          = subghz_protocol_decoder_common_free,
    .feed          = subghz_protocol_decoder_toyota_feed,
    .reset         = subghz_protocol_decoder_toyota_reset,
    .get_hash_data = subghz_protocol_decoder_toyota_get_hash_data,
    .serialize     = subghz_protocol_decoder_toyota_serialize,
    .deserialize   = subghz_protocol_decoder_toyota_deserialize,
    .get_string    = subghz_protocol_decoder_toyota_get_string,
};

// TX encoder (v2.0.12): replay of a captured frame via the captured variant's line
// coding. Variant B (Tundra, NRZ 200/390) and variant C (Prius / Corolla Verso, PWM
// on the high pulse) are implemented and host-verified to round-trip through this
// file's own decoder. Variant A (Corolla, Manchester 400/800) is unreachable from
// the decoder's reset state (see subghz_protocol_decoder_toyota_feed) so no real
// capture carries it; a cnt==0 file is emitted with the B coding.
static const SubGhzEncoderExternal subghz_protocol_toyota_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_toyota");

const SubGhzProtocol subghz_protocol_toyota = {
    .name    = SUBGHZ_PROTOCOL_TOYOTA_NAME,
    .type    = SubGhzProtocolTypeDynamic,
    .flag    = SubGhzProtocolFlag_433 |
               SubGhzProtocolFlag_315 |
               SubGhzProtocolFlag_AM  |
               SubGhzProtocolFlag_Decodable |
               SubGhzProtocolFlag_Load |
               SubGhzProtocolFlag_Save |
               SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_toyota_decoder,
    .encoder = &subghz_protocol_toyota_encoder_ext.vtable,
};

static inline bool te_is_short(uint32_t d, const SubGhzBlockConst* c) {
    return DURATION_DIFF(d, (uint32_t)c->te_short) < (uint32_t)c->te_delta;
}

static inline bool te_is_long(uint32_t d, const SubGhzBlockConst* c) {
    return DURATION_DIFF(d, (uint32_t)c->te_long) < (uint32_t)c->te_delta;
}

static void toyota_push_bit(SubGhzProtocolDecoderToyota* inst, uint8_t bit) {
    uint8_t carry = (uint8_t)(inst->bits_lo >> 63) & 1;
    inst->bits_hi = (inst->bits_hi << 1) | carry;
    inst->bits_lo = (inst->bits_lo << 1) | (bit & 1);
    inst->bit_count++;
}

static uint32_t toyota_extract(
    const SubGhzProtocolDecoderToyota* inst,
    uint8_t offset,
    uint8_t length)
{
    uint32_t result = 0;
    uint8_t  total  = inst->bit_count;
    for(uint8_t i = 0; i < length; i++) {
        int8_t  pos = (int8_t)(total - 1) - (int8_t)(offset + i);
        uint8_t b   = 0;
        if(pos >= 64)     b = (inst->bits_hi >> (pos - 64)) & 1;
        else if(pos >= 0) b = (inst->bits_lo >> pos) & 1;
        result = (result << 1) | b;
    }
    return result;
}

static bool toyota_frame_plausible(uint32_t serial, uint8_t button, uint32_t hop, uint8_t variant) {
    if(serial == 0U || serial == 0x0FFFFFFFU) return false;

    if(hop == 0U || hop == 0xFFFFFFFFU) return false;

    const uint8_t b = button & 0x0FU;
    if(variant == 2U) {
        if(b != TOYOTA_C_BTN_LOCK && b != TOYOTA_C_BTN_UNLOCK &&
           b != TOYOTA_C_BTN_TRUNK && b != TOYOTA_C_BTN_PANIC) return false;
    } else if(variant == 1U) {
        if(b != TOYOTA_B_BTN_LOCK && b != TOYOTA_B_BTN_UNLOCK &&
           b != 0x0FU && b != 0x04U) return false;
    } else {
        if(b != TOYOTA_A_BTN_LOCK && b != TOYOTA_A_BTN_UNLOCK &&
           b != 0x09U && b != 0x02U && b != 0x04U) return false;
    }
    return true;
}

static void toyota_decode_and_fire(SubGhzProtocolDecoderToyota* inst) {
    const SubGhzBlockConst* c =
        (inst->variant == 1) ? &toyota_const_b : &toyota_const_a;

    if(inst->bit_count < (uint8_t)c->min_count_bit_for_found) return;

    inst->hop    = toyota_extract(inst,  0, 32);
    inst->serial = toyota_extract(inst, 32, 28);
    inst->button = (uint8_t)toyota_extract(inst, 60, 4);

    if(!toyota_frame_plausible(inst->serial, inst->button, inst->hop, inst->variant)) {
        FURI_LOG_D(TAG, "REJECT: implausible serial=%08lX btn=%X hop=%08lX var=%d",
            (unsigned long)inst->serial, (unsigned int)inst->button,
            (unsigned long)inst->hop, (int)inst->variant);
        return;
    }

    inst->generic.data =
        ((uint64_t)inst->hop    << 32) |
        ((uint64_t)inst->serial <<  4) |
        ((uint64_t)inst->button & 0x0F);
    // Frames are 67/68 bits but generic.data holds the 64 extracted bits only; keep
    // the trailing (bit_count-64) bits in data_2 so a saved capture can be replayed
    // bit-for-bit by the encoder (v2.0.12 TX support).
    inst->generic.data_2 = (inst->bit_count > 64) ?
        toyota_extract(inst, 64, (uint8_t)(inst->bit_count - 64)) : 0;

    inst->generic.data_count_bit = inst->bit_count;
    inst->generic.serial         = inst->serial;
    inst->generic.btn            = inst->button;
    inst->generic.cnt            = inst->variant;

    inst->decoder.decode_data      = inst->generic.data;
    inst->decoder.decode_count_bit = inst->generic.data_count_bit;

    FURI_LOG_D(TAG, "FIRE var=%d bits=%d hop=%08lX serial=%07lX btn=%X",
        (int)inst->variant, (int)inst->bit_count,
        (unsigned long)inst->hop,
        (unsigned long)inst->serial,
        (unsigned int)inst->button);

    if(inst->base.callback)
        inst->base.callback(&inst->base, inst->base.context);
}

void* subghz_protocol_decoder_toyota_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderToyota* inst =
        malloc(sizeof(SubGhzProtocolDecoderToyota));
    inst->base.protocol         = &subghz_protocol_toyota;
    inst->generic.protocol_name = inst->base.protocol->name;
    return inst;
}



void subghz_protocol_decoder_toyota_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderToyota* inst = context;

    inst->decoder.parser_step = ToyotaStepReset;
    inst->decoder.te_last     = 0;
    inst->bits_lo             = 0;
    inst->bits_hi             = 0;
    inst->bit_count           = 0;
    inst->te_last             = 0;
    inst->have_high           = false;
    inst->preamble_count      = 0;
    inst->hop                 = 0;
    inst->serial              = 0;
    inst->button              = 0;

}

static void toyota_feed_variant_a(
    SubGhzProtocolDecoderToyota* inst,
    bool level, uint32_t duration)
{
    const SubGhzBlockConst* c = &toyota_const_a;

    if(inst->decoder.parser_step == ToyotaStepPreambleA) {

        if(level) {
            inst->te_last   = duration;
            inst->have_high = true;
            return;
        }

        if(!inst->have_high) {
            subghz_protocol_decoder_toyota_reset(inst);
            return;
        }
        inst->have_high = false;

        if(duration >= TOYOTA_A_KIA_SYNC_MIN && duration <= TOYOTA_A_KIA_SYNC_MAX) {
            subghz_protocol_decoder_toyota_reset(inst);
            return;
        }

        bool hs = te_is_short(inst->te_last, c);
        bool hl = te_is_long (inst->te_last, c);
        bool ls = te_is_short(duration, c);
        bool ll = te_is_long (duration, c);

        if(hs && ls) {
            inst->preamble_count++;
            return;
        }

        if(inst->preamble_count < TOYOTA_A_PREAMBLE_MIN) {
            subghz_protocol_decoder_toyota_reset(inst);
            return;
        }

        if(!((hl && ls) || (hs && ll))) {
            subghz_protocol_decoder_toyota_reset(inst);
            return;
        }

        inst->bits_lo   = 0;
        inst->bits_hi   = 0;
        inst->bit_count = 0;

        if     (hl && ls) toyota_push_bit(inst, 0);
        else if(hs && ll) toyota_push_bit(inst, 1);

        inst->decoder.parser_step = ToyotaStepDataA;
        return;
    }

    if(inst->decoder.parser_step == ToyotaStepDataA) {

        if(level) {
            if(te_is_short(duration, c) || te_is_long(duration, c)) {
                inst->te_last   = duration;
                inst->have_high = true;
            } else {
                if(inst->bit_count >= (uint8_t)c->min_count_bit_for_found)
                    toyota_decode_and_fire(inst);
                subghz_protocol_decoder_toyota_reset(inst);
            }
            return;
        }

        if(!inst->have_high) return;
        inst->have_high = false;

        bool hs = te_is_short(inst->te_last, c);
        bool hl = te_is_long (inst->te_last, c);
        bool ls = te_is_short(duration, c);
        bool ll = te_is_long (duration, c);

        if(hl && ls) {
            toyota_push_bit(inst, 0);
        } else if(hs && ll) {
            toyota_push_bit(inst, 1);
        } else {
            if(inst->bit_count >= (uint8_t)c->min_count_bit_for_found)
                toyota_decode_and_fire(inst);
            subghz_protocol_decoder_toyota_reset(inst);
            return;
        }

        if(inst->bit_count >= TOYOTA_A_BITS) {
            toyota_decode_and_fire(inst);
            subghz_protocol_decoder_toyota_reset(inst);
        }
    }
}

static void toyota_feed_variant_b(
    SubGhzProtocolDecoderToyota* inst,
    bool level, uint32_t duration)
{
    const SubGhzBlockConst* c = &toyota_const_b;

    if(inst->decoder.parser_step == ToyotaStepPreambleB) {

        if(level) {
            if(te_is_short(duration, c)) {
                inst->te_last   = duration;
                inst->have_high = true;
            } else {
                subghz_protocol_decoder_toyota_reset(inst);
            }
            return;
        }

        if(!inst->have_high) {
            subghz_protocol_decoder_toyota_reset(inst);
            return;
        }
        inst->have_high = false;

        if(duration >= TOYOTA_B_SYNC_GAP_MIN &&
           duration <= TOYOTA_B_SYNC_GAP_MAX)
        {
            if(inst->preamble_count >= TOYOTA_B_PREAMBLE_MIN) {
                FURI_LOG_D(TAG, "B: sync gap after %d pairs -> NRZ data",
                    (int)inst->preamble_count);
                inst->bits_lo   = 0;
                inst->bits_hi   = 0;
                inst->bit_count = 0;
                inst->have_high = false;
                inst->decoder.parser_step = ToyotaStepDataB;
            } else {
                subghz_protocol_decoder_toyota_reset(inst);
            }
            return;
        }

        if(te_is_long(duration, c)) {
            inst->preamble_count++;
            return;
        }

        subghz_protocol_decoder_toyota_reset(inst);
        return;
    }

    if(inst->decoder.parser_step == ToyotaStepDataB) {

        if(duration >= TOYOTA_B_SYNC_GAP_MIN) {

            if(inst->bit_count >= (uint8_t)c->min_count_bit_for_found) {
                toyota_decode_and_fire(inst);
            }
            subghz_protocol_decoder_toyota_reset(inst);
            return;
        }

        if(duration < TOYOTA_B_DATA_PULSE_MIN || duration > TOYOTA_B_DATA_PULSE_MAX) {
            subghz_protocol_decoder_toyota_reset(inst);
            return;
        }

        uint8_t bit = (duration > TOYOTA_B_NRZ_MIDPOINT) ? 1 : 0;
        toyota_push_bit(inst, bit);

        if(inst->bit_count >= TOYOTA_B_BITS) {
            toyota_decode_and_fire(inst);
            subghz_protocol_decoder_toyota_reset(inst);
        }
    }
}

static void toyota_start_data_c(SubGhzProtocolDecoderToyota* inst) {
    inst->bits_lo   = 0;
    inst->bits_hi   = 0;
    inst->bit_count = 0;
    inst->have_high = false;
    inst->decoder.parser_step = ToyotaStepDataC;
}

static void toyota_decode_and_fire_c(SubGhzProtocolDecoderToyota* inst) {
    if(inst->bit_count < TOYOTA_C_BITS) return;

    inst->hop    = toyota_extract(inst,  0, 32);
    inst->serial = toyota_extract(inst, 32, 28);
    inst->button = (uint8_t)(toyota_extract(inst, 60, 4) & 0x0F);

    if(!toyota_frame_plausible(inst->serial, inst->button, inst->hop, 2)) {
        FURI_LOG_D(TAG, "REJECT(C): implausible serial=%08lX btn=%X hop=%08lX",
            (unsigned long)inst->serial, (unsigned int)inst->button,
            (unsigned long)inst->hop);
        return;
    }

    inst->variant = 2;

    inst->generic.data =
        ((uint64_t)inst->hop    << 32) |
        ((uint64_t)inst->serial <<  4) |
        ((uint64_t)inst->button & 0x0F);
    // Keep the trailing (bit_count-64) bits for bit-for-bit replay (see variant A/B).
    inst->generic.data_2 = (inst->bit_count > 64) ?
        toyota_extract(inst, 64, (uint8_t)(inst->bit_count - 64)) : 0;

    inst->generic.data_count_bit = inst->bit_count;
    inst->generic.serial         = inst->serial;
    inst->generic.btn            = inst->button;
    inst->generic.cnt            = inst->variant;

    inst->decoder.decode_data      = inst->generic.data;
    inst->decoder.decode_count_bit = inst->generic.data_count_bit;

    FURI_LOG_D(TAG, "FIRE(C) bits=%d hop=%08lX serial=%07lX btn=%X",
        (int)inst->bit_count,
        (unsigned long)inst->hop,
        (unsigned long)inst->serial,
        (unsigned int)inst->button);

    if(inst->base.callback)
        inst->base.callback(&inst->base, inst->base.context);
}

static void toyota_feed_variant_c(
    SubGhzProtocolDecoderToyota* inst,
    bool level, uint32_t duration)
{
    if(inst->decoder.parser_step == ToyotaStepPreambleC) {

        if(level) {
            inst->te_last   = duration;
            inst->have_high = true;
            return;
        }

        if(!inst->have_high) {
            subghz_protocol_decoder_toyota_reset(inst);
            return;
        }
        inst->have_high = false;

        if(duration >= TOYOTA_C_SYNC_MIN && duration <= TOYOTA_C_SYNC_MAX) {
            if(inst->preamble_count >= TOYOTA_C_PREAMBLE_MIN) {
                toyota_start_data_c(inst);
            } else {
                subghz_protocol_decoder_toyota_reset(inst);
            }
            return;
        }

        if(inst->te_last >= TOYOTA_C_PRE_HIGH_MIN && inst->te_last <= TOYOTA_C_PRE_HIGH_MAX &&
           duration >= TOYOTA_C_PRE_LOW_MIN && duration <= TOYOTA_C_PRE_LOW_MAX) {
            inst->preamble_count++;
            return;
        }

        subghz_protocol_decoder_toyota_reset(inst);
        return;
    }

    if(inst->decoder.parser_step == ToyotaStepDataC) {

        if(level) {
            if(duration >= TOYOTA_C_DATA_HIGH_MIN && duration <= TOYOTA_C_DATA_HIGH_MAX) {
                if(inst->bit_count < TOYOTA_C_FRAME_BITS) {
                    toyota_push_bit(inst, (duration > TOYOTA_C_HIGH_MIDPOINT) ? 1 : 0);
                }
                inst->have_high = true;
                inst->te_last   = duration;
            } else {
                if(inst->bit_count >= TOYOTA_C_BITS) toyota_decode_and_fire_c(inst);
                subghz_protocol_decoder_toyota_reset(inst);
                inst->variant             = 2;
                inst->decoder.parser_step = ToyotaStepPreambleC;
                inst->te_last             = duration;
                inst->have_high           = true;
            }
            return;
        }

        inst->have_high = false;

        if(duration >= TOYOTA_C_SYNC_MIN && duration <= TOYOTA_C_SYNC_MAX) {
            if(inst->bit_count >= TOYOTA_C_BITS) toyota_decode_and_fire_c(inst);
            toyota_start_data_c(inst);
            return;
        }

        if(duration > TOYOTA_C_END_LOW) {
            if(inst->bit_count >= TOYOTA_C_BITS) toyota_decode_and_fire_c(inst);
            subghz_protocol_decoder_toyota_reset(inst);
            inst->variant             = 2;
            inst->decoder.parser_step = ToyotaStepPreambleC;
            inst->preamble_count      = 0;
        }
    }
}

void subghz_protocol_decoder_toyota_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderToyota* inst = context;

    if(inst->decoder.parser_step == ToyotaStepReset) {
        if(!level) return;

        bool fits_b = te_is_short(duration, &toyota_const_b) &&
                      (duration < TOYOTA_VARIANT_THRESH);
        bool fits_a = te_is_short(duration, &toyota_const_a) &&
                      (duration >= TOYOTA_VARIANT_THRESH);

        if(fits_b) {
            inst->variant             = 1;
            inst->te_last             = duration;
            inst->have_high           = true;
            inst->preamble_count      = 0;
            inst->decoder.parser_step = ToyotaStepPreambleB;
            FURI_LOG_T(TAG, "Detected Variant B (Tundra), first HIGH=%lu",
                (unsigned long)duration);
        } else if(fits_a) {
            inst->variant             = 2;
            inst->te_last             = duration;
            inst->have_high           = true;
            inst->preamble_count      = 0;
            inst->decoder.parser_step = ToyotaStepPreambleC;
            FURI_LOG_T(TAG, "Detected Variant C (Prius/Corolla Verso), first HIGH=%lu",
                (unsigned long)duration);
        }
        return;
    }

    if(inst->variant == 1) {
        toyota_feed_variant_b(inst, level, duration);
    } else if(inst->variant == 2) {
        toyota_feed_variant_c(inst, level, duration);
    } else {
        toyota_feed_variant_a(inst, level, duration);
    }
}

uint8_t subghz_protocol_decoder_toyota_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderToyota* inst = context;
    return subghz_protocol_blocks_get_hash_data(
        &inst->decoder,
        (inst->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_toyota_serialize(
    void*              context,
    FlipperFormat*     flipper_format,
    SubGhzRadioPreset* preset)
{
    furi_assert(context);
    SubGhzProtocolDecoderToyota* inst = context;
    inst->generic.cnt = inst->variant;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&inst->generic, flipper_format, preset);
    // Persist the trailing frame bits (data_2) so the encoder can replay the full
    // 67/68-bit frame. Optional on load for backward compatibility with older
    // captures (which then replay without their trailing bits).
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t tail = (uint32_t)inst->generic.data_2;
        if(!flipper_format_write_uint32(flipper_format, "Tail", &tail, 1)) {
            FURI_LOG_E(TAG, "Unable to add Tail");
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
    }
    return ret;
}

SubGhzProtocolStatus subghz_protocol_decoder_toyota_deserialize(
    void*          context,
    FlipperFormat* flipper_format)
{
    furi_assert(context);
    SubGhzProtocolDecoderToyota* inst = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_deserialize(&inst->generic, flipper_format);

    if(ret == SubGhzProtocolStatusOk &&
       inst->generic.data_count_bit < TOYOTA_C_BITS) {
        FURI_LOG_D(TAG, "Wrong number of bits in key");
        ret = SubGhzProtocolStatusErrorValueBitCount;
    }

    if(ret == SubGhzProtocolStatusOk) {
        inst->hop    = (uint32_t)(inst->generic.data >> 32);
        inst->serial = (uint32_t)((inst->generic.data >> 4) & 0x0FFFFFFF);
        inst->button = (uint8_t)(inst->generic.data & 0x0F);

        inst->generic.serial = inst->serial;
        inst->generic.btn    = inst->button;
        inst->variant        = (inst->generic.cnt == 2) ? 2 :
                               ((inst->generic.cnt != 0) ? 1 : 0);
        inst->generic.cnt    = inst->variant;

        // Optional trailing frame bits (absent in captures saved before v2.0.12).
        uint32_t tail = 0;
        if(!flipper_format_rewind(flipper_format) ||
           !flipper_format_read_uint32(flipper_format, "Tail", &tail, 1)) {
            tail = 0;
        }
        inst->generic.data_2 = tail;
        flipper_format_rewind(flipper_format);
    }

    return ret;
}

void subghz_protocol_decoder_toyota_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderToyota* inst = context;
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &inst->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(inst->generic.protocol_name, &input, output);
}


/* ========================================================================== */
/* Encoder (v2.0.12) - replay of a captured frame                              */
/*                                                                            */
/* Reconstructs the on-air frame from generic.data (the 64 extracted bits,    */
/* MSB first = frame bits 0..63) followed by generic.data_2 (the trailing     */
/* bit_count-64 bits, MSB first), and emits it with the captured variant's    */
/* line coding. Both codings were host-verified to round-trip through this    */
/* file's decoder. RF acceptance by a vehicle still requires a bench test.    */
/* ========================================================================== */

#define TOYOTA_ENC_UPLOAD_SIZE 256

/* Variant C (Prius / Corolla Verso): PWM on the HIGH pulse, low separators.   */
/* Values sit mid-window of the decoder's accept ranges (see TOYOTA_C_*).      */
#define TOYOTA_C_TX_PRE_HIGH  375u
#define TOYOTA_C_TX_PRE_LOW   425u
#define TOYOTA_C_TX_SEP_LOW   425u
#define TOYOTA_C_TX_BIT1_HIGH 800u
#define TOYOTA_C_TX_BIT0_HIGH 400u

typedef struct {
    SubGhzProtocolEncoderBase  base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric         generic;
} SubGhzProtocolEncoderToyota;

/* Frame bit j (0 = first on air). Bits 0..63 come from data (MSB first), the
 * remainder from data_2 (MSB first over its bit_count-64 bits). */
