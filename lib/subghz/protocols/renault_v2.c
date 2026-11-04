#include "renault_v2.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"

#define TAG "RenaultV2"

// Renault V2 (Clio 3) -- ported from firmware renault_v2.c via KeeLoq-RX arke_renault_v123.py.
// OOK Manchester, te 230/460. 82-bit frame = [sync 0x71EA :16][encrypted :66].
// The ENTIRE payload is encrypted: NO visible serial, NO counter, NO CRC. This is an
// IDENTIFY-ONLY decoder -- it confirms "a Renault V2 fob was seen" and surfaces the raw
// 82-bit key, nothing clonable. Decode-only; .encoder = NULL.
//
// Bit engine = custom stateless half-bit accumulator ("Ansatz C"): a LONG emits one bit
// equal to its level; a SHORT pairs with the next SHORT and emits one bit equal to the
// level of the 2nd SHORT. The 16-bit constant sync word is then searched in the bit buffer.
// False-positive defences: exact 16-bit sync + strict frame-length gate (full frame must be
// present after the sync, rejecting over-long foreign/mis-synced captures).

#define RENAULT_V2_DATA_BITS   82
#define RENAULT_V2_MIN_PROCESS 60
#define RENAULT_V2_ARR_SIZE    200
#define RENAULT_V2_OVERFLOW    190
#define RENAULT_V2_LEN_TOL     4
#define RENAULT_V2_SYNC        0x71EAU

static const SubGhzBlockConst subghz_protocol_renault_v2_const = {
    .te_short = 230,
    .te_long = 460,
    .te_delta = 60,
    .min_count_bit_for_found = RENAULT_V2_DATA_BITS,
};

// sync 0x71EA = 0111 0001 1110 1010 (MSB-first)
static const uint8_t renault_v2_sync_bits[16] =
    {0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 0, 1, 0, 1, 0};

typedef enum {
    RenaultV2StepReset = 0,
    RenaultV2StepCollect = 1,
} RenaultV2Step;

struct SubGhzProtocolDecoderRenaultV2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t step;
    bool half_bit;
    uint8_t all_bits[RENAULT_V2_ARR_SIZE];
    uint16_t all_bit_count;

    uint32_t data_tail; // low 18 bits of the 82-bit frame (bits 64..81)
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderRenaultV2);


// Mirror of arke_renault_v123._RenaultManchesterBase._process() for V2 (no serial).
static void renault_v2_process(SubGhzProtocolDecoderRenaultV2* instance) {
    const uint8_t* bits = instance->all_bits;
    int n = (int)instance->all_bit_count;
    if(n < RENAULT_V2_MIN_PROCESS) return;

    int search_end = n - RENAULT_V2_MIN_PROCESS; // firmware: all_bit_count - MIN_PROCESS
    int sync_pos = -1;
    for(int i = 0; i <= search_end; i++) {
        bool match = true;
        for(int j = 0; j < 16; j++) {
            if(bits[i + j] != renault_v2_sync_bits[j]) {
                match = false;
                break;
            }
        }
        if(match) {
            sync_pos = i;
            break;
        }
    }
    if(sync_pos < 0) return;

    int data_after = n - sync_pos;
    // Exact-length gate (hardening): full frame must be present, reject over-long.
    if(data_after < RENAULT_V2_DATA_BITS || data_after > RENAULT_V2_DATA_BITS + RENAULT_V2_LEN_TOL)
        return;

    int data_len = data_after < RENAULT_V2_DATA_BITS ? data_after : RENAULT_V2_DATA_BITS;

    // Build the 82-bit frame: top 64 bits -> generic.data, low 18 bits -> data_tail.
    uint64_t gdata = 0;
    int top = data_len < 64 ? data_len : 64;
    for(int i = 0; i < top; i++) gdata = (gdata << 1) | (bits[sync_pos + i] & 1);
    uint32_t tail = 0;
    for(int i = 64; i < data_len; i++) tail = (tail << 1) | (bits[sync_pos + i] & 1);

    instance->data_tail = tail;
    // V2: entire payload encrypted -> no serial to surface (firmware sets serial = 0).
    instance->generic.data = gdata;
    instance->generic.data_count_bit = RENAULT_V2_DATA_BITS;
    instance->generic.serial = 0;
    instance->generic.btn = 0;
    instance->generic.cnt = 0;
    instance->decoder.decode_data = gdata;
    instance->decoder.decode_count_bit = RENAULT_V2_DATA_BITS;

    if(instance->base.callback)
        instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_renault_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderRenaultV2* instance = calloc(1, sizeof(SubGhzProtocolDecoderRenaultV2));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_renault_v2;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_renault_v2_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV2* instance = context;
    instance->step = RenaultV2StepReset;
    instance->half_bit = false;
    instance->all_bit_count = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_renault_v2_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV2* instance = context;
    uint32_t ts = subghz_protocol_renault_v2_const.te_short;
    uint32_t tl = subghz_protocol_renault_v2_const.te_long;
    uint32_t d = subghz_protocol_renault_v2_const.te_delta;

    if(instance->step == RenaultV2StepReset) {
        if(DURATION_DIFF(duration, ts) <= d) {
            instance->step = RenaultV2StepCollect;
            instance->all_bit_count = 0;
            instance->half_bit = true;
        }
        return;
    }

    // RenaultV2StepCollect
    if(instance->all_bit_count >= RENAULT_V2_OVERFLOW) {
        renault_v2_process(instance);
        instance->step = RenaultV2StepReset;
        return;
    }

    bool is_short = DURATION_DIFF(duration, ts) <= d;
    bool is_long = DURATION_DIFF(duration, tl) <= d;

    if(is_long) {
        if(instance->half_bit) {
            // LONG after a half-bit = Manchester error -> process & reset
            renault_v2_process(instance);
            instance->step = RenaultV2StepReset;
        } else {
            // LONG = complete bit, value = level
            if(instance->all_bit_count < RENAULT_V2_ARR_SIZE)
                instance->all_bits[instance->all_bit_count++] = level ? 1 : 0;
        }
    } else if(is_short) {
        if(instance->half_bit) {
            // second SHORT completes a bit, value = level of the 2nd SHORT
            if(instance->all_bit_count < RENAULT_V2_ARR_SIZE)
                instance->all_bits[instance->all_bit_count++] = level ? 1 : 0;
            instance->half_bit = false;
        } else {
            instance->half_bit = true;
        }
    } else {
        // invalid timing -> process frame & reset
        renault_v2_process(instance);
        instance->step = RenaultV2StepReset;
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_renault_v2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV2* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t tail = instance->data_tail;
    if(!flipper_format_write_uint32(flipper_format, "Data2", &tail, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_renault_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultV2* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_renault_v2_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint32_t tail = 0;
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Data2", &tail, 1)) {
        instance->data_tail = tail & 0x0003FFFFU;
    } else {
        instance->data_tail = 0;
    }

    instance->generic.serial = 0;
    instance->generic.btn = 0;
    instance->generic.cnt = 0;
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_renault_v2_decoder = {
    .alloc = subghz_protocol_decoder_renault_v2_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_renault_v2_feed,
    .reset = subghz_protocol_decoder_renault_v2_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_renault_v2_serialize,
    .deserialize = subghz_protocol_decoder_renault_v2_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_renault_v2 = {
    .name = RENAULT_V2_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_renault_v2_decoder,
    .encoder = NULL,
};
