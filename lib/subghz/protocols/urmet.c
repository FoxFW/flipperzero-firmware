#include "urmet.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

#include <string.h>

#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolUrmet"

// PPM/USART @ 2000 baud (fixed high = 1*te). Live lock: Manchester block →
// ~34ms gap → PPM preamble H1 L1 H1 L11. Do not key off remote serial bytes.
#define URMET_TE              500
#define URMET_TE_DELTA        150
#define URMET_SYNC_TE         11
#define URMET_SYNC_DELTA_TE   3
#define URMET_END_GAP_US      20000
#define URMET_PAUSE_TE        68 // ~34 ms trailing low on TX
#define URMET_MAN_EDGE_MIN_US 40
#define URMET_MAN_EDGE_MAX_US 280
#define URMET_MAN_MIN_EDGES   16
#define URMET_MAN_GAP_MAX_US  60000
#define URMET_MIN_BYTES       16
#define URMET_MAX_BYTES       40
#define URMET_MAX_GAPS        192
#define URMET_BITBUF          512
#define URMET_UPLOAD_MAX      (8 + URMET_MAX_GAPS * 2)
#define URMET_RAW_FIELD       "Raw"
#define URMET_GAPS_FIELD      "Gaps"

typedef enum {
    UrmetStepReset = 0,
    UrmetStepManchester, // collecting short (~100us) Manchester edges
    UrmetStepArmed, // saw Manchester + inter-block gap; wait PPM preamble
    UrmetStepPreambleLow1, // saw H~1te, expect L~1te
    UrmetStepPreambleHigh2, // expect H~1te
    UrmetStepPreambleSync, // expect L~11te sync
    UrmetStepData, // locked: H~1te + variable low gaps
} UrmetDecoderStep;

struct SubGhzProtocolDecoderUrmet {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    UrmetDecoderStep step;
    bool pending_high;
    uint16_t man_edges; // Manchester short-edge count before arming
    uint16_t bit_cnt;
    uint8_t bit_buf[URMET_BITBUF];

    // In-progress collection (may be aborted).
    uint8_t rx_gap_count;
    uint8_t rx_gaps[URMET_MAX_GAPS];

    // Last accepted frame (survives abort / serialize).
    uint8_t gap_count;
    uint8_t gaps[URMET_MAX_GAPS];
    uint8_t raw_bytes[URMET_MAX_BYTES];
    uint8_t byte_count;
};

struct SubGhzProtocolEncoderUrmet {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_bytes[URMET_MAX_BYTES];
    uint8_t byte_count;

    uint8_t gap_count;
    uint8_t gaps[URMET_MAX_GAPS];
};

const SubGhzProtocolDecoder subghz_protocol_urmet_decoder = {
    .alloc = subghz_protocol_decoder_urmet_alloc,
    .free = subghz_protocol_decoder_urmet_free,

    .feed = subghz_protocol_decoder_urmet_feed,
    .reset = subghz_protocol_decoder_urmet_reset,

    .get_hash_data = subghz_protocol_decoder_urmet_get_hash_data,
    .serialize = subghz_protocol_decoder_urmet_serialize,
    .deserialize = subghz_protocol_decoder_urmet_deserialize,
    .get_string = subghz_protocol_decoder_urmet_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_urmet_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_urmet");

const SubGhzProtocol subghz_protocol_urmet = {
    .name = SUBGHZ_PROTOCOL_URMET_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_urmet_decoder,
    .encoder = &subghz_protocol_urmet_encoder_ext.vtable,
};

// ---------------------------------------------------------------- Shared helpers

static bool urmet_is_manchester_edge(uint32_t duration) {
    return duration >= URMET_MAN_EDGE_MIN_US && duration <= URMET_MAN_EDGE_MAX_US;
}

static bool urmet_is_te1(uint32_t duration) {
    return DURATION_DIFF(duration, (uint32_t)URMET_TE) < (uint32_t)URMET_TE_DELTA;
}

static bool urmet_is_sync_low(uint32_t duration) {
    return DURATION_DIFF(duration, (uint32_t)URMET_TE * URMET_SYNC_TE) <
           (uint32_t)URMET_TE * URMET_SYNC_DELTA_TE;
}

static uint32_t urmet_te_count(uint32_t duration) {
    uint32_t n = (duration + URMET_TE / 2) / URMET_TE;
    if(n < 1) n = 1;
    if(n > 20) n = 20;
    return n;
}

static uint64_t urmet_bytes_to_key(const uint8_t* bytes, uint8_t count) {
    uint64_t key = 0;
    uint8_t n = count < 8 ? count : 8;
    for(uint8_t i = 0; i < n; i++) {
        key = (key << 8) | bytes[i];
    }
    if(n < 8) {
        key <<= (uint8_t)((8 - n) * 8);
    }
    return key;
}

static void urmet_key_to_bytes(uint64_t key, uint8_t* bytes, uint8_t count) {
    uint8_t n = count < 8 ? count : 8;
    for(uint8_t i = 0; i < n; i++) {
        bytes[i] = (uint8_t)(key >> (56 - i * 8));
    }
}

static bool urmet_nibble_ok(uint8_t b) {
    uint8_t hi = (b >> 4) & 0x0F;
    uint8_t lo = b & 0x0F;
    return (hi == 0x5 || hi == 0x7 || hi == 0xD || hi == 0xF) &&
           (lo == 0x5 || lo == 0x7 || lo == 0xD || lo == 0xF);
}

static bool urmet_payload_plausible(const uint8_t* bytes, uint8_t count) {
    if(count < URMET_MIN_BYTES || count > URMET_MAX_BYTES) return false;
    // Encoding alphabet only (5/7/D/F). Do not require sample-remote bytes
    // (serial / header values differ between remotes).
    for(uint8_t i = 0; i < count; i++) {
        if(!urmet_nibble_ok(bytes[i])) return false;
    }
    return true;
}

static bool urmet_byte_count_ok(uint8_t byte_count) {
    return byte_count >= URMET_MIN_BYTES && byte_count <= URMET_MAX_BYTES;
}

// Decode inverted USART 8N1 (idle low, start high, LSB first, wire-inverted data).
static uint8_t
    urmet_decode_inv_usart(const uint8_t* bits, uint16_t bit_cnt, uint8_t* out, uint8_t out_max) {
    uint8_t n = 0;
    uint16_t i = 0;
    while(i < bit_cnt && n < out_max) {
        if(bits[i] != 1) {
            i++;
            continue;
        }
        if(i > 0 && bits[i - 1] != 0) {
            i++;
            continue;
        }
        if((uint16_t)(i + 9) >= bit_cnt) break;

        uint8_t val = 0;
        for(uint8_t b = 0; b < 8; b++) {
            uint8_t wire = bits[i + 1 + b];
            val |= (uint8_t)((wire ? 0 : 1) << b);
        }
        if(bits[i + 9] != 0) {
            i++;
            continue;
        }
        out[n++] = val;
        i += 10;
        while(i < bit_cnt && bits[i] == 0)
            i++;
    }
    return n;
}

static void urmet_append_bits(uint8_t* bit_buf, uint16_t* bit_cnt, bool level, uint32_t n) {
    for(uint32_t i = 0; i < n && *bit_cnt < URMET_BITBUF; i++) {
        bit_buf[(*bit_cnt)++] = level ? 1 : 0;
    }
}

static bool urmet_append_gap(uint8_t* gaps, uint8_t* gap_count, uint8_t gap_te) {
    if(*gap_count >= URMET_MAX_GAPS) return false;
    gaps[(*gap_count)++] = gap_te;
    return true;
}

// Build gap list from USART bytes (fallback when Gaps field missing).
static uint8_t urmet_gaps_from_bytes(const uint8_t* bytes, uint8_t byte_count, uint8_t* gaps) {
    // Bit stream -> RLE lows after each high run of 1*te (highs always split to 1).
    uint8_t tmp_bits[URMET_BITBUF];
    uint16_t bit_cnt = 0;
    // Leading idle not stored as a gap; first edge is start high of byte 0.
    for(uint8_t bi = 0; bi < byte_count && bit_cnt + 10 < URMET_BITBUF; bi++) {
        uint8_t val = bytes[bi];
        tmp_bits[bit_cnt++] = 1; // start
        for(uint8_t b = 0; b < 8; b++) {
            bool logical = (val >> b) & 1;
            tmp_bits[bit_cnt++] = logical ? 0 : 1;
        }
        tmp_bits[bit_cnt++] = 0; // stop
    }

    uint8_t gap_count = 0;
    uint16_t i = 0;
    while(i < bit_cnt && gap_count < URMET_MAX_GAPS) {
        if(tmp_bits[i] != 1) {
            i++;
            continue;
        }
        // Consume exactly one high te (USART highs are 1).
        i++;
        uint8_t low = 0;
        while(i < bit_cnt && tmp_bits[i] == 0 && low < 20) {
            low++;
            i++;
        }
        if(low == 0) low = 1;
        gaps[gap_count++] = low;
    }
    return gap_count;
}

static bool urmet_load_from_ff(
    FlipperFormat* flipper_format,
    uint8_t* raw_bytes,
    uint8_t* byte_count,
    uint8_t* gaps,
    uint8_t* gap_count,
    uint16_t data_count_bit,
    uint64_t key) {
    uint32_t raw_len = 0;
    flipper_format_rewind(flipper_format);
    if(flipper_format_get_value_count(flipper_format, URMET_RAW_FIELD, &raw_len) && raw_len > 0) {
        if(raw_len > URMET_MAX_BYTES) {
            FURI_LOG_E(TAG, "Raw too long: %lu", (unsigned long)raw_len);
            return false;
        }
        memset(raw_bytes, 0, URMET_MAX_BYTES);
        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_hex(flipper_format, URMET_RAW_FIELD, raw_bytes, raw_len)) {
            FURI_LOG_E(TAG, "Failed to read Raw");
            return false;
        }
        *byte_count = (uint8_t)raw_len;
    } else {
        if(data_count_bit < URMET_MIN_BYTES * 8 || data_count_bit > URMET_MAX_BYTES * 8 ||
           (data_count_bit % 8) != 0) {
            return false;
        }
        *byte_count = (uint8_t)(data_count_bit / 8);
        memset(raw_bytes, 0, URMET_MAX_BYTES);
        urmet_key_to_bytes(key, raw_bytes, *byte_count);
    }

    if(!urmet_byte_count_ok(*byte_count)) return false;

    uint32_t gaps_len = 0;
    flipper_format_rewind(flipper_format);
    if(flipper_format_get_value_count(flipper_format, URMET_GAPS_FIELD, &gaps_len) &&
       gaps_len > 0) {
        if(gaps_len > URMET_MAX_GAPS) {
            FURI_LOG_E(TAG, "Gaps too long: %lu", (unsigned long)gaps_len);
            return false;
        }
        memset(gaps, 0, URMET_MAX_GAPS);
        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_hex(flipper_format, URMET_GAPS_FIELD, gaps, gaps_len)) {
            FURI_LOG_E(TAG, "Failed to read Gaps");
            return false;
        }
        *gap_count = (uint8_t)gaps_len;
    } else {
        *gap_count = urmet_gaps_from_bytes(raw_bytes, *byte_count, gaps);
    }

    return *gap_count >= 2;
}

// ---------------------------------------------------------------- Decoder

void* subghz_protocol_decoder_urmet_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderUrmet* instance = malloc(sizeof(SubGhzProtocolDecoderUrmet));
    instance->base.protocol = &subghz_protocol_urmet;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->step = UrmetStepReset;
    instance->pending_high = false;
    instance->man_edges = 0;
    instance->bit_cnt = 0;
    instance->rx_gap_count = 0;
    instance->gap_count = 0;
    instance->byte_count = 0;
    memset(instance->raw_bytes, 0, sizeof(instance->raw_bytes));
    memset(instance->gaps, 0, sizeof(instance->gaps));
    memset(instance->rx_gaps, 0, sizeof(instance->rx_gaps));
    return instance;
}

void subghz_protocol_decoder_urmet_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderUrmet* instance = context;
    free(instance);
}

void subghz_protocol_decoder_urmet_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderUrmet* instance = context;
    instance->step = UrmetStepReset;
    instance->pending_high = false;
    instance->man_edges = 0;
    instance->bit_cnt = 0;
    // Keep last decoded raw_bytes / gaps for serialize after a successful read.
}

static void urmet_decoder_abort(SubGhzProtocolDecoderUrmet* instance) {
    instance->step = UrmetStepReset;
    instance->pending_high = false;
    instance->man_edges = 0;
    instance->bit_cnt = 0;
    instance->rx_gap_count = 0;
    // Do not clear last accepted raw_bytes / gaps.
}

static void subghz_protocol_decoder_urmet_finish(SubGhzProtocolDecoderUrmet* instance) {
    uint8_t decoded[URMET_MAX_BYTES];
    uint8_t n =
        urmet_decode_inv_usart(instance->bit_buf, instance->bit_cnt, decoded, URMET_MAX_BYTES);

    if(urmet_payload_plausible(decoded, n) && instance->rx_gap_count >= 2) {
        memcpy(instance->raw_bytes, decoded, n);
        instance->byte_count = n;
        memcpy(instance->gaps, instance->rx_gaps, instance->rx_gap_count);
        instance->gap_count = instance->rx_gap_count;
        instance->generic.data = urmet_bytes_to_key(decoded, n);
        instance->generic.data_count_bit = (uint16_t)n * 8;
        instance->decoder.decode_data = instance->generic.data;
        instance->decoder.decode_count_bit = instance->generic.data_count_bit;

        FURI_LOG_I(
            TAG,
            "DECODED %u bytes gaps=%u Key=%02X%02X%02X%02X%02X%02X%02X%02X",
            n,
            instance->gap_count,
            decoded[0],
            decoded[1],
            decoded[2],
            decoded[3],
            decoded[4],
            decoded[5],
            decoded[6],
            decoded[7]);

        if(instance->base.callback)
            instance->base.callback(&instance->base, instance->base.context);
    } else {
        FURI_LOG_D(
            TAG, "reject: bits=%u bytes=%u gaps=%u", instance->bit_cnt, n, instance->rx_gap_count);
    }

    urmet_decoder_abort(instance);
}

void subghz_protocol_decoder_urmet_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderUrmet* instance = context;

    // Long lows: end of PPM data, or Manchester→PPM inter-block gap.
    if(!level && duration >= URMET_END_GAP_US) {
        if(instance->step == UrmetStepData) {
            if(instance->bit_cnt < URMET_BITBUF) {
                instance->bit_buf[instance->bit_cnt++] = 0;
            }
            subghz_protocol_decoder_urmet_finish(instance);
        } else if(
            instance->step == UrmetStepManchester && instance->man_edges >= URMET_MAN_MIN_EDGES &&
            duration <= URMET_MAN_GAP_MAX_US) {
            // Manchester block done → arm for PPM preamble.
            instance->step = UrmetStepArmed;
            instance->man_edges = 0;
            instance->pending_high = false;
        } else {
            urmet_decoder_abort(instance);
        }
        return;
    }

    switch(instance->step) {
    case UrmetStepReset:
        if(urmet_is_manchester_edge(duration)) {
            instance->step = UrmetStepManchester;
            instance->man_edges = 1;
        }
        break;

    case UrmetStepManchester:
        if(urmet_is_manchester_edge(duration)) {
            if(instance->man_edges < 0xFFF0) instance->man_edges++;
        } else if(urmet_is_te1(duration)) {
            // PPM-sized edge too early — not our sequence.
            urmet_decoder_abort(instance);
        } else {
            // Ignore mild timing noise inside Manchester; abort on big outliers.
            if(duration > URMET_MAN_EDGE_MAX_US * 2 && duration < URMET_END_GAP_US) {
                urmet_decoder_abort(instance);
            }
        }
        break;

    case UrmetStepArmed:
        // After Manchester+gap, only accept the PPM wake high.
        if(level && urmet_is_te1(duration)) {
            instance->step = UrmetStepPreambleLow1;
        } else if(urmet_is_manchester_edge(duration)) {
            // Another Manchester burst instead of PPM.
            instance->step = UrmetStepManchester;
            instance->man_edges = 1;
        }
        break;

    case UrmetStepPreambleLow1:
        if(!level && urmet_is_te1(duration)) {
            instance->step = UrmetStepPreambleHigh2;
        } else if(level && urmet_is_te1(duration)) {
            instance->step = UrmetStepPreambleLow1;
        } else {
            urmet_decoder_abort(instance);
        }
        break;

    case UrmetStepPreambleHigh2:
        if(level && urmet_is_te1(duration)) {
            instance->step = UrmetStepPreambleSync;
        } else {
            urmet_decoder_abort(instance);
        }
        break;

    case UrmetStepPreambleSync:
        if(!level && urmet_is_sync_low(duration)) {
            // Locked. Seed bit/gap buffers with preamble H1 L1 H1 L11.
            instance->bit_cnt = 0;
            instance->rx_gap_count = 0;
            instance->pending_high = false;
            urmet_append_bits(instance->bit_buf, &instance->bit_cnt, true, 1);
            urmet_append_bits(instance->bit_buf, &instance->bit_cnt, false, 1);
            urmet_append_bits(instance->bit_buf, &instance->bit_cnt, true, 1);
            urmet_append_bits(instance->bit_buf, &instance->bit_cnt, false, URMET_SYNC_TE);
            urmet_append_gap(instance->rx_gaps, &instance->rx_gap_count, 1);
            urmet_append_gap(instance->rx_gaps, &instance->rx_gap_count, URMET_SYNC_TE);
            instance->step = UrmetStepData;
        } else if(level && urmet_is_te1(duration)) {
            instance->step = UrmetStepPreambleLow1;
        } else {
            urmet_decoder_abort(instance);
        }
        break;

    case UrmetStepData:
        if(level) {
            if(!urmet_is_te1(duration) || instance->pending_high) {
                urmet_decoder_abort(instance);
                break;
            }
            instance->pending_high = true;
            break;
        }

        if(!instance->pending_high) {
            if(duration > (uint32_t)URMET_TE * 2) {
                urmet_decoder_abort(instance);
            }
            break;
        }
        instance->pending_high = false;

        {
            uint32_t gap = urmet_te_count(duration);
            if(gap < 1 || gap > 15) {
                urmet_decoder_abort(instance);
                break;
            }
            if(!urmet_append_gap(instance->rx_gaps, &instance->rx_gap_count, (uint8_t)gap)) {
                subghz_protocol_decoder_urmet_finish(instance);
                break;
            }
            urmet_append_bits(instance->bit_buf, &instance->bit_cnt, true, 1);
            urmet_append_bits(instance->bit_buf, &instance->bit_cnt, false, gap);

            if(instance->bit_cnt >= URMET_BITBUF || instance->rx_gap_count >= URMET_MAX_GAPS) {
                subghz_protocol_decoder_urmet_finish(instance);
            }
        }
        break;

    default:
        urmet_decoder_abort(instance);
        break;
    }
}

uint8_t subghz_protocol_decoder_urmet_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderUrmet* instance = context;
    uint8_t hash = 0;
    for(uint8_t i = 0; i < instance->byte_count; i++) {
        hash ^= instance->raw_bytes[i];
    }
    for(uint8_t i = 0; i < instance->gap_count; i++) {
        hash ^= instance->gaps[i];
    }
    hash ^= instance->byte_count;
    hash ^= instance->gap_count;
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_urmet_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderUrmet* instance = context;

    if(!urmet_byte_count_ok(instance->byte_count) || instance->gap_count < 2) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    instance->generic.data = urmet_bytes_to_key(instance->raw_bytes, instance->byte_count);
    instance->generic.data_count_bit = (uint16_t)instance->byte_count * 8;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_insert_or_update_hex(
           flipper_format, URMET_RAW_FIELD, instance->raw_bytes, instance->byte_count)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    flipper_format_rewind(flipper_format);
    if(!flipper_format_insert_or_update_hex(
           flipper_format, URMET_GAPS_FIELD, instance->gaps, instance->gap_count)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_urmet_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderUrmet* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    if(!urmet_load_from_ff(
           flipper_format,
           instance->raw_bytes,
           &instance->byte_count,
           instance->gaps,
           &instance->gap_count,
           instance->generic.data_count_bit,
           instance->generic.data)) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }

    instance->generic.data_count_bit = (uint16_t)instance->byte_count * 8;
    instance->generic.data = urmet_bytes_to_key(instance->raw_bytes, instance->byte_count);
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return SubGhzProtocolStatusOk;
}

void subghz_protocol_decoder_urmet_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderUrmet* instance = context;
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}

