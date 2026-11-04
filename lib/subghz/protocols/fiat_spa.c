#include "fiat_spa.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include <lib/toolbox/manchester_decoder.h>
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

#define TAG "SubGhzProtocolFiatSpa"

static const SubGhzBlockConst subghz_protocol_fiat_spa_const = {
    .te_short = 200,
    .te_long = 400,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

#define FIAT_SPA_PREAMBLE_PAIRS 150
#define FIAT_SPA_GAP_US 800
#define FIAT_SPA_TOTAL_BURSTS 3
#define FIAT_SPA_INTER_BURST_GAP 25000
#define FIAT_SPA_UPLOAD_MAX 1328

struct SubGhzProtocolDecoderFiatSpa {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockGeneric generic;
    SubGhzBlockDecoder decoder;
    ManchesterState manchester_state;
    uint16_t preamble_count;
    uint32_t data_low;
    uint32_t data_high;
    uint8_t bit_count;
    uint32_t hop;
    uint32_t fix;
    uint8_t endbyte;
};

struct SubGhzProtocolEncoderFiatSpa {
    SubGhzProtocolEncoderBase base;
    void* decoder_callback;
    void* decoder_context;
    SubGhzBlockGeneric generic;
    SubGhzProtocolBlockEncoder encoder;
    uint32_t hop;
    uint32_t fix;
    uint8_t endbyte;
};

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockGeneric generic;
} SubGhzProtocolCommonFiatSpa;

typedef enum {
    FiatSpaDecoderStepReset = 0,
    FiatSpaDecoderStepPreamble,
    FiatSpaDecoderStepData,
} FiatSpaDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_fiat_spa_decoder = {
    .alloc = subghz_protocol_decoder_fiat_spa_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_fiat_spa_feed,
    .reset = subghz_protocol_decoder_fiat_spa_reset,
    .get_hash_data = subghz_protocol_decoder_fiat_spa_get_hash_data,
    .serialize = subghz_protocol_decoder_fiat_spa_serialize,
    .deserialize = subghz_protocol_decoder_fiat_spa_deserialize,
    .get_string = subghz_protocol_decoder_fiat_spa_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_fiat_spa_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_fiat_spa");

const SubGhzProtocol subghz_protocol_fiat_spa = {
    .name = SUBGHZ_PROTOCOL_FIAT_SPA_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_fiat_spa_decoder,
    .encoder = &subghz_protocol_fiat_spa_encoder_ext.vtable,
};

void* subghz_protocol_decoder_fiat_spa_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderFiatSpa* instance = malloc(sizeof(SubGhzProtocolDecoderFiatSpa));
    instance->base.protocol = &subghz_protocol_fiat_spa;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_fiat_spa_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderFiatSpa* instance = context;
    instance->decoder.parser_step = FiatSpaDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->preamble_count = 0;
    instance->data_low = 0;
    instance->data_high = 0;
    instance->bit_count = 0;
    instance->hop = 0;
    instance->fix = 0;
    instance->endbyte = 0;
    instance->manchester_state = ManchesterStateMid1;
}

void subghz_protocol_decoder_fiat_spa_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderFiatSpa* instance = context;
    uint32_t te_short = (uint32_t)subghz_protocol_fiat_spa_const.te_short;
    uint32_t te_long = (uint32_t)subghz_protocol_fiat_spa_const.te_long;
    uint32_t te_delta = (uint32_t)subghz_protocol_fiat_spa_const.te_delta;
    uint32_t gap_threshold = FIAT_SPA_GAP_US;
    uint32_t diff;

    switch(instance->decoder.parser_step) {
    case FiatSpaDecoderStepReset:
        if(!level) return;
        if(duration < te_short) {
            diff = te_short - duration;
        } else {
            diff = duration - te_short;
        }
        if(diff < te_delta) {
            instance->data_low = 0;
            instance->data_high = 0;
            instance->decoder.parser_step = FiatSpaDecoderStepPreamble;
            instance->preamble_count = 0;
            instance->bit_count = 0;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            manchester_advance(
                instance->manchester_state,
                ManchesterEventReset,
                &instance->manchester_state,
                NULL);
        }
        break;

    case FiatSpaDecoderStepPreamble:
        if(level) {
            if(duration < te_short) {
                diff = te_short - duration;
            } else {
                diff = duration - te_short;
            }
            if(diff < te_delta) {
                instance->preamble_count++;
            } else {
                instance->decoder.parser_step = FiatSpaDecoderStepReset;
            }
            return;
        }

        if(duration < te_short) {
            diff = te_short - duration;
        } else {
            diff = duration - te_short;
        }

        if(diff < te_delta) {
            instance->preamble_count++;
        } else {
            if(instance->preamble_count >= FIAT_SPA_PREAMBLE_PAIRS) {
                if(duration < gap_threshold) {
                    diff = gap_threshold - duration;
                } else {
                    diff = duration - gap_threshold;
                }
                if(diff < te_delta) {
                    instance->decoder.parser_step = FiatSpaDecoderStepData;
                    instance->preamble_count = 0;
                    instance->data_low = 0;
                    instance->data_high = 0;
                    instance->bit_count = 0;
                    manchester_advance(instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);
                    return;
                }
            }
            instance->decoder.parser_step = FiatSpaDecoderStepReset;
        }

        if(instance->preamble_count >= FIAT_SPA_PREAMBLE_PAIRS &&
           instance->decoder.parser_step == FiatSpaDecoderStepPreamble) {
            if(duration < gap_threshold) {
                diff = gap_threshold - duration;
            } else {
                diff = duration - gap_threshold;
            }
            if(diff < te_delta) {
                instance->decoder.parser_step = FiatSpaDecoderStepData;
                instance->preamble_count = 0;
                instance->data_low = 0;
                instance->data_high = 0;
                instance->bit_count = 0;
                manchester_advance(instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);
                return;
            }
        }
        break;

    case FiatSpaDecoderStepData: {
        ManchesterEvent event = ManchesterEventReset;
        if(duration < te_short) {
            diff = te_short - duration;
            if(diff < te_delta) {
                event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
            }
        } else {
            diff = duration - te_short;
            if(diff < te_delta) {
                event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
            } else {
                if(duration < te_long) {
                    diff = te_long - duration;
                } else {
                    diff = duration - te_long;
                }
                if(diff < te_delta) {
                    event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
                }
            }
        }
        if(event != ManchesterEventReset) {
            bool data_bit_bool;
            if(manchester_advance(
                   instance->manchester_state,
                   event,
                   &instance->manchester_state,
                   &data_bit_bool)) {
                uint32_t new_bit = data_bit_bool ? 1 : 0;
                uint32_t carry = (instance->data_low >> 31) & 1;
                instance->data_low = (instance->data_low << 1) | new_bit;
                instance->data_high = (instance->data_high << 1) | carry;
                instance->bit_count++;
                if(instance->bit_count == 64) {
                    instance->fix = instance->data_low;
                    instance->hop = instance->data_high;
                    instance->data_low = 0;
                    instance->data_high = 0;
                }
                if(instance->bit_count == 0x47) {
                    instance->endbyte = (uint8_t)(instance->data_low & 0x3F);
                    instance->generic.data = ((uint64_t)instance->hop << 32) | instance->fix;
                    instance->generic.data_count_bit = 71;
                    instance->generic.serial = instance->fix;
                    instance->generic.btn = instance->endbyte;
                    instance->generic.cnt = instance->hop;
                    instance->decoder.decode_data = instance->generic.data;
                    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
                    if(instance->base.callback) {
                        instance->base.callback(&instance->base, instance->base.context);
                    }
                    instance->data_low = 0;
                    instance->data_high = 0;
                    instance->bit_count = 0;
                    instance->decoder.parser_step = FiatSpaDecoderStepReset;
                }
            }
        } else {
            if(instance->bit_count == 0x47) {
                instance->endbyte = (uint8_t)(instance->data_low & 0x3F);
                instance->generic.data = ((uint64_t)instance->hop << 32) | instance->fix;
                instance->generic.data_count_bit = 71;
                instance->generic.serial = instance->fix;
                instance->generic.btn = instance->endbyte;
                instance->generic.cnt = instance->hop;
                instance->decoder.decode_data = instance->generic.data;
                instance->decoder.decode_count_bit = instance->generic.data_count_bit;
                if(instance->base.callback) {
                    instance->base.callback(&instance->base, instance->base.context);
                }
                instance->data_low = 0;
                instance->data_high = 0;
                instance->bit_count = 0;
                instance->decoder.parser_step = FiatSpaDecoderStepReset;
            } else if(instance->bit_count < 64) {
                instance->decoder.parser_step = FiatSpaDecoderStepReset;
            }
        }
        break;
    }
    }
}

uint8_t subghz_protocol_decoder_fiat_spa_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderFiatSpa* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_fiat_spa_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderFiatSpa* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        if(subghz_block_generic_serialize(&instance->generic, flipper_format, preset) !=
           SubGhzProtocolStatusOk) {
            break;
        }
        if(!flipper_format_write_uint32(
               flipper_format, "EndByte", (uint32_t*)&instance->endbyte, 1)) {
            break;
        }
        ret = SubGhzProtocolStatusOk;
    } while(false);
    return ret;
}

SubGhzProtocolStatus subghz_protocol_decoder_fiat_spa_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderFiatSpa* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) break;
        uint32_t endbyte_temp = 0;
        if(!flipper_format_read_uint32(flipper_format, "EndByte", &endbyte_temp, 1)) {
            instance->endbyte = 0;
        } else {
            instance->endbyte = (uint8_t)endbyte_temp;
        }
        instance->hop = (uint32_t)(instance->generic.data >> 32);
        instance->fix = (uint32_t)(instance->generic.data & 0xFFFFFFFF);
        instance->generic.cnt = instance->hop;
        instance->generic.serial = instance->fix;
        instance->generic.btn = instance->endbyte;
        ret = SubGhzProtocolStatusOk;
    } while(false);
    return ret;
}

void subghz_protocol_decoder_fiat_spa_get_string(void* context, FuriString* output) {
    SubGhzProtocolCommonFiatSpa* instance = context;
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}


