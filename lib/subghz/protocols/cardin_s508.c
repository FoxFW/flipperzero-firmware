#include "cardin_s508.h"

#include <lib/toolbox/manchester_decoder.h>
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolCardinS508"

static const SubGhzBlockConst subghz_protocol_cardin_s508_const = {
    .te_short = 100,
    .te_long = 200,
    .te_delta = 50,
    .min_count_bit_for_found = 128,
};

#define CARDIN_S508_PULSE_MIN    40
#define CARDIN_S508_HI_SHORT_MAX 165
#define CARDIN_S508_HI_LONG_MAX  340
#define CARDIN_S508_LO_SHORT_MAX 135
#define CARDIN_S508_LO_LONG_MAX  300

#define CARDIN_S508_SYNC         0x0CF4u
#define CARDIN_S508_SYNC_INV     0x030Bu
#define CARDIN_S508_SYNC_MASK    0x0FFFu
#define CARDIN_S508_PAYLOAD_BITS 128u

struct SubGhzProtocolDecoderCardinS508 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    ManchesterState manchester_saved_state;
    uint16_t sync_shift;
    uint16_t payload_count;
    uint64_t payload_hi;
    uint64_t payload_lo;
    bool sync_locked;
    bool inverted;
    uint64_t last_hi;
    uint64_t last_lo;
    bool have_last;
};

struct SubGhzProtocolEncoderCardinS508 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

const SubGhzProtocolDecoder subghz_protocol_cardin_s508_decoder = {
    .alloc = subghz_protocol_decoder_cardin_s508_alloc,
    .free = subghz_protocol_decoder_cardin_s508_free,
    .feed = subghz_protocol_decoder_cardin_s508_feed,
    .reset = subghz_protocol_decoder_cardin_s508_reset,
    .get_hash_data = subghz_protocol_decoder_cardin_s508_get_hash_data,
    .serialize = subghz_protocol_decoder_cardin_s508_serialize,
    .deserialize = subghz_protocol_decoder_cardin_s508_deserialize,
    .get_string = subghz_protocol_decoder_cardin_s508_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_cardin_s508_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol subghz_protocol_cardin_s508 = {
    .name = SUBGHZ_PROTOCOL_CARDIN_S508_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_868 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_cardin_s508_decoder,
    .encoder = &subghz_protocol_cardin_s508_encoder,
};

static void subghz_protocol_decoder_cardin_s508_reset_accumulators(
    SubGhzProtocolDecoderCardinS508* instance) {
    instance->sync_shift = 0;
    instance->sync_locked = false;
    instance->inverted = false;
    instance->payload_hi = 0;
    instance->payload_lo = 0;
    instance->payload_count = 0;
    manchester_advance(
        instance->manchester_saved_state,
        ManchesterEventReset,
        &instance->manchester_saved_state,
        NULL);
}

void* subghz_protocol_decoder_cardin_s508_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderCardinS508* instance = malloc(sizeof(SubGhzProtocolDecoderCardinS508));
    instance->base.protocol = &subghz_protocol_cardin_s508;
    instance->generic.protocol_name = instance->base.protocol->name;
    subghz_protocol_decoder_cardin_s508_reset_accumulators(instance);
    instance->have_last = false;
    return instance;
}

void subghz_protocol_decoder_cardin_s508_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderCardinS508* instance = context;
    free(instance);
}

void subghz_protocol_decoder_cardin_s508_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderCardinS508* instance = context;
    subghz_protocol_decoder_cardin_s508_reset_accumulators(instance);
    instance->have_last = false;
}

static void subghz_protocol_decoder_cardin_s508_add_bit(
    SubGhzProtocolDecoderCardinS508* instance,
    bool bit) {
    if(!instance->sync_locked) {
        instance->sync_shift = ((instance->sync_shift << 1) | (bit ? 1u : 0u)) &
                               CARDIN_S508_SYNC_MASK;
        if(instance->sync_shift == CARDIN_S508_SYNC) {
            instance->sync_locked = true;
            instance->inverted = false;
        } else if(instance->sync_shift == CARDIN_S508_SYNC_INV) {
            instance->sync_locked = true;
            instance->inverted = true;
        } else {
            return;
        }
        instance->payload_hi = 0;
        instance->payload_lo = 0;
        instance->payload_count = 0;
        return;
    }

    instance->payload_hi = (instance->payload_hi << 1) | (instance->payload_lo >> 63);
    instance->payload_lo = (instance->payload_lo << 1) | (bit ? 1u : 0u);
    if(++instance->payload_count < CARDIN_S508_PAYLOAD_BITS) {
        return;
    }

    uint64_t hi = instance->inverted ? ~instance->payload_hi : instance->payload_hi;
    uint64_t lo = instance->inverted ? ~instance->payload_lo : instance->payload_lo;
    bool match = instance->have_last && (instance->last_hi == hi) && (instance->last_lo == lo);
    instance->last_hi = hi;
    instance->last_lo = lo;
    instance->have_last = true;
    instance->sync_locked = false;
    instance->sync_shift = 0;
    if(!match) {
        return;
    }
    instance->generic.data = lo;
    instance->generic.data_2 = hi;
    instance->generic.data_count_bit = CARDIN_S508_PAYLOAD_BITS;
    instance->decoder.decode_data = lo;
    instance->decoder.decode_count_bit = CARDIN_S508_PAYLOAD_BITS;
    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

static bool
    subghz_protocol_decoder_cardin_s508_classify(bool level, uint32_t duration, bool* is_short) {
    if(duration < CARDIN_S508_PULSE_MIN) return false;
    uint32_t short_max = level ? CARDIN_S508_HI_SHORT_MAX : CARDIN_S508_LO_SHORT_MAX;
    uint32_t long_max = level ? CARDIN_S508_HI_LONG_MAX : CARDIN_S508_LO_LONG_MAX;
    if(duration < short_max) {
        *is_short = true;
        return true;
    }
    if(duration < long_max) {
        *is_short = false;
        return true;
    }
    return false;
}

void subghz_protocol_decoder_cardin_s508_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderCardinS508* instance = context;

    bool is_short;
    if(!subghz_protocol_decoder_cardin_s508_classify(level, duration, &is_short)) {
        subghz_protocol_decoder_cardin_s508_reset_accumulators(instance);
        return;
    }

    ManchesterEvent event;
    if(is_short) {
        event = level ? ManchesterEventShortHigh : ManchesterEventShortLow;
    } else {
        event = level ? ManchesterEventLongHigh : ManchesterEventLongLow;
    }
    bool data;
    if(manchester_advance(
           instance->manchester_saved_state, event, &instance->manchester_saved_state, &data)) {
        subghz_protocol_decoder_cardin_s508_add_bit(instance, data);
    }
}

uint8_t subghz_protocol_decoder_cardin_s508_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderCardinS508* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_cardin_s508_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderCardinS508* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    uint8_t key_data[sizeof(uint64_t)] = {0};
    for(size_t i = 0; i < sizeof(uint64_t); i++) {
        key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data_2 >> (i * 8)) & 0xFF;
    }
    if((ret == SubGhzProtocolStatusOk) &&
       !flipper_format_write_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
        FURI_LOG_E(TAG, "Unable to add Data");
        ret = SubGhzProtocolStatusErrorParserOthers;
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_cardin_s508_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderCardinS508* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_cardin_s508_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(!flipper_format_read_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Missing Data");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        instance->generic.data_2 = 0;
        for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
            instance->generic.data_2 = instance->generic.data_2 << 8 | key_data[i];
        }
    } while(false);
    return ret;
}

void subghz_protocol_decoder_cardin_s508_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderCardinS508* instance = context;
    furi_string_cat_printf(
        output,
        "%s\r\n"
        "Rolling (AES-128)\r\n"
        "Sync:110011110100\r\n"
        "Key:%08lX%08lX%08lX%08lX\r\n",
        instance->generic.protocol_name,
        (uint32_t)(instance->generic.data_2 >> 32),
        (uint32_t)(instance->generic.data_2 & 0xFFFFFFFF),
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)(instance->generic.data & 0xFFFFFFFF));
}
