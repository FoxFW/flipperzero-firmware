#include "hormann_bisecur.h"

#include <lib/flipper_format/flipper_format_i.h>
#include <lib/toolbox/manchester_decoder.h>

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolHormannBiSecur"

static const SubGhzBlockConst subghz_protocol_hormann_bisecur_const = {
    .te_short = 208,
    .te_long = 416,
    .te_delta = 104,
    .min_count_bit_for_found = 176,
};

struct SubGhzProtocolDecoderHormannBiSecur {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t sync_cnt;
    ManchesterState manchester_saved_state;

    uint8_t type;
    uint8_t data[22];
    uint8_t crc;
};

typedef enum {
    HormannBiSecurDecoderStepReset = 0,
    HormannBiSecurDecoderStepFoundPreambleAlternatingShort,
    HormannBiSecurDecoderStepFoundPreambleHighVeryLong,
    HormannBiSecurDecoderStepFoundPreambleAlternatingLong,
    HormannBiSecurDecoderStepFoundData,
} HormannBiSecurDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_hormann_bisecur_decoder = {
    .alloc = subghz_protocol_decoder_hormann_bisecur_alloc,
    .free = subghz_protocol_decoder_hormann_bisecur_free,

    .feed = subghz_protocol_decoder_hormann_bisecur_feed,
    .reset = subghz_protocol_decoder_hormann_bisecur_reset,

    .get_hash_data = subghz_protocol_decoder_hormann_bisecur_get_hash_data,
    .serialize = subghz_protocol_decoder_hormann_bisecur_serialize,
    .deserialize = subghz_protocol_decoder_hormann_bisecur_deserialize,
    .get_string = subghz_protocol_decoder_hormann_bisecur_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_hormann_bisecur_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol subghz_protocol_hormann_bisecur = {
    .name = SUBGHZ_PROTOCOL_HORMANN_BISECUR_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_868 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,

    .decoder = &subghz_protocol_hormann_bisecur_decoder,
    .encoder = &subghz_protocol_hormann_bisecur_encoder,
};

static const ManchesterState manchester_reset_state = ManchesterStateMid1;

static bool manchester_advance_alt(
    ManchesterState state,
    ManchesterEvent event,
    ManchesterState* next_state,
    bool* data) {
    bool result = false;
    ManchesterState new_state = manchester_reset_state;

    if(event == ManchesterEventReset) {
        new_state = manchester_reset_state;
    } else if(state == ManchesterStateStart1) {
        if(event == ManchesterEventShortLow) {
            new_state = ManchesterStateMid1;
            result = true;
            if(data) *data = false;
        } else if(event == ManchesterEventLongLow) {
            new_state = ManchesterStateStart0;
            result = true;
            if(data) *data = false;
        } else {
            new_state = manchester_reset_state;
        }
    } else if(state == ManchesterStateMid1) {
        if(event == ManchesterEventShortHigh) {
            new_state = ManchesterStateStart1;
        } else if(event == ManchesterEventShortLow) {
            new_state = ManchesterStateStart0;
        } else {
            new_state = manchester_reset_state;
        }
    } else if(state == ManchesterStateStart0) {
        if(event == ManchesterEventShortHigh) {
            new_state = ManchesterStateMid0;
            result = true;
            if(data) *data = true;
        } else if(event == ManchesterEventLongHigh) {
            new_state = ManchesterStateStart1;
            result = true;
            if(data) *data = true;
        } else {
            new_state = manchester_reset_state;
        }
    } else if(state == ManchesterStateMid0) {
        if(event == ManchesterEventShortLow) {
            new_state = ManchesterStateStart0;
        } else if(event == ManchesterEventShortHigh) {
            new_state = ManchesterStateStart1;
        } else {
            new_state = manchester_reset_state;
        }
    }

    *next_state = new_state;
    return result;
}

static uint8_t
    subghz_protocol_decoder_hormann_bisecur_crc(SubGhzProtocolDecoderHormannBiSecur* instance) {
    furi_assert(instance);

    switch(instance->type) {
    case 0x50:
        return ~(subghz_protocol_blocks_crc8(instance->data + 1, 20, 0x07, 0x00) ^ 0x55);
    case 0x70:
        return subghz_protocol_blocks_crc8le(instance->data, 21, 0x07, 0xFF);
    }

    FURI_LOG_E(TAG, "Unknown type 0x%02X", instance->type);

    return 0;
}

static bool subghz_protocol_decoder_hormann_bisecur_check_crc(
    SubGhzProtocolDecoderHormannBiSecur* instance) {
    furi_assert(instance);

    if(instance->type != 0x50 && instance->type != 0x70) {
        FURI_LOG_W(TAG, "Unknown type 0x%02X", instance->type);
        return false;
    }

    return subghz_protocol_decoder_hormann_bisecur_crc(instance) == instance->crc;
}

static void
    subghz_protocol_hormann_bisecur_parse_data(SubGhzProtocolDecoderHormannBiSecur* instance) {
    furi_assert(instance);

    instance->type = instance->data[0];

    instance->generic.serial = 0;

    for(uint8_t i = 1; i < 5; i++) {
        instance->generic.serial = instance->generic.serial << 8 | instance->data[i];
    }

    instance->generic.data = 0;

    for(uint8_t i = 5; i < 13; i++) {
        instance->generic.data = instance->generic.data << 8 | instance->data[i];
    }

    instance->generic.data_2 = 0;

    for(uint8_t i = 13; i < 21; i++) {
        instance->generic.data_2 = instance->generic.data_2 << 8 | instance->data[i];
    }

    instance->crc = instance->data[21];
}

static void subghz_protocol_decoder_hormann_bisecur_add_bit(
    SubGhzProtocolDecoderHormannBiSecur* instance,
    bool level) {
    furi_assert(instance);

    if(instance->generic.data_count_bit >=
       subghz_protocol_hormann_bisecur_const.min_count_bit_for_found) {
        return;
    }

    if(level) {
        uint8_t byte_index = instance->generic.data_count_bit / 8;
        uint8_t bit_index = instance->generic.data_count_bit % 8;

        instance->data[byte_index] |= 1 << (7 - bit_index);
    }

    instance->generic.data_count_bit++;

    if(instance->generic.data_count_bit >=
       subghz_protocol_hormann_bisecur_const.min_count_bit_for_found) {
        if(instance->base.callback) {
            instance->base.callback(&instance->base, instance->base.context);
        } else {
            subghz_protocol_decoder_hormann_bisecur_reset(instance);
        }
    }
}

void* subghz_protocol_decoder_hormann_bisecur_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHormannBiSecur* instance =
        malloc(sizeof(SubGhzProtocolDecoderHormannBiSecur));
    instance->base.protocol = &subghz_protocol_hormann_bisecur;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_hormann_bisecur_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHormannBiSecur* instance = context;
    free(instance);
}

void subghz_protocol_decoder_hormann_bisecur_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHormannBiSecur* instance = context;
    instance->decoder.parser_step = HormannBiSecurDecoderStepReset;
    memset(instance->data, 0, 22);
    instance->generic.data_count_bit = 0;
    instance->manchester_saved_state = 0;
}

void subghz_protocol_decoder_hormann_bisecur_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderHormannBiSecur* instance = context;

    uint32_t duration_short = (uint32_t)subghz_protocol_hormann_bisecur_const.te_short;
    uint32_t duration_long = (uint32_t)subghz_protocol_hormann_bisecur_const.te_long;
    uint32_t duration_delta = (uint32_t)subghz_protocol_hormann_bisecur_const.te_delta;
    uint32_t duration_half_short = duration_short / 2;

    ManchesterEvent event = ManchesterEventReset;

    switch(instance->decoder.parser_step) {
    case HormannBiSecurDecoderStepReset:
        if(!level &&
           DURATION_DIFF(duration, duration_short + duration_half_short) < duration_delta) {
            instance->decoder.parser_step = HormannBiSecurDecoderStepFoundPreambleAlternatingShort;
        }
        break;
    case HormannBiSecurDecoderStepFoundPreambleAlternatingShort:
        if(DURATION_DIFF(duration, duration_short) < duration_delta) {
            break;
        }

        if(level && DURATION_DIFF(duration, duration_long * 4) < duration_delta) {
            instance->decoder.parser_step = HormannBiSecurDecoderStepFoundPreambleHighVeryLong;
            break;
        }

        instance->decoder.parser_step = HormannBiSecurDecoderStepReset;
        break;
    case HormannBiSecurDecoderStepFoundPreambleHighVeryLong:
        if(!level && DURATION_DIFF(duration, duration_long) < duration_delta) {
            instance->sync_cnt = 3;
            instance->decoder.parser_step = HormannBiSecurDecoderStepFoundPreambleAlternatingLong;
            break;
        }

        instance->decoder.parser_step = HormannBiSecurDecoderStepReset;
        break;
    case HormannBiSecurDecoderStepFoundPreambleAlternatingLong:
        if(level == (instance->sync_cnt-- & 1) &&
           DURATION_DIFF(duration, duration_long) < duration_delta) {
            if(!instance->sync_cnt) {
                manchester_advance_alt(
                    instance->manchester_saved_state,
                    event,
                    &instance->manchester_saved_state,
                    NULL);
                instance->decoder.parser_step = HormannBiSecurDecoderStepFoundData;
            }

            break;
        }

        instance->decoder.parser_step = HormannBiSecurDecoderStepReset;
        break;
    case HormannBiSecurDecoderStepFoundData:
        if(DURATION_DIFF(duration, duration_short) < duration_delta ||
           (instance->generic.data_count_bit ==
                subghz_protocol_hormann_bisecur_const.min_count_bit_for_found - 1 &&
            duration > duration_short)) {
            event = !level ? ManchesterEventShortHigh : ManchesterEventShortLow;
        }

        if(DURATION_DIFF(duration, duration_long) < duration_delta) {
            event = !level ? ManchesterEventLongHigh : ManchesterEventLongLow;
        }

        if(event == ManchesterEventReset) {
            subghz_protocol_decoder_hormann_bisecur_reset(instance);
        } else {
            bool new_level;

            if(manchester_advance_alt(
                   instance->manchester_saved_state,
                   event,
                   &instance->manchester_saved_state,
                   &new_level)) {
                subghz_protocol_decoder_hormann_bisecur_add_bit(instance, new_level);
            }
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_hormann_bisecur_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHormannBiSecur* instance = context;

    union {
        uint32_t full;
        uint8_t split[4];
    } hash = {0};
    size_t key_length = instance->generic.data_count_bit / 8;

    for(size_t i = 0; i < key_length; i++) {
        hash.split[i % sizeof(hash)] ^= instance->data[i];
    }

    return hash.full;
}

SubGhzProtocolStatus subghz_protocol_decoder_hormann_bisecur_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);

    SubGhzProtocolDecoderHormannBiSecur* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;

    do {
        res = subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
        if(res != SubGhzProtocolStatusOk) {
            break;
        }

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            res = SubGhzProtocolStatusErrorParserOthers;
            break;
        }

        uint16_t key_length = instance->generic.data_count_bit / 8;

        if(!flipper_format_update_hex(flipper_format, "Key", instance->data, key_length)) {
            FURI_LOG_E(TAG, "Unable to update Key");
            res = SubGhzProtocolStatusErrorParserKey;
            break;
        }
    } while(false);

    return res;
}

SubGhzProtocolStatus subghz_protocol_decoder_hormann_bisecur_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderHormannBiSecur* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    instance->generic.data = 0;

    if(instance->generic.data_count_bit !=
       subghz_protocol_hormann_bisecur_const.min_count_bit_for_found) {
        FURI_LOG_E(TAG, "Wrong number of bits in key");
        return SubGhzProtocolStatusErrorValueBitCount;
    }

    if(!flipper_format_rewind(flipper_format)) {
        FURI_LOG_E(TAG, "Rewind error");
        return SubGhzProtocolStatusErrorParserOthers;
    }

    size_t key_length = instance->generic.data_count_bit / 8;

    if(!flipper_format_read_hex(flipper_format, "Key", instance->data, key_length)) {
        FURI_LOG_E(TAG, "Unable to read Key in decoder");
        return SubGhzProtocolStatusErrorParserKey;
    }

    subghz_protocol_hormann_bisecur_parse_data(instance);

    return SubGhzProtocolStatusOk;
}

void subghz_protocol_decoder_hormann_bisecur_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderHormannBiSecur* instance = context;
    bool valid_crc = subghz_protocol_decoder_hormann_bisecur_check_crc(instance);

    furi_string_cat_printf(
        output,
        "%s\r\n"
        "%dbit CRC:0x%02X %s\r\n"
        "Type:0x%02X Sn:0x%08lX\r\n"
        "Key:%016llX\r\n"
        "Key:%016llX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        instance->crc,
        valid_crc ? "OK" : "WRONG",
        instance->type,
        instance->generic.serial,
        instance->generic.data,
        instance->generic.data_2);
}
