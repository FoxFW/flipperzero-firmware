#include "microchip_hcs362.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "decode_plugin.h"
#include "keeloq.h"
#include "../environment.h"
#include "../encode_plugin.h"

#define TAG "HCS362"

#define HCS362_FRAME_BITS   69
#define HCS362_PRE_MIN      8
#define HCS362_TE           200U
#define HCS362_TE_DELTA     90U
#define HCS362_SEP_MIN      450U
#define HCS362_SEP_MAX      780U
#define HCS362_END_GAP      800U

static const SubGhzBlockConst subghz_protocol_hcs362_const = {
    .te_short = HCS362_TE,
    .te_long = 2 * HCS362_TE,
    .te_delta = HCS362_TE_DELTA,
    .min_count_bit_for_found = 64,
};

typedef enum {
    HCS362StepReset = 0,
    HCS362StepPreamble,
    HCS362StepSaveDuration,
    HCS362StepCheckDuration,
} HCS362Step;

struct SubGhzProtocolDecoderHCS362 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t step;
    uint16_t pre_count;
    uint32_t te_last;
    uint8_t frame[9];
    uint16_t bit_count;

    SubGhzKeystore* keystore;
    const char* manufacture_name;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderHCS362);

static inline uint8_t hcs362_bit(const uint8_t* b, int i) {
    return (b[i >> 3] >> (7 - (i & 7))) & 1;
}

static uint8_t hcs362_crc(const uint8_t* b) {
    uint8_t crc0 = 0, crc1 = 0;
    for(int n = 0; n < 65; n++) {
        uint8_t d = hcs362_bit(b, n);
        uint8_t next_crc1 = crc0 ^ d;
        uint8_t next_crc0 = crc0 ^ d ^ crc1;
        crc0 = next_crc0;
        crc1 = next_crc1;
    }
    return (uint8_t)((crc1 << 1) | crc0);
}

static void hcs362_add_bit(SubGhzProtocolDecoderHCS362* instance, uint8_t bit) {
    if(instance->bit_count < HCS362_FRAME_BITS) {
        int i = instance->bit_count;
        if(bit) instance->frame[i >> 3] |= (uint8_t)(0x80 >> (i & 7));
        instance->bit_count++;
    }
}

static void hcs362_process(SubGhzProtocolDecoderHCS362* instance) {
    if(instance->bit_count != HCS362_FRAME_BITS) return;
    const uint8_t* b = instance->frame;

    bool all_ff = true;
    for(int i = 1; i < 8; i++)
        if(b[i] != 0xFF) {
            all_ff = false;
            break;
        }
    if(all_ff) return;

    uint8_t actual_crc = (uint8_t)(((b[8] >> 6) & 1) | (((b[8] >> 5) & 1) << 1));
    if(actual_crc != hcs362_crc(b)) return;

    uint64_t cw = 0;
    for(int i = 0; i < 64; i++) cw = (cw << 1) | hcs362_bit(b, i);

    instance->generic.data = cw;
    instance->generic.data_count_bit = 64;
    instance->manufacture_name = "";
    subghz_protocol_keeloq_identify_codeword(
        &instance->generic, instance->keystore, &instance->manufacture_name);
    if(instance->generic.serial == 0) return;

    instance->decoder.decode_data = cw;
    instance->decoder.decode_count_bit = 64;

    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
}

void* subghz_protocol_decoder_hcs362_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolDecoderHCS362* instance = calloc(1, sizeof(SubGhzProtocolDecoderHCS362));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_hcs362;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->keystore = subghz_environment_get_keystore(environment);
    return instance;
}

void subghz_protocol_decoder_hcs362_free(void* context) {
    furi_check(context);
    free(context);
}

void subghz_protocol_decoder_hcs362_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHCS362* instance = context;
    instance->step = HCS362StepReset;
    instance->pre_count = 0;
    instance->bit_count = 0;
    memset(instance->frame, 0, sizeof(instance->frame));
}

void subghz_protocol_decoder_hcs362_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderHCS362* instance = context;
    const uint32_t te = HCS362_TE;
    const uint32_t d = HCS362_TE_DELTA;

    switch(instance->step) {
    case HCS362StepReset:
        if(level && DURATION_DIFF(duration, te) < d) {
            instance->step = HCS362StepPreamble;
            instance->pre_count = 1;
        }
        break;

    case HCS362StepPreamble:
        if(!level) {
            if(duration >= HCS362_SEP_MIN && duration <= HCS362_SEP_MAX &&
               instance->pre_count >= HCS362_PRE_MIN) {

                instance->step = HCS362StepSaveDuration;
                instance->bit_count = 0;
                memset(instance->frame, 0, sizeof(instance->frame));
            } else if(DURATION_DIFF(duration, te) < d) {

            } else {
                instance->step = HCS362StepReset;
                instance->pre_count = 0;
            }
        } else {
            if(DURATION_DIFF(duration, te) < d) {
                if(instance->pre_count < 0xFFFF) instance->pre_count++;
            } else {
                instance->step = HCS362StepReset;
                instance->pre_count = 0;
            }
        }
        break;

    case HCS362StepSaveDuration:
        if(level) {
            instance->te_last = duration;
            instance->step = HCS362StepCheckDuration;
        } else if(duration >= HCS362_END_GAP) {
            instance->step = HCS362StepReset;
            instance->pre_count = 0;
        }
        break;

    case HCS362StepCheckDuration:
        if(!level) {
            if(duration >= HCS362_END_GAP) {
                hcs362_process(instance);
                instance->step = HCS362StepReset;
                instance->pre_count = 0;
            } else if(
                DURATION_DIFF(instance->te_last, te) < d && DURATION_DIFF(duration, 2 * te) < 2 * d) {
                hcs362_add_bit(instance, 1);
                instance->step = HCS362StepSaveDuration;
            } else if(
                DURATION_DIFF(instance->te_last, 2 * te) < 2 * d && DURATION_DIFF(duration, te) < d) {
                hcs362_add_bit(instance, 0);
                instance->step = HCS362StepSaveDuration;
            } else {
                instance->step = HCS362StepReset;
                instance->pre_count = 0;
            }
        } else {
            instance->step = HCS362StepReset;
            instance->pre_count = 0;
        }
        break;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_hcs362_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderHCS362* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;
    if(!flipper_format_write_hex(flipper_format, "Frame", instance->frame, 9)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_hcs362_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderHCS362* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_hcs362_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;
    flipper_format_rewind(flipper_format);
    flipper_format_read_hex(flipper_format, "Frame", instance->frame, 9);
    instance->manufacture_name = "";
    subghz_protocol_keeloq_identify_codeword(
        &instance->generic, instance->keystore, &instance->manufacture_name);
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

void subghz_protocol_decoder_hcs362_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderHCS362* instance = context;
    const SubGhzDecodeInput input = {
        .generic = &instance->generic,
        .aux = instance->manufacture_name,
    };
    subghz_decode_plugin_get_string("Microchip-HCS362", &input, output);
}

const SubGhzProtocolDecoder subghz_protocol_hcs362_decoder = {
    .alloc = subghz_protocol_decoder_hcs362_alloc,
    .free = subghz_protocol_decoder_hcs362_free,
    .feed = subghz_protocol_decoder_hcs362_feed,
    .reset = subghz_protocol_decoder_hcs362_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_hcs362_serialize,
    .deserialize = subghz_protocol_decoder_hcs362_deserialize,
    .get_string = subghz_protocol_decoder_hcs362_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_hcs362_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_recon_pwm");

const SubGhzProtocol subghz_protocol_hcs362 = {
    .name = SUBGHZ_PROTOCOL_HCS362_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_hcs362_decoder,
    .encoder = &subghz_protocol_hcs362_encoder_ext.vtable,
};
