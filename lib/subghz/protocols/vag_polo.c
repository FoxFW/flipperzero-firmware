#include "vag_polo.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include "common.h"

#define TAG "VagPolo"

// VAG Polo -- ported from firmware vag_polo.c via KeeLoq-RX arke_vag.py (class VagPolo).
// OOK/ASK, Manchester te_short=300 / te_long=590. The decoder runs the standard
// Flipper Manchester (manchester_advance) continuously: it waits for a run of >=32
// cleartext '1' preamble bits, then the 16-bit sync 0x3F91, then collects an 80-bit
// (10 byte) payload = Button(8) | Serial(32) | rolling(40). The button high nibble
// is cleartext (0x7 = Unlock, 0xB = Lock); the rolling part is encrypted -> no clone.
// Decode-only, structural lock (no CRC); .encoder = NULL.

#define VAG_POLO_TE_SHORT    300
#define VAG_POLO_TE_LONG     590
#define VAG_POLO_TE_DELTA    130
#define VAG_POLO_EVENT_TOL   (VAG_POLO_TE_DELTA + 20) // 150, mirrors arke _event tol
#define VAG_POLO_SYNC        0x3F91
#define VAG_POLO_PRE_ONES    32
#define VAG_POLO_DATA_BITS   80
#define VAG_POLO_BYTES       10

static const SubGhzBlockConst subghz_protocol_vag_polo_const = {
    .te_short = VAG_POLO_TE_SHORT,
    .te_long = VAG_POLO_TE_LONG,
    .te_delta = VAG_POLO_TE_DELTA,
    .min_count_bit_for_found = VAG_POLO_DATA_BITS,
};

struct SubGhzProtocolDecoderVagPolo {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint8_t parser_step;
    uint16_t sync_reg;
    uint16_t pre_ones;
    uint8_t payload[VAG_POLO_BYTES];
    uint16_t pbits;

    uint32_t serial;
    uint8_t btn; // 1 = Unlock, 2 = Lock
    uint8_t button_raw; // raw payload[0]
    uint16_t hdr16; // top 16 bits of the 80-bit frame (payload[0..1])
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderVagPolo);


typedef enum {
    VagPoloStepReset = 0,
    VagPoloStepPreamble = 5,
    VagPoloStepData = 6,
} VagPoloStep;

// Manchester event classification -- "core" polarity (level -> ShortLow),
// mirrors arke VagPolo._event().
static ManchesterEvent vag_polo_event(uint32_t duration, bool level) {
    if(DURATION_DIFF(duration, VAG_POLO_TE_SHORT) < VAG_POLO_EVENT_TOL) {
        return level ? ManchesterEventShortLow : ManchesterEventShortHigh;
    }
    if(DURATION_DIFF(duration, VAG_POLO_TE_LONG) < VAG_POLO_EVENT_TOL) {
        return level ? ManchesterEventLongLow : ManchesterEventLongHigh;
    }
    return ManchesterEventReset;
}

static void vag_polo_rearm(SubGhzProtocolDecoderVagPolo* instance) {
    instance->parser_step = VagPoloStepReset;
    instance->manchester_state = ManchesterStateMid1;
    instance->sync_reg = 0;
    instance->pre_ones = 0;
    instance->pbits = 0;
    for(uint8_t i = 0; i < VAG_POLO_BYTES; i++) instance->payload[i] = 0;
}

static void vag_polo_commit(SubGhzProtocolDecoderVagPolo* instance) {
    uint8_t* p = instance->payload;
    uint8_t button = p[0];
    uint8_t hi = (button >> 4) & 0x0F;
    // code = payload[1..9] (9 bytes); serial = code[0..3] = payload[1..4]
    uint32_t serial = ((uint32_t)p[1] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 8) |
                      (uint32_t)p[4];
    uint8_t btn = (hi == 0x7) ? 1 : 2;

    // Full 80-bit frame -> ducato storage convention: top 64 in generic.data,
    // remaining 16 bits (payload[8..9]) kept in hdr/decode. Here we store the
    // 64-bit "code" value payload[2..9] that arke reports as `data`.
    uint64_t data = 0;
    for(uint8_t i = 2; i < VAG_POLO_BYTES; i++) data = (data << 8) | p[i];

    instance->serial = serial;
    instance->btn = btn;
    instance->button_raw = button;
    instance->hdr16 = ((uint16_t)p[0] << 8) | p[1];

    instance->generic.data = data; // payload[2..9], 64 bits
    instance->generic.data_count_bit = VAG_POLO_DATA_BITS;
    instance->generic.serial = serial;
    instance->generic.btn = btn;
    instance->generic.cnt = 0; // rolling part encrypted, not a plain counter
    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = VAG_POLO_DATA_BITS;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_vag_polo_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderVagPolo* instance = calloc(1, sizeof(SubGhzProtocolDecoderVagPolo));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_vag_polo;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->manchester_state = ManchesterStateMid1;
    return instance;
}



void subghz_protocol_decoder_vag_polo_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderVagPolo* instance = context;
    vag_polo_rearm(instance);
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_vag_polo_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderVagPolo* instance = context;

    switch(instance->parser_step) {
    case VagPoloStepReset:
        if(level && DURATION_DIFF(duration, VAG_POLO_TE_SHORT) < VAG_POLO_EVENT_TOL) {
            instance->parser_step = VagPoloStepPreamble;
            instance->manchester_state = ManchesterStateMid1;
            instance->sync_reg = 0;
            instance->pre_ones = 0;
        }
        break;

    case VagPoloStepPreamble: {
        ManchesterEvent event = vag_polo_event(duration, level);
        if(event == ManchesterEventReset) {
            instance->parser_step = VagPoloStepReset;
            break;
        }
        bool bit = false;
        if(manchester_advance(
               instance->manchester_state, event, &instance->manchester_state, &bit)) {
            instance->sync_reg = (uint16_t)((instance->sync_reg << 1) | (bit ? 1 : 0));
            if(bit) {
                instance->pre_ones++;
            } else if(instance->pre_ones < VAG_POLO_PRE_ONES) {
                instance->pre_ones = 0;
                instance->sync_reg = 0;
                break;
            }
            if(instance->sync_reg == VAG_POLO_SYNC && instance->pre_ones >= VAG_POLO_PRE_ONES) {
                instance->pbits = 0;
                for(uint8_t i = 0; i < VAG_POLO_BYTES; i++) instance->payload[i] = 0;
                instance->parser_step = VagPoloStepData;
            }
        }
        break;
    }

    case VagPoloStepData: {
        ManchesterEvent event = vag_polo_event(duration, level);
        if(event == ManchesterEventReset) {
            instance->parser_step = VagPoloStepReset;
            break;
        }
        bool bit = false;
        if(manchester_advance(
               instance->manchester_state, event, &instance->manchester_state, &bit)) {
            uint16_t bi = instance->pbits >> 3;
            if(bi < VAG_POLO_BYTES) {
                instance->payload[bi] =
                    (uint8_t)((instance->payload[bi] << 1) | (bit ? 1 : 0));
            }
            instance->pbits++;
            if(instance->pbits >= VAG_POLO_DATA_BITS) {
                uint8_t hi = (instance->payload[0] >> 4) & 0x0F;
                if(hi == 0x7 || hi == 0xB) {
                    vag_polo_commit(instance);
                }
                vag_polo_rearm(instance);
            }
        }
        break;
    }

    default:
        break;
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_vag_polo_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderVagPolo* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t serial = instance->serial;
    uint32_t btn = instance->btn;
    uint32_t button_raw = instance->button_raw;
    uint32_t hdr = instance->hdr16;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "Btn", &btn, 1) ||
       !flipper_format_write_uint32(flipper_format, "ButtonRaw", &button_raw, 1) ||
       !flipper_format_write_uint32(flipper_format, "Hdr", &hdr, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_vag_polo_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderVagPolo* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_vag_polo_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    uint32_t serial = 0, btn = 0, button_raw = 0, hdr = 0;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Serial", &serial, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Btn", &btn, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "ButtonRaw", &button_raw, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Hdr", &hdr, 1);

    instance->serial = serial;
    instance->btn = (uint8_t)btn;
    instance->button_raw = (uint8_t)button_raw;
    instance->hdr16 = (uint16_t)hdr;

    instance->generic.serial = serial;
    instance->generic.btn = (uint8_t)btn;
    instance->generic.cnt = 0;
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

const SubGhzProtocolDecoder subghz_protocol_vag_polo_decoder = {
    .alloc = subghz_protocol_decoder_vag_polo_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_vag_polo_feed,
    .reset = subghz_protocol_decoder_vag_polo_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_vag_polo_serialize,
    .deserialize = subghz_protocol_decoder_vag_polo_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocol subghz_protocol_vag_polo = {
    .name = VAG_POLO_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_vag_polo_decoder,
    .encoder = NULL,
};
