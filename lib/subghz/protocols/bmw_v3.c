#include "bmw_v3.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"

#define TAG "BmwV3"

// BMW/Mini V3 -- ported from firmware bmw_v3.c via KeeLoq-RX arke_bmw.py.
// 2-FSK Manchester, te 95/190 us, delta 40, inter-frame gap > 350 us.
// Frame = 0xCC header byte + 64-bit payload. te_last half-period accumulator
// (bit value = INVERSE of the completing pulse level, like BMW V2). The header
// is located by sliding an 8-bit window; on a 0xCC match the accumulator resets
// and the next 64 bits are the payload. CLEARTEXT device serial + button are
// recoverable; the 40-bit rolling code is AES-encrypted (no counter / next-code
// -- structural, like GM). Only meaningful on 2-FSK captures. Decode-only;
// .encoder = NULL.

#define BMW_V3_DATA_BITS 64
#define BMW_V3_HEADER 0xCCU
#define BMW_V3_GAP 350

static const SubGhzBlockConst subghz_protocol_bmw_v3_const = {
    .te_short = 95,
    .te_long = 190,
    .te_delta = 40,
    .min_count_bit_for_found = BMW_V3_DATA_BITS,
};

struct SubGhzProtocolDecoderBmwV3 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t step; // 0 reset, 1 waitgap, 2 findheader, 3 collectdata
    uint64_t data;
    uint16_t nbit;
    uint32_t te_last;

    uint16_t serial; // 16-bit cleartext serial
    uint8_t btn;
    uint16_t cnt; // 16-bit cleartext counter field
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderBmwV3);


typedef enum {
    BmwV3StepReset = 0,
    BmwV3StepWaitGap = 1,
    BmwV3StepFindHeader = 2,
    BmwV3StepCollectData = 3,
} BmwV3Step;

static bool bmw_v3_button_valid(uint8_t btn) {
    return btn == 0x02 || btn == 0x04 || btn == 0x08 || btn == 0x10;
}

// Mirror of arke BmwV3._bit: accumulate one bit, drive the header search / data
// collection state machine.
static void bmw_v3_add_bit(SubGhzProtocolDecoderBmwV3* instance, uint8_t bit) {
    instance->data = (instance->data << 1) | (bit & 1U);
    instance->nbit++;

    if(instance->step == BmwV3StepFindHeader) {
        if(instance->nbit >= 8 && (uint8_t)(instance->data & 0xFFU) == BMW_V3_HEADER) {
            instance->data = 0;
            instance->nbit = 0;
            instance->step = BmwV3StepCollectData;
        } else if(instance->nbit > 32) {
            instance->step = BmwV3StepReset;
        }
    } else if(instance->step == BmwV3StepCollectData) {
        if(instance->nbit == 64) {
            uint64_t data = instance->data;
            uint8_t btn = (uint8_t)((data >> 56) & 0xFFU);
            if(bmw_v3_button_valid(btn)) {
                uint16_t serial =
                    (uint16_t)((((data >> 48) & 0xFFU) << 8) | ((data >> 40) & 0xFFU));
                uint16_t cnt = (uint16_t)((data >> 16) & 0xFFFFU);

                instance->serial = serial;
                instance->btn = btn;
                instance->cnt = cnt;

                instance->generic.data = data;
                instance->generic.data_count_bit = BMW_V3_DATA_BITS;
                instance->generic.serial = serial;
                instance->generic.btn = btn;
                instance->generic.cnt = cnt;
                instance->decoder.decode_data = data;
                instance->decoder.decode_count_bit = BMW_V3_DATA_BITS;

                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
            }
            instance->step = BmwV3StepReset;
        }
    }
}

void* subghz_protocol_decoder_bmw_v3_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderBmwV3* instance = calloc(1, sizeof(SubGhzProtocolDecoderBmwV3));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_bmw_v3;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_bmw_v3_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderBmwV3* instance = context;
    instance->step = BmwV3StepReset;
    instance->data = 0;
    instance->nbit = 0;
    instance->te_last = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_bmw_v3_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderBmwV3* instance = context;
    uint32_t te_s = subghz_protocol_bmw_v3_const.te_short;
    uint32_t te_l = subghz_protocol_bmw_v3_const.te_long;
    uint32_t te_d = subghz_protocol_bmw_v3_const.te_delta;

    if(instance->step == BmwV3StepReset) {
        if((!level) && duration > BMW_V3_GAP) {
            instance->step = BmwV3StepWaitGap;
            instance->data = 0;
            instance->nbit = 0;
            instance->te_last = 0;
        }
    } else if(instance->step == BmwV3StepWaitGap) {
        if(level) {
            if(DURATION_DIFF(duration, te_s) <= te_d || DURATION_DIFF(duration, te_l) <= te_d) {
                instance->te_last = duration;
                instance->step = BmwV3StepFindHeader;
            } else {
                instance->step = BmwV3StepReset;
            }
        } else {
            if(duration <= BMW_V3_GAP) instance->step = BmwV3StepReset;
        }
    } else if(instance->step == BmwV3StepFindHeader || instance->step == BmwV3StepCollectData) {
        bool is_short = DURATION_DIFF(duration, te_s) <= te_d;
        bool is_long = DURATION_DIFF(duration, te_l) <= te_d;
        if(!is_short && !is_long) {
            instance->step = BmwV3StepReset;
            if((!level) && duration > BMW_V3_GAP) {
                instance->step = BmwV3StepWaitGap;
                instance->data = 0;
                instance->nbit = 0;
                instance->te_last = 0;
            }
            return;
        }
        if(is_short) {
            if(instance->te_last == 0) {
                instance->te_last = duration;
            } else {
                bmw_v3_add_bit(instance, level ? 0 : 1);
                instance->te_last = 0;
            }
        } else { // is_long
            if(instance->te_last != 0) {
                bmw_v3_add_bit(instance, level ? 0 : 1);
                instance->te_last = te_s;
            } else {
                bmw_v3_add_bit(instance, level ? 0 : 1);
                instance->te_last = 0;
            }
        }
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_bmw_v3_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderBmwV3* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t serial = instance->serial;
    uint32_t btn = instance->btn;
    uint32_t cnt = instance->cnt;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "Btn", &btn, 1) ||
       !flipper_format_write_uint32(flipper_format, "Cnt", &cnt, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_bmw_v3_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderBmwV3* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_bmw_v3_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint64_t data = instance->generic.data;
    instance->btn = (uint8_t)((data >> 56) & 0xFFU);
    instance->serial = (uint16_t)((((data >> 48) & 0xFFU) << 8) | ((data >> 40) & 0xFFU));
    instance->cnt = (uint16_t)((data >> 16) & 0xFFFFU);

    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->btn;
    instance->generic.cnt = instance->cnt;
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_bmw_v3_decoder = {
    .alloc = subghz_protocol_decoder_bmw_v3_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_bmw_v3_feed,
    .reset = subghz_protocol_decoder_bmw_v3_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_bmw_v3_serialize,
    .deserialize = subghz_protocol_decoder_bmw_v3_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_bmw_v3 = {
    .name = BMW_V3_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    // 2-FSK -> add SubGhzProtocolFlag_FM
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_bmw_v3_decoder,
    .encoder = NULL,
};
