#include "honda_v8.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"

#define TAG "HondaV8"

// Honda V8 -- ported from firmware honda_v8.c via KeeLoq-RX arke_honda.py (class HondaV8).
// KeeLoq inverted PWM, base symbol 400 us (te_delta 150), sync >= 2500 us. 66-bit frame,
// LSB-first; low-pulse width encodes the bit (short -> 0, long -> 1). The accumulated 64
// bits are bit-reversed to "raw". Fields: button=raw>>60 (4b), serial=raw>>32 (28b),
// enc = raw[31:0] (32-bit KeeLoq ciphertext).
// Integrity = decryption match: for each Honda mfr key (21-entry table, probed in a fixed
// order), KeeLoq-decrypt (528 rounds, NLF 0x3A5C742E) the ciphertext and require
//   (dec & 0xF) == button  AND  ((dec>>4)&0xFF) == ((serial>>20)&0xFF)  (discriminant)
// and reject the all-zero (dec rc==0 && disc==0) case. Counter = (dec>>16)&0xFFFF, gated
// by a rolling window (1 <= delta <= 128). Decode-only; .encoder = NULL.

#define HV8_TE_BASE  400
#define HV8_TE_DELTA 150
#define HV8_SYNC_MIN 2500
#define HV8_FRAME_BITS 66
#define HV8_ROLL_WIN 128
#define HV8_NLF 0x3A5C742EUL
#define HV8_ROUNDS 528

static const SubGhzBlockConst subghz_protocol_honda_v8_const = {
    .te_short = HV8_TE_BASE,
    .te_long = HV8_TE_BASE * 2,
    .te_delta = HV8_TE_DELTA,
    .min_count_bit_for_found = HV8_FRAME_BITS,
};

// Honda manufacturer key table (low, high) -- 21 entries.
static const uint32_t hv8_keys_lo[21] = {
    0xA8F5DFFC, 0xABBA94D4, 0xDCF40FCD, 0x56EB02D8, 0xA8507BA5, 0x6857C1C5, 0x54E900DA,
    0x5683FC9B, 0x53EE07DD, 0x5D973077, 0x52EF06DC, 0x36714027, 0x51EC05DF, 0x1F4DAA96,
    0x50ED04DE, 0xDC8DC8EA, 0x30850A82, 0xD5204919, 0x558EA3B0, 0x54454441, 0x40802014};
static const uint32_t hv8_keys_hi[21] = {
    0x8DAA5CDB, 0x10C1F940, 0x460114B7, 0xE9B977B4, 0x3F26FA21, 0xA4C5C542, 0xEBBB753E,
    0x6A67C523, 0xECBC7263, 0xE1CA6024, 0xEDBD737C, 0x6011AC25, 0xEEBE7059, 0x9BB87A26,
    0xEFBF713C, 0xF39FD627, 0x610A14EB, 0x1719FF41, 0x779F14DA, 0x47454DE2, 0x0514F348};
static const uint8_t hv8_probe[21] = {14, 1, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 18, 19, 20};

struct SubGhzProtocolDecoderHondaV8 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t parser_step;
    uint64_t data;
    uint16_t count;
    bool have_last;
    uint16_t last_rc;

    uint32_t serial;
    uint8_t btn;
    uint16_t cnt;
    uint8_t key_idx;
    uint32_t enc;
    bool crc_ok;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderHondaV8);


typedef enum {
    HondaV8StepReset = 0,
    HondaV8StepData = 1,
} HondaV8Step;

static bool honda_v8_btn_valid(uint8_t btn) {
    switch(btn) {
    case 0x0: // Panic
    case 0x1: // Lock
    case 0x2: // Trunk
    case 0x4: // Remote Start
    case 0x8: // Unlock
    case 0x9: // Unlock x2
        return true;
    default:
        return false;
    }
}

static uint32_t honda_v8_nlf(uint32_t x) {
    uint32_t idx = (((x >> 1) & 1) | (((x >> 9) & 1) << 1) | (((x >> 20) & 1) << 2) |
                    (((x >> 26) & 1) << 3) | (((x >> 31) & 1) << 4));
    return (HV8_NLF >> idx) & 1;
}

// KeeLoq-style decrypt, exactly as arke_honda._hv8_decrypt (shift-right, 528 rounds).
static uint32_t honda_v8_decrypt(uint32_t ct, uint64_t key) {
    uint32_t x = ct;
    for(int i = 0; i < HV8_ROUNDS; i++) {
        uint32_t kb = (uint32_t)((key >> ((15 - i) & 0x3F)) & 1);
        uint32_t nlf = honda_v8_nlf(x);
        uint32_t msb = x & 1;
        uint32_t b15 = (x >> 15) & 1;
        uint32_t fb = msb ^ nlf ^ b15 ^ kb;
        x = (x >> 1) | (fb << 31);
    }
    return x;
}

// Mirror HondaV8._find_key(). Returns key index (0..20) or -1; outputs dec and rc.
static int honda_v8_find_key(uint32_t enc, uint32_t serial, uint8_t btn, uint32_t* out_dec, uint16_t* out_rc) {
    uint8_t exp_disc = (uint8_t)((serial >> 20) & 0xFF);
    for(int p = 0; p < 21; p++) {
        int ki = hv8_probe[p];
        uint64_t k = ((uint64_t)hv8_keys_hi[ki] << 32) | hv8_keys_lo[ki];
        uint32_t dec = honda_v8_decrypt(enc, k);
        if((dec & 0x0000000FU) != btn) continue;
        uint8_t disc = (uint8_t)((dec & 0x00000FF0U) >> 4);
        if(disc != exp_disc) continue;
        uint16_t rc = (uint16_t)((dec & 0xFFFF0000U) >> 16);
        if(rc == 0 && disc == 0) continue;
        *out_dec = dec;
        *out_rc = rc;
        return ki;
    }
    return -1;
}

static uint64_t honda_v8_reverse64(uint64_t v) {
    uint64_t r = 0;
    for(int i = 0; i < 64; i++) {
        r = (r << 1) | (v & 1);
        v >>= 1;
    }
    return r;
}

static void honda_v8_process(SubGhzProtocolDecoderHondaV8* instance) {
    uint64_t raw = honda_v8_reverse64(instance->data);
    uint32_t enc = (uint32_t)(raw & 0xFFFFFFFFU);
    uint32_t serial = (uint32_t)((raw >> 32) & 0x0FFFFFFFU);
    uint8_t btn = (uint8_t)((raw >> 60) & 0x0F);

    if(!honda_v8_btn_valid(btn)) return;
    if(serial == 0 || serial == 0x0FFFFFFF) return;

    uint32_t dec = 0;
    uint16_t rc = 0;
    int ki = honda_v8_find_key(enc, serial, btn, &dec, &rc);
    if(ki < 0) return;

    if(instance->have_last) {
        uint16_t d = (uint16_t)((rc - instance->last_rc) & 0xFFFF);
        if(d == 0 || d > HV8_ROLL_WIN) return;
    }
    instance->last_rc = rc;
    instance->have_last = true;

    instance->serial = serial;
    instance->btn = btn;
    instance->cnt = rc;
    instance->key_idx = (uint8_t)ki;
    instance->enc = enc;
    instance->crc_ok = true;

    instance->generic.data = raw;
    instance->generic.data_count_bit = HV8_FRAME_BITS;
    instance->generic.serial = serial;
    instance->generic.btn = btn;
    instance->generic.cnt = rc;
    instance->decoder.decode_data = raw;
    instance->decoder.decode_count_bit = HV8_FRAME_BITS;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_honda_v8_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHondaV8* instance = calloc(1, sizeof(SubGhzProtocolDecoderHondaV8));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_honda_v8;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_honda_v8_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV8* instance = context;
    instance->parser_step = HondaV8StepReset;
    instance->data = 0;
    instance->count = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    // NB: rolling-window state (have_last/last_rc) intentionally preserved across resets,
    // mirroring the persistent decoder instance (per arke HondaV8.last_rc).
}

void subghz_protocol_decoder_honda_v8_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV8* instance = context;

    switch(instance->parser_step) {
    case HondaV8StepReset:
        if((!level) && duration >= HV8_SYNC_MIN) {
            instance->data = 0;
            instance->count = 0;
            instance->parser_step = HondaV8StepData;
        }
        break;

    case HondaV8StepData:
        if(!level) {
            if(duration >= HV8_SYNC_MIN) {
                if(instance->count == HV8_FRAME_BITS || instance->count > 60) {
                    honda_v8_process(instance);
                }
                instance->data = 0;
                instance->count = 0;
                return;
            }
            uint8_t bit_val;
            if(duration + HV8_TE_DELTA >= HV8_TE_BASE && duration <= HV8_TE_BASE + HV8_TE_DELTA) {
                bit_val = 0;
            } else {
                bit_val = 1;
            }
            if(instance->count < 64) {
                instance->data >>= 1;
                if(bit_val) instance->data |= ((uint64_t)1 << 63);
            }
            instance->count++;
            if(instance->count >= HV8_FRAME_BITS) {
                honda_v8_process(instance);
                instance->data = 0;
                instance->count = 0;
                instance->parser_step = HondaV8StepReset;
            }
        }
        break;

    default:
        break;
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_honda_v8_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV8* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t serial = instance->serial;
    uint32_t btn = instance->btn;
    uint32_t cnt = instance->cnt;
    uint32_t key_idx = instance->key_idx;
    uint32_t enc = instance->enc;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "Btn", &btn, 1) ||
       !flipper_format_write_uint32(flipper_format, "Cnt", &cnt, 1) ||
       !flipper_format_write_uint32(flipper_format, "KeyIdx", &key_idx, 1) ||
       !flipper_format_write_uint32(flipper_format, "Enc", &enc, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_honda_v8_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV8* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_honda_v8_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint64_t raw = instance->generic.data;
    instance->serial = (uint32_t)((raw >> 32) & 0x0FFFFFFFU);
    instance->btn = (uint8_t)((raw >> 60) & 0x0F);
    instance->enc = (uint32_t)(raw & 0xFFFFFFFFU);

    uint32_t tmp = 0;
    flipper_format_rewind(flipper_format);
    instance->cnt = flipper_format_read_uint32(flipper_format, "Cnt", &tmp, 1) ? (uint16_t)tmp : 0;
    flipper_format_rewind(flipper_format);
    instance->key_idx =
        flipper_format_read_uint32(flipper_format, "KeyIdx", &tmp, 1) ? (uint8_t)tmp : 0xFF;

    // Re-validate the KeeLoq key match on load.
    uint32_t dec = 0;
    uint16_t rc = 0;
    int ki = honda_v8_find_key(instance->enc, instance->serial, instance->btn, &dec, &rc);
    instance->crc_ok = (ki >= 0);
    if(ki >= 0) {
        instance->key_idx = (uint8_t)ki;
        instance->cnt = rc;
    }

    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->btn;
    instance->generic.cnt = instance->cnt;
    instance->decoder.decode_data = raw;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_honda_v8_decoder = {
    .alloc = subghz_protocol_decoder_honda_v8_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_honda_v8_feed,
    .reset = subghz_protocol_decoder_honda_v8_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_honda_v8_serialize,
    .deserialize = subghz_protocol_decoder_honda_v8_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_honda_v8 = {
    .name = HONDA_V8_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_honda_v8_decoder,
    .encoder = NULL,
};
