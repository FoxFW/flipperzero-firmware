#include "renault_v3.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include <lib/subghz/blocks/bitbuf.h>

#define TAG "RenaultV3"

// Renault V3 (Megane) -- ported from firmware renault_v3.c via KeeLoq-RX arke_renault_v123.py.
// 2-FSK NRZ, te 52/104. Variable 64..160-bit frame, two frame types:
//   [sync 0xC969 (TypeA) | 0xCB2D (TypeB) :16][serial :16][encrypted ...]
// Only the 16-bit serial is in the clear; button/counter live inside the encrypted
// remainder and are NOT separable. NO CRC. Decode-only; .encoder = NULL.
// Being 2-FSK, this protocol declares SubGhzProtocolFlag_FM.
//
// Bit engine = NRZ quantiser: each timing emits N = round(duration/te_short) bits of the
// current level. After >=MIN_PREAMBLE short pulses, the first non-short pulse starts data.
// False-positive defences: exact 16-bit sync (A or B) + 64..160-bit length envelope +
// non-degenerate serial.

#define RENAULT_V3_MIN_DATA_BITS 64
#define RENAULT_V3_MAX_DATA_BITS 160
#define RENAULT_V3_MIN_PREAMBLE  10
#define RENAULT_V3_ARR_SIZE      256
#define RENAULT_V3_OVERFLOW      250
#define RENAULT_V3_FRAME_BYTES   20 // 160 bits

static const SubGhzBlockConst subghz_protocol_renault_v3_const = {
    .te_short = 52,
    .te_long = 104,
    .te_delta = 22,
    .min_count_bit_for_found = RENAULT_V3_MIN_DATA_BITS,
};

// sync A 0xC969 = 1100 1001 0110 1001
static const uint8_t renault_v3_sync_a[16] =
    {1, 1, 0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1};
// sync B 0xCB2D = 1100 1011 0010 1101
static const uint8_t renault_v3_sync_b[16] =
    {1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1};

// Structurally meaningless 16-bit serials -> reject (matches arke _DEGENERATE 16-bit forms).
static bool renault_v3_serial_degenerate(uint16_t s) {
    return s == 0x0000 || s == 0xFFFF || s == 0x5555 || s == 0xAAAA;
}

typedef enum {
    RenaultV3StepReset = 0,
    RenaultV3StepPreamble = 1,
    RenaultV3StepCollect = 2,
} RenaultV3Step;

struct SubGhzProtocolDecoderRenaultV3 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t step;
    uint32_t preamble_count;
    uint8_t all_bits[SUBGHZ_BITBUF_BYTES(RENAULT_V3_ARR_SIZE)];
    uint16_t all_bit_count;

    uint16_t serial;
    uint8_t variant; // 0 = TypeA, 1 = TypeB
    uint8_t frame_bytes[RENAULT_V3_FRAME_BYTES]; // full MSB-first frame from sync
    uint16_t frame_bit_len;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderRenaultV3);


static void renault_v3_append_n(SubGhzProtocolDecoderRenaultV3* instance, uint32_t n, bool level) {
    uint8_t bit_val = level ? 1 : 0;
    for(uint32_t i = 0; i < n; i++) {
        if(instance->all_bit_count < RENAULT_V3_ARR_SIZE)
            subghz_bitbuf_set(instance->all_bits, instance->all_bit_count++, bit_val);
    }
}

// Mirror of arke_renault_v123.RenaultV3._process().
static void renault_v3_process(SubGhzProtocolDecoderRenaultV3* instance) {
    const uint8_t* bits = instance->all_bits;
    int n = (int)instance->all_bit_count;
    if(n < RENAULT_V3_MIN_DATA_BITS) return;

    int search_end = n - RENAULT_V3_MIN_DATA_BITS;
    if(search_end < 0) search_end = 0;

    int sync_pos = -1;
    uint8_t variant = 0;
    for(int i = 0; i <= search_end; i++) {
        bool a = true, b = true;
        for(int j = 0; j < 16; j++) {
            if(subghz_bitbuf_get(bits, i + j) != renault_v3_sync_a[j]) a = false;
            if(subghz_bitbuf_get(bits, i + j) != renault_v3_sync_b[j]) b = false;
        }
        if(a) {
            sync_pos = i;
            variant = 0;
            break;
        }
        if(b) {
            sync_pos = i;
            variant = 1;
            break;
        }
    }
    if(sync_pos < 0) return;

    int data_after = n - sync_pos;
    // Exact-length gate (hardening): within the protocol's 64..160-bit envelope.
    if(data_after < RENAULT_V3_MIN_DATA_BITS || data_after > RENAULT_V3_MAX_DATA_BITS) return;

    int data_len = data_after < RENAULT_V3_MAX_DATA_BITS ? data_after : RENAULT_V3_MAX_DATA_BITS;

    // top 64 bits -> generic.data; serial = bits 16..31 after sync.
    uint64_t gdata = 0;
    int top = data_len < 64 ? data_len : 64;
    for(int i = 0; i < top; i++) gdata = (gdata << 1) | (subghz_bitbuf_get(bits, sync_pos + i) & 1);
    uint16_t serial = (uint16_t)((gdata >> 32) & 0xFFFFU);
    if(renault_v3_serial_degenerate(serial)) return;

    // Store the whole frame MSB-first for display/serialize (encrypted remainder).
    for(int i = 0; i < RENAULT_V3_FRAME_BYTES; i++) instance->frame_bytes[i] = 0;
    for(int i = 0; i < data_len; i++) {
        if(subghz_bitbuf_get(bits, sync_pos + i)) instance->frame_bytes[i >> 3] |= (uint8_t)(0x80 >> (i & 7));
    }
    instance->frame_bit_len = (uint16_t)data_len;
    instance->serial = serial;
    instance->variant = variant;

    instance->generic.data = gdata;
    instance->generic.data_count_bit = (uint8_t)data_len;
    instance->generic.serial = serial;
    instance->generic.btn = 0;
    instance->generic.cnt = 0;
    instance->decoder.decode_data = gdata;
    instance->decoder.decode_count_bit = (uint8_t)data_len;

    if(instance->base.callback)
        instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_renault_v3_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderRenaultV3* instance = calloc(1, sizeof(SubGhzProtocolDecoderRenaultV3));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_renault_v3;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_renault_v3_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV3* instance = context;
    instance->step = RenaultV3StepReset;
    instance->preamble_count = 0;
    instance->all_bit_count = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_renault_v3_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV3* instance = context;
    uint32_t ts = subghz_protocol_renault_v3_const.te_short;
    uint32_t d = subghz_protocol_renault_v3_const.te_delta;

    if(instance->step == RenaultV3StepReset) {
        if(DURATION_DIFF(duration, ts) <= d) {
            instance->step = RenaultV3StepPreamble;
            instance->preamble_count = 1;
        }
        return;
    }

    if(instance->step == RenaultV3StepPreamble) {
        bool is_short = DURATION_DIFF(duration, ts) <= d;
        if(is_short) {
            instance->preamble_count++;
            if(instance->preamble_count > 5000) instance->step = RenaultV3StepReset;
        } else if(instance->preamble_count >= RENAULT_V3_MIN_PREAMBLE) {
            // non-SHORT after enough preamble -> NRZ data starts here
            instance->all_bit_count = 0;
            uint32_t n = (duration + ts / 2) / ts;
            if(n > 8) n = 8;
            renault_v3_append_n(instance, n, level);
            instance->step = RenaultV3StepCollect;
        } else {
            instance->step = RenaultV3StepReset;
        }
        return;
    }

    // RenaultV3StepCollect
    uint32_t n = (duration + ts / 2) / ts;
    if(n == 0 || duration > ts * 8) {
        renault_v3_process(instance);
        instance->step = RenaultV3StepReset;
        return;
    }
    renault_v3_append_n(instance, n, level);
    if(instance->all_bit_count >= RENAULT_V3_OVERFLOW) {
        renault_v3_process(instance);
        instance->step = RenaultV3StepReset;
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_renault_v3_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV3* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t serial = instance->serial;
    uint32_t variant = instance->variant;
    uint32_t bitlen = instance->frame_bit_len;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "Variant", &variant, 1) ||
       !flipper_format_write_uint32(flipper_format, "FrameBits", &bitlen, 1) ||
       !flipper_format_write_hex(
           flipper_format, "Frame", instance->frame_bytes, RENAULT_V3_FRAME_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_renault_v3_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV3* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_renault_v3_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint32_t serial = 0;
    uint32_t variant = 0;
    uint32_t bitlen = instance->generic.data_count_bit;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Serial", &serial, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Variant", &variant, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "FrameBits", &bitlen, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_hex(flipper_format, "Frame", instance->frame_bytes, RENAULT_V3_FRAME_BYTES);

    instance->serial = (uint16_t)(serial & 0xFFFFU);
    instance->variant = (uint8_t)(variant & 1U);
    instance->frame_bit_len = (uint16_t)bitlen;

    instance->generic.serial = instance->serial;
    instance->generic.btn = 0;
    instance->generic.cnt = 0;
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_renault_v3_decoder = {
    .alloc = subghz_protocol_decoder_renault_v3_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_renault_v3_feed,
    .reset = subghz_protocol_decoder_renault_v3_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_renault_v3_serialize,
    .deserialize = subghz_protocol_decoder_renault_v3_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_renault_v3 = {
    .name = RENAULT_V3_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_renault_v3_decoder,
    .encoder = NULL,
};
