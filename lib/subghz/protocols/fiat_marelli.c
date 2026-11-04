#include "fiat_marelli.h"

#include "fiat_v1.h" // reuse the verified Fiat V1 BCM-Hitag2 authenticator + key dictionary
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <furi_hal_subghz.h>
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

#define TAG "FiatMarelli"

#define FIAT_MARELLI_PREAMBLE_PULSE_MIN 50
#define FIAT_MARELLI_PREAMBLE_PULSE_MAX 350
#define FIAT_MARELLI_PREAMBLE_MIN       80
#define FIAT_MARELLI_MAX_DATA_BITS      104
#define FIAT_MARELLI_MIN_DATA_BITS      80
#define FIAT_MARELLI_GAP_TE_MULT        4
#define FIAT_MARELLI_SYNC_TE_MIN_MULT   4
#define FIAT_MARELLI_SYNC_TE_MAX_MULT   12
#define FIAT_MARELLI_RETX_GAP_MIN       5000
#define FIAT_MARELLI_RETX_SYNC_MIN      400
#define FIAT_MARELLI_RETX_SYNC_MAX      2800
#define FIAT_MARELLI_TE_TYPE_AB_BOUNDARY 180

static uint8_t fiat_marelli_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x03;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x01) : (crc << 1);
        }
    }
    return crc;
}

static const SubGhzBlockConst subghz_protocol_fiat_marelli_const = {
    .te_short = 260,
    .te_long = 520,
    .te_delta = 80,
    .min_count_bit_for_found = 80,
};

struct SubGhzProtocolDecoderFiatMarelli {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    ManchesterState manchester_state;
    uint8_t decoder_state;
    uint16_t preamble_count;
    uint8_t raw_data[13];
    uint8_t bit_count;
    uint32_t extra_data;
    uint32_t te_last;
    uint32_t te_sum;
    uint16_t te_count;
    uint32_t te_detected;
    // Cached result of the Hitag2 manufacturer-key dictionary pass (computed
    // lazily on first get_string; see fiat_marelli_find_hitag2_key).
    bool hitag2_checked;
    int16_t hitag2_key_index; // -1 = no known key matched; >=0 index into fiat_v1 keys
    uint32_t hitag2_epoch;
};

struct SubGhzProtocolEncoderFiatMarelli {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint8_t raw_data[13];
    uint32_t extra_data;
    uint8_t bit_count;
    uint32_t te_detected;
};
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderFiatMarelli);


typedef enum {
    FiatMarelliDecoderStepReset = 0,
    FiatMarelliDecoderStepPreamble = 1,
    FiatMarelliDecoderStepSync = 2,
    FiatMarelliDecoderStepData = 3,
    FiatMarelliDecoderStepRetxSync = 4,
} FiatMarelliDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_fiat_marelli_decoder = {
    .alloc = subghz_protocol_decoder_fiat_marelli_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_fiat_marelli_feed,
    .reset = subghz_protocol_decoder_fiat_marelli_reset,
    .get_hash_data = subghz_protocol_decoder_fiat_marelli_get_hash_data,
    .serialize = subghz_protocol_decoder_fiat_marelli_serialize,
    .deserialize = subghz_protocol_decoder_fiat_marelli_deserialize,
    .get_string = subghz_protocol_decoder_fiat_marelli_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_fiat_marelli_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_fiat_marelli");

const SubGhzProtocol subghz_protocol_fiat_marelli = {
    .name = FIAT_MARELLI_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    // SubGhzProtocolFlag_Decodable deliberately omitted (v2.0.14, 2026-10-12).
    // Delphi BSI and Magneti Marelli BSI Fiat systems both use the same
    // Hitag2/PCF7946 transponder, so there is no distinct Marelli over-the-air
    // protocol: the Fiat V0/V1 decoders already cover every Fiat Hitag2 remote.
    // With Decodable set, this decoder only ever latched garbage 102-bit frames
    // (misframed, wrong serial) onto Fiat V1 (Delphi) and Renault captures and
    // showed up as a bogus second Decode-RAW result (0 of 22 corpus fires were
    // uniquely Marelli). Dropping Decodable keeps it out of the live-RX/Decode-RAW
    // sweep, while Load|Save|Send keep existing saved MARELLI .sub files openable
    // and emulatable. Re-add Decodable if a genuine Marelli RF frame is ever found.
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_fiat_marelli_decoder,
    .encoder = &subghz_protocol_fiat_marelli_encoder_ext.vtable,
};

#define FIAT_MARELLI_ENCODER_UPLOAD_MAX 1500
#define FIAT_MARELLI_ENCODER_REPEAT     3
#define FIAT_MARELLI_PREAMBLE_PAIRS     100

static void fiat_marelli_rebuild_raw_data(SubGhzProtocolDecoderFiatMarelli* instance) {
    memset(instance->raw_data, 0, sizeof(instance->raw_data));

    uint64_t key = instance->generic.data;
    for(int i = 0; i < 8; i++) {
        instance->raw_data[i] = (uint8_t)(key >> (56 - i * 8));
    }

    uint8_t extra_bits =
        instance->generic.data_count_bit > 64 ? (instance->generic.data_count_bit - 64) : 0;
    for(uint8_t i = 0; i < extra_bits && i < 32; i++) {
        uint8_t byte_idx = 8 + (i / 8);
        uint8_t bit_pos = 7 - (i % 8);
        if(instance->extra_data & (1UL << (extra_bits - 1 - i))) {
            instance->raw_data[byte_idx] |= (1 << bit_pos);
        }
    }

    instance->bit_count = instance->generic.data_count_bit;

    if(instance->bit_count >= 56) {
        instance->generic.serial =
            ((uint32_t)instance->raw_data[2] << 24) |
            ((uint32_t)instance->raw_data[3] << 16) |
            ((uint32_t)instance->raw_data[4] << 8) |
            ((uint32_t)instance->raw_data[5]);
        instance->generic.btn = (instance->raw_data[6] >> 4) & 0xF;
        instance->generic.cnt = (instance->raw_data[7] >> 3) & 0x1F;
    }
}

static void fiat_marelli_prepare_data(SubGhzProtocolDecoderFiatMarelli* instance) {
    instance->bit_count = 0;
    instance->extra_data = 0;
    instance->generic.data = 0;
    memset(instance->raw_data, 0, sizeof(instance->raw_data));
    manchester_advance(
        instance->manchester_state,
        ManchesterEventReset,
        &instance->manchester_state,
        NULL);
    instance->decoder_state = FiatMarelliDecoderStepData;
}

void* subghz_protocol_decoder_fiat_marelli_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderFiatMarelli* instance =
        calloc(1, sizeof(SubGhzProtocolDecoderFiatMarelli));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_fiat_marelli;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_fiat_marelli_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderFiatMarelli* instance = context;
    instance->decoder_state = FiatMarelliDecoderStepReset;
    instance->preamble_count = 0;
    instance->bit_count = 0;
    instance->extra_data = 0;
    instance->te_last = 0;
    instance->te_sum = 0;
    instance->te_count = 0;
    instance->te_detected = 0;
    instance->generic.data = 0;
    instance->hitag2_checked = false;
    instance->hitag2_key_index = -1;
    instance->hitag2_epoch = 0;
    memset(instance->raw_data, 0, sizeof(instance->raw_data));
    instance->manchester_state = ManchesterStateMid1;
}

void subghz_protocol_decoder_fiat_marelli_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderFiatMarelli* instance = context;

    uint32_t te_short = instance->te_detected ? instance->te_detected
                                              : (uint32_t)subghz_protocol_fiat_marelli_const.te_short;
    uint32_t te_long = te_short * 2;
    uint32_t te_delta = te_short / 2;
    if(te_delta < 30) te_delta = 30;
    uint32_t diff;

    switch(instance->decoder_state) {
    case FiatMarelliDecoderStepReset:
        if(level) {
            if(duration >= FIAT_MARELLI_PREAMBLE_PULSE_MIN &&
               duration <= FIAT_MARELLI_PREAMBLE_PULSE_MAX) {
                instance->decoder_state = FiatMarelliDecoderStepPreamble;
                instance->preamble_count = 1;
                instance->te_sum = duration;
                instance->te_count = 1;
                instance->te_last = duration;
            }
        } else {
            if(duration > FIAT_MARELLI_RETX_GAP_MIN) {
                instance->decoder_state = FiatMarelliDecoderStepRetxSync;
                instance->te_last = duration;
            }
        }
        break;

    case FiatMarelliDecoderStepPreamble:
        if(duration >= FIAT_MARELLI_PREAMBLE_PULSE_MIN &&
           duration <= FIAT_MARELLI_PREAMBLE_PULSE_MAX) {
            instance->preamble_count++;
            instance->te_sum += duration;
            instance->te_count++;
            instance->te_last = duration;
        } else if(!level) {
            if(instance->preamble_count >= FIAT_MARELLI_PREAMBLE_MIN && instance->te_count > 0) {
                instance->te_detected = instance->te_sum / instance->te_count;
                uint32_t gap_threshold = instance->te_detected * FIAT_MARELLI_GAP_TE_MULT;

                if(duration > gap_threshold) {
                    instance->decoder_state = FiatMarelliDecoderStepSync;
                    instance->te_last = duration;
                } else {
                    instance->decoder_state = FiatMarelliDecoderStepReset;
                }
            } else {
                instance->decoder_state = FiatMarelliDecoderStepReset;
            }
        } else {
            instance->decoder_state = FiatMarelliDecoderStepReset;
        }
        break;

    case FiatMarelliDecoderStepSync: {
        uint32_t sync_min = instance->te_detected * FIAT_MARELLI_SYNC_TE_MIN_MULT;
        uint32_t sync_max = instance->te_detected * FIAT_MARELLI_SYNC_TE_MAX_MULT;

        if(level && duration >= sync_min && duration <= sync_max) {
            fiat_marelli_prepare_data(instance);
            instance->te_last = duration;
        } else {
            instance->decoder_state = FiatMarelliDecoderStepReset;
        }
        break;
    }

    case FiatMarelliDecoderStepRetxSync:
        if(level && duration >= FIAT_MARELLI_RETX_SYNC_MIN &&
           duration <= FIAT_MARELLI_RETX_SYNC_MAX) {
            if(!instance->te_detected) {
                instance->te_detected = duration / 8;
                if(instance->te_detected < 70) instance->te_detected = 100;
                if(instance->te_detected > 350) instance->te_detected = 260;
            }
            fiat_marelli_prepare_data(instance);
            instance->te_last = duration;
        } else {
            instance->decoder_state = FiatMarelliDecoderStepReset;
        }
        break;

    case FiatMarelliDecoderStepData: {
        ManchesterEvent event = ManchesterEventReset;
        bool frame_complete = false;

        diff = (duration > te_short) ? (duration - te_short) : (te_short - duration);
        if(diff < te_delta) {
            event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
        } else {
            diff = (duration > te_long) ? (duration - te_long) : (te_long - duration);
            if(diff < te_delta) {
                event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
            }
        }

        if(event != ManchesterEventReset) {
            bool data_bit;
            if(manchester_advance(
                   instance->manchester_state,
                   event,
                   &instance->manchester_state,
                   &data_bit)) {
                uint32_t new_bit = data_bit ? 1 : 0;

                if(instance->bit_count < FIAT_MARELLI_MAX_DATA_BITS) {
                    uint8_t byte_idx = instance->bit_count / 8;
                    uint8_t bit_pos = 7 - (instance->bit_count % 8);
                    if(new_bit) {
                        instance->raw_data[byte_idx] |= (1 << bit_pos);
                    }
                }

                if(instance->bit_count < 64) {
                    instance->generic.data = (instance->generic.data << 1) | new_bit;
                } else {
                    instance->extra_data = (instance->extra_data << 1) | new_bit;
                }

                instance->bit_count++;

                if(instance->bit_count >= FIAT_MARELLI_MAX_DATA_BITS) {
                    frame_complete = true;
                }
            }
        } else {
            if(instance->bit_count >= FIAT_MARELLI_MIN_DATA_BITS) {
                frame_complete = true;
            } else {
                instance->decoder_state = FiatMarelliDecoderStepReset;
            }
        }

        if(frame_complete) {
            instance->generic.data_count_bit = instance->bit_count;

            bool crc_ok = true;
            if(instance->bit_count >= 104) {
                uint8_t calc = fiat_marelli_crc8(instance->raw_data, 12);
                crc_ok = (calc == instance->raw_data[12]);
            }

            // Preamble-residue gate (see fiat_marelli notes): a genuine Marelli
            // frame starts with a full 0xFF preamble byte and a data-start 0 bit
            // in byte[1] (observed 0xFFFE across two fobs; 0xFFFC per spec). A
            // noise/cross-protocol latch (e.g. a Manchester-ish section of a Fiat
            // V1 capture) comes through as 0xFFFF and is rejected here, so it no
            // longer shows up as a bogus Marelli result in Decode RAW.
            bool preamble_ok =
                (instance->raw_data[0] == 0xFFU) && ((instance->raw_data[1] & 0x01U) == 0U);

            if(crc_ok && preamble_ok) {
                instance->generic.serial =
                    ((uint32_t)instance->raw_data[2] << 24) |
                    ((uint32_t)instance->raw_data[3] << 16) |
                    ((uint32_t)instance->raw_data[4] << 8) |
                    ((uint32_t)instance->raw_data[5]);
                instance->generic.btn = (instance->raw_data[6] >> 4) & 0xF;
                instance->generic.cnt = (instance->raw_data[7] >> 3) & 0x1F;

                if(instance->base.callback) {
                    instance->base.callback(&instance->base, instance->base.context);
                }
            }

            instance->decoder_state = FiatMarelliDecoderStepReset;
        }

        instance->te_last = duration;
        break;
    }

    }
}

uint8_t subghz_protocol_decoder_fiat_marelli_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderFiatMarelli* instance = context;
    SubGhzBlockDecoder decoder = {
        .decode_data = instance->generic.data,
        .decode_count_bit =
            instance->generic.data_count_bit > 64 ? 64 : instance->generic.data_count_bit,
    };
    return subghz_protocol_blocks_get_hash_data(&decoder, (decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_fiat_marelli_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderFiatMarelli* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        flipper_format_write_uint32(flipper_format, "Extra", &instance->extra_data, 1);

        uint32_t extra_bits = instance->generic.data_count_bit > 64
                                  ? (instance->generic.data_count_bit - 64)
                                  : 0;
        flipper_format_write_uint32(flipper_format, "Extra_bits", &extra_bits, 1);

        uint32_t te = instance->te_detected;
        flipper_format_write_uint32(flipper_format, "TE", &te, 1);
    }

    return ret;
}

SubGhzProtocolStatus subghz_protocol_decoder_fiat_marelli_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderFiatMarelli* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_deserialize(&instance->generic, flipper_format);

    if(ret == SubGhzProtocolStatusOk) {
        uint32_t extra = 0;
        if(flipper_format_read_uint32(flipper_format, "Extra", &extra, 1)) {
            instance->extra_data = extra;
        }

        uint32_t te = 0;
        if(flipper_format_read_uint32(flipper_format, "TE", &te, 1)) {
            instance->te_detected = te;
        }

        fiat_marelli_rebuild_raw_data(instance);
    }

    return ret;
}

// Hitag2 manufacturer-key dictionary pass for a Fiat/Marelli frame. Marelli
// shares the Fiat V1 BCM-Hitag2 cipher, so we reuse the verified fiat_v1
// authenticator and key list. The Marelli field mapping (confirmed against the
// community KeeLoq-RX recovery and a host round-trip) is:
//   uid     = raw[2..5]
//   button  = raw[6] >> 4
//   control = ((raw[6] & 0x0F) << 5) | (raw[7] >> 3)   (exposed epoch4|counter5)
//   hop(ks) = raw[8..11]
// The frame's hidden high counter bits are modelled by the fiat_v1 epoch. The
// full 0..FIAT_MARELLI_HITAG2_EPOCH_MAX sweep (~95 keys x 256 epochs) measures
// ~10-30s on the 64MHz target, so the info screen calls this epoch_max=1 only
// (epoch-0 quick check, matching fiat_v1's inline verify); the full sweep runs
// off the GUI thread (worker). Result cached in the instance.
#define FIAT_MARELLI_HITAG2_EPOCH_MAX 256U
static int16_t
    fiat_marelli_find_hitag2_key(const uint8_t* raw, uint32_t* out_epoch, uint32_t epoch_max) {
    uint32_t uid = ((uint32_t)raw[2] << 24) | ((uint32_t)raw[3] << 16) |
                   ((uint32_t)raw[4] << 8) | (uint32_t)raw[5];
    uint8_t button = (raw[6] >> 4) & 0x0FU;
    uint16_t control = (uint16_t)((((uint16_t)(raw[6] & 0x0FU)) << 5U) | (raw[7] >> 3U));
    uint32_t hop = ((uint32_t)raw[8] << 24) | ((uint32_t)raw[9] << 16) |
                   ((uint32_t)raw[10] << 8) | (uint32_t)raw[11];

    const uint8_t(*keys)[6] = subghz_protocol_fiat_v1_get_known_keys();
    for(uint32_t epoch = 0U; epoch < epoch_max; epoch++) {
        for(uint8_t i = 0U; i < FIAT_V1_KNOWN_KEY_COUNT; i++) {
            if(subghz_protocol_fiat_v1_compute_auth(uid, button, control, keys[i], epoch) ==
               hop) {
                if(out_epoch) *out_epoch = epoch;
                return (int16_t)i;
            }
        }
    }
    return -1;
}

void subghz_protocol_decoder_fiat_marelli_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderFiatMarelli* instance = context;
    // Detect/decode split (FoxFW2.0 v2.1): the epoch-0 dictionary check uses the
    // Fiat V1 cipher (not exported), so it stays internal, same condition and
    // caching as before; nothing it computes is displayed.
    if(instance->bit_count >= 104 && !instance->hitag2_checked &&
       fiat_marelli_crc8(instance->raw_data, 12) == instance->raw_data[12]) {
        instance->hitag2_key_index =
            fiat_marelli_find_hitag2_key(instance->raw_data, &instance->hitag2_epoch, 1U);
        instance->hitag2_checked = true;
    }
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}

