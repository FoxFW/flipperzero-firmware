#include "microchip_hcs361.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "decode_plugin.h"
#include "keeloq.h"
#include "../environment.h"
#include "../encode_plugin.h"

#define TAG "HCS361"

#define HCS361_FRAME_BITS 67
#define HCS361_PRE_MIN    5
#define HCS361_TE         200U
#define HCS361_TE_DELTA   90U
#define HCS361_HDR_MIN    1200U
#define HCS361_END_GAP    800U

static const SubGhzBlockConst subghz_protocol_hcs361_const = {
    .te_short = HCS361_TE,
    .te_long = 2 * HCS361_TE,
    .te_delta = HCS361_TE_DELTA,
    .min_count_bit_for_found = 64,
};

typedef enum {
    HCS361StepReset = 0,
    HCS361StepPreamble,
    HCS361StepSaveDuration,
    HCS361StepCheckDuration,
} HCS361Step;

struct SubGhzProtocolDecoderHCS361 {
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
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderHCS361);

static inline uint8_t hcs361_bit(const uint8_t* b, int i) {
    return (b[i >> 3] >> (7 - (i & 7))) & 1;
}

static bool hcs361_crc_ok(const uint8_t* b, uint8_t actual_crc) {
    int crc = 0, crc_bat_low = 0;
    for(int i = 0; i < 65; i++) {
        int bit = hcs361_bit(b, i);
        int crc_bit = ((crc >> 1) ^ bit) & 0x1;
        if(i == 64) {
            int crc_bit_bl = crc_bit ^ 1;
            crc_bat_low = crc_bit_bl | (((crc_bit_bl ^ crc) << 1) & 0x2);
        }
        crc = crc_bit | (((crc_bit ^ crc) << 1) & 0x2);
    }
    return actual_crc == crc || actual_crc == crc_bat_low;
}

static void hcs361_add_bit(SubGhzProtocolDecoderHCS361* instance, uint8_t bit) {
    if(instance->bit_count < HCS361_FRAME_BITS) {
        int i = instance->bit_count;
        if(bit) instance->frame[i >> 3] |= (uint8_t)(0x80 >> (i & 7));
        instance->bit_count++;
    }
}

static void hcs361_process(SubGhzProtocolDecoderHCS361* instance) {
    if(instance->bit_count != HCS361_FRAME_BITS) return;
    const uint8_t* b = instance->frame;

    bool all_ff = true;
    for(int i = 0; i < 8; i++)
        if(b[i] != 0xFF) {
            all_ff = false;
            break;
        }
    if(all_ff) return;

    uint8_t actual_crc = (uint8_t)((b[8] >> 5) & 0x3);
    if(!hcs361_crc_ok(b, actual_crc)) return;

    uint64_t cw = 0;
    for(int i = 0; i < 64; i++) cw = (cw << 1) | hcs361_bit(b, i);

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

void* subghz_protocol_decoder_hcs361_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolDecoderHCS361* instance = calloc(1, sizeof(SubGhzProtocolDecoderHCS361));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_hcs361;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->keystore = subghz_environment_get_keystore(environment);
    return instance;
}

void subghz_protocol_decoder_hcs361_free(void* context) {
    furi_check(context);
    free(context);
}

void subghz_protocol_decoder_hcs361_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHCS361* instance = context;
    instance->step = HCS361StepReset;
    instance->pre_count = 0;
    instance->bit_count = 0;
    memset(instance->frame, 0, sizeof(instance->frame));
}

void subghz_protocol_decoder_hcs361_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderHCS361* instance = context;
    const uint32_t te = HCS361_TE;
    const uint32_t d = HCS361_TE_DELTA;

    switch(instance->step) {
    case HCS361StepReset:
        if(level && DURATION_DIFF(duration, te) < d) {
            instance->step = HCS361StepPreamble;
            instance->pre_count = 1;
        }
        break;

    case HCS361StepPreamble:
        if(!level) {
            if(duration >= HCS361_HDR_MIN && instance->pre_count >= HCS361_PRE_MIN) {

                instance->step = HCS361StepSaveDuration;
                instance->bit_count = 0;
                memset(instance->frame, 0, sizeof(instance->frame));
            } else if(DURATION_DIFF(duration, te) < d) {

            } else {
                instance->step = HCS361StepReset;
                instance->pre_count = 0;
            }
        } else {
            if(DURATION_DIFF(duration, te) < d) {
                if(instance->pre_count < 0xFFFF) instance->pre_count++;
            } else if(duration >= HCS361_HDR_MIN) {

            } else {
                instance->step = HCS361StepReset;
                instance->pre_count = 0;
            }
        }
        break;

    case HCS361StepSaveDuration:
        if(level) {
            instance->te_last = duration;
            instance->step = HCS361StepCheckDuration;
        } else if(duration >= HCS361_END_GAP) {
            instance->step = HCS361StepReset;
            instance->pre_count = 0;
        }
        break;

    case HCS361StepCheckDuration:
        if(!level) {
            if(duration >= HCS361_END_GAP) {
                hcs361_process(instance);
                instance->step = HCS361StepReset;
                instance->pre_count = 0;
            } else if(
                DURATION_DIFF(instance->te_last, te) < d && DURATION_DIFF(duration, 2 * te) < 2 * d) {
                hcs361_add_bit(instance, 1);
                instance->step = HCS361StepSaveDuration;
            } else if(
                DURATION_DIFF(instance->te_last, 2 * te) < 2 * d && DURATION_DIFF(duration, te) < d) {
                hcs361_add_bit(instance, 0);
                instance->step = HCS361StepSaveDuration;
            } else {
                instance->step = HCS361StepReset;
                instance->pre_count = 0;
            }
        } else {
            instance->step = HCS361StepReset;
            instance->pre_count = 0;
        }
        break;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_hcs361_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderHCS361* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;
    if(!flipper_format_write_hex(flipper_format, "Frame", instance->frame, 9)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_hcs361_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderHCS361* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_hcs361_const.min_count_bit_for_found);
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

void subghz_protocol_decoder_hcs361_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderHCS361* instance = context;
    const SubGhzDecodeInput input = {
        .generic = &instance->generic,
        .aux = instance->manufacture_name,
    };
    subghz_decode_plugin_get_string("Microchip-HCS361", &input, output);
}

const SubGhzProtocolDecoder subghz_protocol_hcs361_decoder = {
    .alloc = subghz_protocol_decoder_hcs361_alloc,
    .free = subghz_protocol_decoder_hcs361_free,
    .feed = subghz_protocol_decoder_hcs361_feed,
    .reset = subghz_protocol_decoder_hcs361_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_hcs361_serialize,
    .deserialize = subghz_protocol_decoder_hcs361_deserialize,
    .get_string = subghz_protocol_decoder_hcs361_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_hcs361_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_recon_pwm");

const SubGhzProtocol subghz_protocol_hcs361 = {
    .name = SUBGHZ_PROTOCOL_HCS361_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_hcs361_decoder,
    .encoder = &subghz_protocol_hcs361_encoder_ext.vtable,
};
