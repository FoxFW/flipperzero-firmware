/* enc_fiat_v2_v3_v5.fal : SD encoder for fiat_v2_v3_v5 (auto-split enc v1). FoxFW2.0 2026-10-21. */
/* plugin-unique globals (fbt also links plugin sources into firmware.elf) */
#define subghz_protocol_encoder_fiat_alloc enc_fiat_v2_v3_v5__subghz_protocol_encoder_fiat_alloc
#define subghz_protocol_encoder_fiat_deserialize enc_fiat_v2_v3_v5__subghz_protocol_encoder_fiat_deserialize
#define subghz_protocol_encoder_fiat_free enc_fiat_v2_v3_v5__subghz_protocol_encoder_fiat_free
#define subghz_protocol_encoder_fiat_stop enc_fiat_v2_v3_v5__subghz_protocol_encoder_fiat_stop
#define subghz_protocol_encoder_fiat_yield enc_fiat_v2_v3_v5__subghz_protocol_encoder_fiat_yield
#define subghz_protocol_fiat_v2_v3_v5 enc_fiat_v2_v3_v5__subghz_protocol_fiat_v2_v3_v5
#include <lib/subghz/encode_plugin.h>
#include <flipper_application/flipper_application.h>
#include <string.h>
#include <lib/subghz/protocols/fiat_v2_v3_v5.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/blocks/custom_btn_i.h>
#include <lib/subghz/protocols/common.h>

#define TAG "SubGhzProtocolFiat"
#define FIAT_V3_TE_SHORT         460
#define FIAT_V3_TE_LONG          920
#define FIAT_V3_TE_DELTA         200
#define FIAT_V3_MIN_GAP          1500
#define FIAT_V3_FRAME_BITS       70
#define FIAT_V3_MIN_FRAME_BITS   60
#define FIAT_V3_MAX_BITS         100
#define FIAT_V3_PREAMBLE_BITS    13
#define FIAT_V5_TE_SHORT       200
#define FIAT_V5_TE_LONG        400
#define FIAT_V5_TE_DELTA       100
#define FIAT_V5_MIN_GAP        2000
#define FIAT_V5_FRAME_BITS     97
#define FIAT_V5_MIN_FRAME_BITS 90
#define FIAT_V5_MAX_BITS       130
#define FIAT_V5_PREAMBLE_BITS  15
#define FIAT_VARIANT_IDX(v) ((v) == FiatVariantV3 ? 0 : 1)
#define FIAT_VARIANT_COUNT  2
#define FIAT_V3_BTN_LOCK   0x00
#define FIAT_V3_BTN_UNLOCK 0x01
#define FIAT_V3_BTN_TRUNK  0x02
#define FIAT_V5_BTN_OPEN  0x80
#define FIAT_V5_BTN_TRUNK 0x40
#define FIAT_V5_BTN_CLOSE 0x20

typedef enum {
    FiatVariantV3 = 3,
    FiatVariantV5 = 5,
} FiatVariant;

typedef struct {
    FiatVariant variant;
    const SubGhzBlockConst* block_const;
    uint32_t min_gap;
    uint16_t frame_bits;
    uint16_t max_bits;
    uint8_t preamble_bits;
    uint8_t btn_max;
    bool has_counter;
    uint8_t cnt_length_bit;
    uint8_t btn_length_bit;
    const char* display_name;
} FiatVariantParams;

typedef struct {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    FiatVariant variant;

    union {
        struct {
            uint32_t fix_value;
            uint8_t btn;
            uint8_t status_bits;
            uint64_t hop_value;
            uint8_t hop_low;
        } v3;
        struct {
            uint32_t fix_value;
            uint8_t type_byte;
            uint8_t btn;
            uint8_t seed;
            uint8_t cnt;
            uint32_t hop_value;
        } v5;
    } extra;
} SubGhzProtocolEncoderFiat;

static const SubGhzBlockConst subghz_protocol_fiat_v3_const = {
    .te_short = FIAT_V3_TE_SHORT,
    .te_long = FIAT_V3_TE_LONG,
    .te_delta = FIAT_V3_TE_DELTA,
    .min_count_bit_for_found = FIAT_V3_MIN_FRAME_BITS,
};

static const SubGhzBlockConst subghz_protocol_fiat_v5_const = {
    .te_short = FIAT_V5_TE_SHORT,
    .te_long = FIAT_V5_TE_LONG,
    .te_delta = FIAT_V5_TE_DELTA,
    .min_count_bit_for_found = FIAT_V5_MIN_FRAME_BITS,
};

static const FiatVariantParams fiat_variant_params[FIAT_VARIANT_COUNT] = {
    {
        .variant = FiatVariantV3,
        .block_const = &subghz_protocol_fiat_v3_const,
        .min_gap = FIAT_V3_MIN_GAP,
        .frame_bits = FIAT_V3_FRAME_BITS,
        .max_bits = FIAT_V3_MAX_BITS,
        .preamble_bits = FIAT_V3_PREAMBLE_BITS,
        .btn_max = 3,
        .has_counter = false,
        .cnt_length_bit = 0,
        .btn_length_bit = 2,
        .display_name = "Fiat V3",
    },
    {
        .variant = FiatVariantV5,
        .block_const = &subghz_protocol_fiat_v5_const,
        .min_gap = FIAT_V5_MIN_GAP,
        .frame_bits = FIAT_V5_FRAME_BITS,
        .max_bits = FIAT_V5_MAX_BITS,
        .preamble_bits = FIAT_V5_PREAMBLE_BITS,
        .btn_max = 3,
        .has_counter = true,
        .cnt_length_bit = 8,
        .btn_length_bit = 8,
        .display_name = "Fiat V5",
    },
};

static const uint8_t fiat_v3_btns[] = {FIAT_V3_BTN_LOCK, FIAT_V3_BTN_UNLOCK, FIAT_V3_BTN_TRUNK};

static const uint8_t fiat_v5_btns[] = {FIAT_V5_BTN_CLOSE, FIAT_V5_BTN_OPEN, FIAT_V5_BTN_TRUNK};

/* FoxFW: positional custom-button helpers. The applab source used map-less "_positional"
 * variants that FoxFW's custom_btn lib does not provide; defined file-local so nothing is
 * added to the core lib. Positional mode: btns[] is the selectable set, custom_id (1-based)
 * picks btns[custom_id-1] (custom_max == count); custom_id 0 / out-of-range falls back to the
 * decoded button. fill_labels is a no-op, matching lib/subghz/blocks/custom_btn.c. */
static void subghz_custom_btn_apply_positional(
    uint8_t* btn,
    uint8_t original,
    uint8_t custom_id,
    const uint8_t* btns,
    size_t btns_count);
static const FiatVariantParams* fiat_get_params(FiatVariant v);
// ============================================================================
// UNIFIED BUTTON FUNCTIONS
// ============================================================================


static uint8_t fiat_get_btn_code(FiatVariant variant, uint8_t original_btn);
// ============================================================================
// V3 PACK / UNPACK helpers
// ============================================================================

static void fiat_v3_pack_generic(
    SubGhzBlockGeneric* generic,
    uint32_t fix,
    uint8_t btn,
    uint64_t hop,
    uint8_t* hop_low_out);
// ============================================================================
// V5 PACK / UNPACK helpers
// ============================================================================

static void fiat_v5_pack_generic(
    SubGhzBlockGeneric* generic,
    uint32_t fix,
    uint8_t btn,
    uint8_t cnt,
    uint32_t hop);
static void fiat_v5_unpack_generic(
    const SubGhzBlockGeneric* generic,
    uint32_t* fix_out,
    uint32_t* hop_out);
// ============================================================================
// MANCHESTER ENCODER (shared, parametrized by te_short/te_long)
// ============================================================================

static size_t
    fiat_encode_bit(LevelDuration* buf, uint8_t* state, uint8_t bit, uint16_t te_s, uint16_t te_l);
// ============================================================================
// UNIFIED BUILD UPLOAD
// ============================================================================

static void fiat_build_upload(SubGhzProtocolEncoderFiat* enc);

/* FoxFW: positional custom-button helpers. The applab source used map-less "_positional"
 * variants that FoxFW's custom_btn lib does not provide; defined file-local so nothing is
 * added to the core lib. Positional mode: btns[] is the selectable set, custom_id (1-based)
 * picks btns[custom_id-1] (custom_max == count); custom_id 0 / out-of-range falls back to the
 * decoded button. fill_labels is a no-op, matching lib/subghz/blocks/custom_btn.c. */
static void subghz_custom_btn_apply_positional(
    uint8_t* btn,
    uint8_t original,
    uint8_t custom_id,
    const uint8_t* btns,
    size_t btns_count) {
    if(!btn) return;
    if(custom_id == 0 || btns == NULL || btns_count == 0 || custom_id > btns_count) {
        *btn = original;
        return;
    }
    *btn = btns[custom_id - 1];
}

static const FiatVariantParams* fiat_get_params(FiatVariant v) {
    return &fiat_variant_params[FIAT_VARIANT_IDX(v)];
}

// ============================================================================
// UNIFIED BUTTON FUNCTIONS
// ============================================================================


static uint8_t fiat_get_btn_code(FiatVariant variant, uint8_t original_btn) {
    uint8_t btn = original_btn;
    uint8_t custom_id = subghz_custom_btn_get();
    switch(variant) {
    case FiatVariantV3:
        subghz_custom_btn_apply_positional(
            &btn, original_btn, custom_id, fiat_v3_btns, COUNT_OF(fiat_v3_btns));
        break;
    case FiatVariantV5:
        subghz_custom_btn_apply_positional(
            &btn, original_btn, custom_id, fiat_v5_btns, COUNT_OF(fiat_v5_btns));
        break;
    }
    return btn;
}

// ============================================================================
// V3 PACK / UNPACK helpers
// ============================================================================

static void fiat_v3_pack_generic(
    SubGhzBlockGeneric* generic,
    uint32_t fix,
    uint8_t btn,
    uint64_t hop,
    uint8_t* hop_low_out) {
    generic->data = ((uint64_t)(fix & 0x3FFFFF) << 42) | ((uint64_t)(btn & 0x3) << 40) |
                    ((hop >> 4) & 0xFFFFFFFFFFULL);
    generic->data_count_bit = FIAT_V3_FRAME_BITS;
    generic->serial = fix;
    generic->btn = btn;
    generic->cnt = 0;
    if(hop_low_out) *hop_low_out = hop & 0xF;
}

// ============================================================================
// V5 PACK / UNPACK helpers
// ============================================================================

static void fiat_v5_pack_generic(
    SubGhzBlockGeneric* generic,
    uint32_t fix,
    uint8_t btn,
    uint8_t cnt,
    uint32_t hop) {
    generic->data = ((uint64_t)fix << 32) | ((uint64_t)hop & 0xFFFFFFFF);
    generic->data_count_bit = FIAT_V5_FRAME_BITS;
    generic->serial = fix;
    generic->btn = btn;
    generic->cnt = cnt;
}

static void fiat_v5_unpack_generic(
    const SubGhzBlockGeneric* generic,
    uint32_t* fix_out,
    uint32_t* hop_out) {
    if(fix_out) *fix_out = (uint32_t)(generic->data >> 32);
    if(hop_out) *hop_out = (uint32_t)(generic->data & 0xFFFFFFFF);
}

// ============================================================================
// MANCHESTER ENCODER (shared, parametrized by te_short/te_long)
// ============================================================================

static size_t
    fiat_encode_bit(LevelDuration* buf, uint8_t* state, uint8_t bit, uint16_t te_s, uint16_t te_l) {
    size_t count = 0;

    switch(*state) {
    case 1:
        if(bit == 0) {
            buf[count++] = level_duration_make(false, (uint32_t)te_l);
            *state = 2;
        } else {
            buf[count++] = level_duration_make(false, (uint32_t)te_s);
            buf[count++] = level_duration_make(true, (uint32_t)te_s);
            *state = 1;
        }
        break;
    case 2:
        if(bit == 0) {
            buf[count++] = level_duration_make(true, (uint32_t)te_s);
            buf[count++] = level_duration_make(false, (uint32_t)te_s);
            *state = 2;
        } else {
            buf[count++] = level_duration_make(true, (uint32_t)te_l);
            *state = 1;
        }
        break;
    case 0:
        if(bit == 1) {
            buf[count++] = level_duration_make(true, (uint32_t)te_s);
            *state = 1;
        } else {
            buf[count++] = level_duration_make(true, (uint32_t)te_s);
            buf[count++] = level_duration_make(false, (uint32_t)te_l);
            *state = 2;
        }
        break;
    case 3:
        if(bit == 0) {
            buf[count++] = level_duration_make(false, (uint32_t)te_s);
            *state = 2;
        } else {
            buf[count++] = level_duration_make(false, (uint32_t)te_s);
            buf[count++] = level_duration_make(true, (uint32_t)te_l);
            *state = 1;
        }
        break;
    default:
        *state = 1;
        break;
    }

    return count;
}

// ============================================================================
// UNIFIED BUILD UPLOAD
// ============================================================================

static void fiat_build_upload(SubGhzProtocolEncoderFiat* enc) {
    LevelDuration* buf = enc->encoder.upload;
    size_t idx = 0;
    uint8_t state = 0;
    const FiatVariantParams* params = fiat_get_params(enc->variant);
    uint16_t te_s = params->block_const->te_short;
    uint16_t te_l = params->block_const->te_long;

    // Preamble
    for(int i = 0; i < params->preamble_bits; i++) {
        idx += fiat_encode_bit(&buf[idx], &state, 1, te_s, te_l);
    }

    switch(enc->variant) {
    case FiatVariantV3: {
        for(int bit = 21; bit >= 0; bit--) {
            idx += fiat_encode_bit(
                &buf[idx], &state, bit_read(enc->extra.v3.fix_value, bit), te_s, te_l);
        }
        for(int bit = 1; bit >= 0; bit--) {
            idx +=
                fiat_encode_bit(&buf[idx], &state, bit_read(enc->extra.v3.btn, bit), te_s, te_l);
        }
        for(int bit = 1; bit >= 0; bit--) {
            idx += fiat_encode_bit(
                &buf[idx], &state, bit_read(enc->extra.v3.status_bits, bit), te_s, te_l);
        }
        for(int bit = 43; bit >= 0; bit--) {
            uint8_t b = (enc->extra.v3.hop_value >> bit) & 1;
            idx += fiat_encode_bit(&buf[idx], &state, b, te_s, te_l);
        }
        buf[idx++] = level_duration_make(true, FIAT_V3_MIN_GAP);
    } break;

    case FiatVariantV5: {
        for(int bit = 31; bit >= 0; bit--) {
            idx += fiat_encode_bit(
                &buf[idx], &state, bit_read(enc->extra.v5.fix_value, bit), te_s, te_l);
        }
        for(int bit = 7; bit >= 0; bit--) {
            idx += fiat_encode_bit(
                &buf[idx], &state, bit_read(enc->extra.v5.type_byte, bit), te_s, te_l);
        }
        for(int bit = 7; bit >= 0; bit--) {
            idx +=
                fiat_encode_bit(&buf[idx], &state, bit_read(enc->extra.v5.btn, bit), te_s, te_l);
        }
        for(int bit = 7; bit >= 0; bit--) {
            idx +=
                fiat_encode_bit(&buf[idx], &state, bit_read(enc->extra.v5.seed, bit), te_s, te_l);
        }
        for(int bit = 7; bit >= 0; bit--) {
            idx +=
                fiat_encode_bit(&buf[idx], &state, bit_read(enc->extra.v5.cnt, bit), te_s, te_l);
        }
        for(int bit = 31; bit >= 0; bit--) {
            idx += fiat_encode_bit(
                &buf[idx], &state, bit_read(enc->extra.v5.hop_value, bit), te_s, te_l);
        }
        idx += fiat_encode_bit(&buf[idx], &state, 0, te_s, te_l);
        buf[idx++] = level_duration_make(true, FIAT_V5_MIN_GAP);
    } break;
    }

    enc->encoder.size_upload = idx;
}

// ============================================================================
// ENCODER — ALLOC / FREE / STOP / YIELD
// ============================================================================

void* subghz_protocol_encoder_fiat_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolEncoderFiat* instance = malloc(sizeof(SubGhzProtocolEncoderFiat));
    instance->base.protocol = &subghz_protocol_fiat_v2_v3_v5;
    instance->generic.protocol_name = subghz_protocol_fiat_v2_v3_v5.name;
    instance->variant = FiatVariantV3; // default, set during deserialize
    instance->encoder.repeat = 10;
    instance->encoder.size_upload = 768;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    memset(&instance->extra, 0, sizeof(instance->extra));

    return instance;
}

void subghz_protocol_encoder_fiat_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderFiat* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_fiat_stop(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderFiat* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_fiat_yield(void* context) {
    SubGhzProtocolEncoderFiat* instance = context;

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

// ============================================================================
// ENCODER — DESERIALIZE
// ============================================================================

SubGhzProtocolStatus
    subghz_protocol_encoder_fiat_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderFiat* enc = context;

    enc->encoder.is_running = false;
    enc->encoder.front = 0;

    SubGhzProtocolStatus res = SubGhzProtocolStatusError;

    do {
        if(SubGhzProtocolStatusOk !=
           subghz_block_generic_deserialize(&enc->generic, flipper_format)) {
            FURI_LOG_E(TAG, "DE"); // Deserialize error
            break;
        }

        // Read variant — or auto-detect from Bit count
        uint32_t variant_temp = 0;
        if(flipper_format_read_uint32(flipper_format, "Variant", &variant_temp, 1)) {
            enc->variant = (FiatVariant)variant_temp;
        } else {
            switch(enc->generic.data_count_bit) {
            case FIAT_V3_FRAME_BITS:
                enc->variant = FiatVariantV3;
                break;
            case FIAT_V5_FRAME_BITS:
                enc->variant = FiatVariantV5;
                break;
            default:
                FURI_LOG_E(
                    TAG, "UVFBC %d", enc->generic.data_count_bit); // Unknown variant for bit count
                break;
            }
        }

        // Optional Repeat
        flipper_format_read_uint32(flipper_format, "Repeat", (uint32_t*)&enc->encoder.repeat, 1);

        switch(enc->variant) {
        case FiatVariantV3: {
            uint32_t hop_low_temp = 0;
            flipper_format_read_uint32(flipper_format, "HopLow", &hop_low_temp, 1);
            enc->extra.v3.hop_low = hop_low_temp & 0xF;

            uint32_t status_temp = 0;
            flipper_format_read_uint32(flipper_format, "Status", &status_temp, 1);
            enc->extra.v3.status_bits = status_temp & 0x3;

            enc->extra.v3.fix_value = (enc->generic.data >> 42) & 0x3FFFFF;
            enc->extra.v3.btn = (enc->generic.data >> 40) & 0x3;
            enc->extra.v3.hop_value = ((enc->generic.data & 0xFFFFFFFFFFULL) << 4) |
                                      (enc->extra.v3.hop_low & 0xF);

            // Button handling
            subghz_custom_btn_set_original_if_unset(enc->extra.v3.btn);
            subghz_custom_btn_set_max(3);

            {
                uint8_t btn = enc->extra.v3.btn;
                if(!subghz_block_generic_global_button_override_get(&btn)) {
                    uint8_t original = subghz_custom_btn_get_original();
                    btn = fiat_get_btn_code(enc->variant, original);
                }
                enc->extra.v3.btn = btn;
            }

            fiat_v3_pack_generic(
                &enc->generic,
                enc->extra.v3.fix_value,
                enc->extra.v3.btn,
                enc->extra.v3.hop_value,
                &enc->extra.v3.hop_low);

            enc->generic.serial = enc->extra.v3.fix_value;
            enc->generic.btn = enc->extra.v3.btn;
            enc->generic.cnt = 0;
            enc->generic.data_count_bit = FIAT_V3_FRAME_BITS;
        } break;

        case FiatVariantV5: {
            uint32_t type_temp = 0;
            flipper_format_read_uint32(flipper_format, "Type", &type_temp, 1);
            enc->extra.v5.type_byte = type_temp & 0xFF;

            uint32_t seed_temp = 0;
            flipper_format_read_uint32(flipper_format, "Seed", &seed_temp, 1);
            enc->extra.v5.seed = seed_temp & 0xFF;

            fiat_v5_unpack_generic(
                &enc->generic, &enc->extra.v5.fix_value, &enc->extra.v5.hop_value);

            {
                uint32_t btn_temp = 0;
                if(flipper_format_read_uint32(flipper_format, "Btn", &btn_temp, 1)) {
                    enc->extra.v5.btn = btn_temp & 0xFF;
                } else {
                    enc->extra.v5.btn = 0;
                }

                uint32_t cnt_temp = 0;
                if(flipper_format_read_uint32(flipper_format, "Cnt", &cnt_temp, 1)) {
                    enc->extra.v5.cnt = cnt_temp & 0xFF;
                } else {
                    enc->extra.v5.cnt = 0;
                }
            }

            // Button handling
            subghz_custom_btn_set_original_if_unset(enc->extra.v5.btn);
            subghz_custom_btn_set_max(3);

            {
                uint8_t btn = enc->extra.v5.btn;
                if(!subghz_block_generic_global_button_override_get(&btn)) {
                    uint8_t original = subghz_custom_btn_get_original();
                    btn = fiat_get_btn_code(enc->variant, original);
                }
                enc->extra.v5.btn = btn;
            }

            // Counter override (8-bit)
            {
                uint32_t cnt32 = enc->extra.v5.cnt;
                if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF) {
                    if(!subghz_block_generic_global_counter_override_get(&cnt32)) {
                        if((cnt32 + furi_hal_subghz_get_rolling_counter_mult()) > 0xFF) {
                            cnt32 = 0;
                        } else {
                            cnt32 += furi_hal_subghz_get_rolling_counter_mult();
                        }
                    }
                } else {
                    cnt32 = (cnt32 + 1) & 0xFF;
                }
                enc->extra.v5.cnt = cnt32 & 0xFF;
            }

            fiat_v5_pack_generic(
                &enc->generic,
                enc->extra.v5.fix_value,
                enc->extra.v5.btn,
                enc->extra.v5.cnt,
                enc->extra.v5.hop_value);
        } break;
        }

        fiat_build_upload(enc);

        // Write updated Key back
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "RE"); // Rewind error
            break;
        }

        uint8_t key_data[sizeof(uint64_t)] = {0};
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (enc->generic.data >> (i * 8)) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "UUK"); // Unable to update Key
            break;
        }

        enc->encoder.is_running = true;
        res = SubGhzProtocolStatusOk;

    } while(false);

    return res;
}

static const SubGhzProtocolEncoder enc_fiat_v2_v3_v5_vt = {
    .alloc = subghz_protocol_encoder_fiat_alloc,
    .free = subghz_protocol_encoder_fiat_free,
    .deserialize = subghz_protocol_encoder_fiat_deserialize,
    .stop = subghz_protocol_encoder_fiat_stop,
    .yield = subghz_protocol_encoder_fiat_yield,
};

/* Plugin-local protocol object: encoder alloc points base.protocol here. */
const SubGhzProtocol subghz_protocol_fiat_v2_v3_v5 = {
    .name = SUBGHZ_PROTOCOL_FIAT_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = NULL,
    .encoder = &enc_fiat_v2_v3_v5_vt,
};

static const SubGhzEncodePluginEntry enc_fiat_v2_v3_v5_entry = {.name = SUBGHZ_PROTOCOL_FIAT_NAME, .encoder = &enc_fiat_v2_v3_v5_vt};
static const FlipperAppPluginDescriptor enc_fiat_v2_v3_v5_descriptor = {.appid = SUBGHZ_ENCODE_PLUGIN_APP_ID, .ep_api_version = SUBGHZ_ENCODE_PLUGIN_API_VERSION, .entry_point = &enc_fiat_v2_v3_v5_entry};
const FlipperAppPluginDescriptor* enc_fiat_v2_v3_v5_ep(void) { return &enc_fiat_v2_v3_v5_descriptor; }
