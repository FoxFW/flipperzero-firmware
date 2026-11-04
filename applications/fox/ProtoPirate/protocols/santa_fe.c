#include "santa_fe.h"
#include "protocols_common.h"

// Ported from KAT src/protocols/santa_fe.rs
#define SF_TE_SHORT    125
#define SF_TE_LONG     375
#define SF_TE_DELTA    100
#define SF_SYNC_US     12000
#define SF_SYNC_DELTA  1800
#define SF_GAP_US      15000
#define SF_BIT_COUNT   80
#define SF_BYTES       10

static const SubGhzBlockConst subghz_protocol_santa_fe_const = {
    .te_short = SF_TE_SHORT,
    .te_long = SF_TE_LONG,
    .te_delta = SF_TE_DELTA,
    .min_count_bit_for_found = SF_BIT_COUNT,
};

#if PROTOPIRATE_WITH_ENCODER
#define SF_UPLOAD_CAPACITY ((2U) + (2U * SF_BIT_COUNT) + 2U)
_Static_assert(
    SF_UPLOAD_CAPACITY <= PP_SHARED_UPLOAD_CAPACITY,
    "SF_UPLOAD_CAPACITY exceeds shared upload slab");
#endif

typedef enum {
    SfDecoderStepReset = 0,
    SfDecoderStepCheckSyncHi,
    SfDecoderStepSaveDuration,
    SfDecoderStepCheckDuration,
} SfDecoderStep;

struct SubGhzProtocolDecoderSantaFe {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t data[SF_BYTES];
    uint16_t bit_count;
    uint32_t rolling;
};

#if PROTOPIRATE_WITH_ENCODER
struct SubGhzProtocolEncoderSantaFe {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint32_t rolling;
};
#endif

static uint8_t sf_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(int b = 0; b < 8; b++) {
            if(crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x31);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

static const char* sf_button_name(uint8_t b) {
    switch(b) {
    case 0x01:
        return "Lock";
    case 0x02:
        return "Unlock";
    case 0x04:
        return "Trunk";
    case 0x08:
        return "Panic";
    default:
        return "Unknown";
    }
}

static void sf_add_bit(SubGhzProtocolDecoderSantaFe* instance, bool bit) {
    if(instance->bit_count < SF_BIT_COUNT) {
        uint16_t byte_idx = instance->bit_count / 8;
        uint8_t bit_idx = 7 - (instance->bit_count % 8);
        if(bit) instance->data[byte_idx] |= (uint8_t)(1U << bit_idx);
        instance->bit_count++;
    }
}

static void sf_process(SubGhzProtocolDecoderSantaFe* instance) {
    if(instance->bit_count < SF_BIT_COUNT) return;
    if(instance->data[9] != sf_crc8(instance->data, 9)) return;

    uint32_t rolling = ((uint32_t)instance->data[0] << 24) | ((uint32_t)instance->data[1] << 16) |
                       ((uint32_t)instance->data[2] << 8) | instance->data[3];
    uint32_t serial = ((uint32_t)instance->data[4] << 16) | ((uint32_t)instance->data[5] << 8) |
                      instance->data[6];
    uint8_t counter = instance->data[7];
    uint8_t button = instance->data[8];

    instance->rolling = rolling;
    // 64-bit hash projection: rolling<<32 | serial<<8 | counter (KAT `packed` bytes 0..7)
    instance->generic.data = ((uint64_t)rolling << 32) | ((uint64_t)serial << 8) | counter;
    instance->generic.data_count_bit = SF_BIT_COUNT;
    instance->generic.serial = serial;
    instance->generic.btn = button;
    instance->generic.cnt = counter;

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void* subghz_protocol_decoder_santa_fe_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderSantaFe* instance = calloc(1, sizeof(SubGhzProtocolDecoderSantaFe));
    furi_check(instance);
    instance->base.protocol = &santa_fe_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_santa_fe_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderSantaFe* instance = context;
    instance->decoder.parser_step = SfDecoderStepReset;
    memset(instance->data, 0, sizeof(instance->data));
    instance->bit_count = 0;
}

void subghz_protocol_decoder_santa_fe_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderSantaFe* instance = context;
    const uint32_t te_short = SF_TE_SHORT;
    const uint32_t te_long = SF_TE_LONG;
    const uint32_t te_delta = SF_TE_DELTA;

    switch(instance->decoder.parser_step) {
    case SfDecoderStepReset:
        if(level && DURATION_DIFF(duration, te_long) < te_delta) {
            instance->decoder.parser_step = SfDecoderStepCheckSyncHi;
        }
        break;

    case SfDecoderStepCheckSyncHi:
        if(!level && DURATION_DIFF(duration, SF_SYNC_US) < SF_SYNC_DELTA) {
            instance->decoder.parser_step = SfDecoderStepSaveDuration;
            memset(instance->data, 0, sizeof(instance->data));
            instance->bit_count = 0;
        } else {
            instance->decoder.parser_step = SfDecoderStepReset;
        }
        break;

    case SfDecoderStepSaveDuration:
        if(level) {
            if(DURATION_DIFF(duration, te_long) < te_delta) {
                sf_add_bit(instance, true);
                instance->decoder.parser_step = SfDecoderStepCheckDuration;
            } else if(DURATION_DIFF(duration, te_short) < te_delta) {
                sf_add_bit(instance, false);
                instance->decoder.parser_step = SfDecoderStepCheckDuration;
            } else if(duration > 3000) {
                if(instance->bit_count >= SF_BIT_COUNT) sf_process(instance);
                instance->decoder.parser_step = SfDecoderStepReset;
            } else {
                instance->decoder.parser_step = SfDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = SfDecoderStepReset;
        }
        break;

    case SfDecoderStepCheckDuration:
        if(!level) {
            if(DURATION_DIFF(duration, te_short) < te_delta ||
               DURATION_DIFF(duration, te_long) < te_delta) {
                if(instance->bit_count >= SF_BIT_COUNT) {
                    sf_process(instance);
                    instance->decoder.parser_step = SfDecoderStepReset;
                } else {
                    instance->decoder.parser_step = SfDecoderStepSaveDuration;
                }
            } else if(duration > 3000) {
                if(instance->bit_count >= SF_BIT_COUNT) sf_process(instance);
                instance->decoder.parser_step = SfDecoderStepReset;
            } else {
                instance->decoder.parser_step = SfDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = SfDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_santa_fe_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderSantaFe* instance = context;
    SubGhzBlockDecoder decoder = {
        .decode_data = instance->generic.data,
        .decode_count_bit = instance->generic.data_count_bit,
    };
    return subghz_protocol_blocks_get_hash_data(&decoder, (decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_santa_fe_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderSantaFe* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret == SubGhzProtocolStatusOk) {
        pp_serialize_fields(
            flipper_format,
            PP_FIELD_SERIAL | PP_FIELD_BTN | PP_FIELD_CNT,
            instance->generic.serial,
            instance->generic.btn,
            instance->generic.cnt,
            0);
        uint32_t rolling = instance->rolling;
        flipper_format_write_uint32(flipper_format, "Rolling", &rolling, 1);
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_santa_fe_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderSantaFe* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_santa_fe_const.min_count_bit_for_found);
    if(ret == SubGhzProtocolStatusOk) {
        instance->generic.serial = (uint32_t)((instance->generic.data >> 8) & 0xFFFFFF);
        instance->generic.cnt = (uint16_t)(instance->generic.data & 0xFF);
        instance->rolling = (uint32_t)(instance->generic.data >> 32);
        uint32_t rolling = instance->rolling;
        if(flipper_format_read_uint32(flipper_format, "Rolling", &rolling, 1)) {
            instance->rolling = rolling;
        }
    }
    return ret;
}

void subghz_protocol_decoder_santa_fe_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderSantaFe* instance = context;
    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Roll:%08lX\r\n"
        "Sn:%06lX Cnt:%02X\r\n"
        "Btn:%s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)instance->rolling,
        (unsigned long)(instance->generic.serial & 0xFFFFFF),
        (unsigned int)(instance->generic.cnt & 0xFF),
        sf_button_name(instance->generic.btn));
}

#if PROTOPIRATE_WITH_ENCODER
static bool sf_build_upload(SubGhzProtocolEncoderSantaFe* instance, uint8_t button) {
    uint8_t pkt[SF_BYTES];
    pkt[0] = (uint8_t)(instance->rolling >> 24);
    pkt[1] = (uint8_t)(instance->rolling >> 16);
    pkt[2] = (uint8_t)(instance->rolling >> 8);
    pkt[3] = (uint8_t)(instance->rolling);
    pkt[4] = (uint8_t)(instance->generic.serial >> 16);
    pkt[5] = (uint8_t)(instance->generic.serial >> 8);
    pkt[6] = (uint8_t)(instance->generic.serial);
    pkt[7] = (uint8_t)(instance->generic.cnt & 0xFF);
    pkt[8] = button;
    pkt[9] = sf_crc8(pkt, 9);

    LevelDuration* up = instance->encoder.upload;
    const size_t cap = SF_UPLOAD_CAPACITY;
    size_t index = 0;

    index = pp_emit(up, index, cap, true, SF_TE_LONG);
    index = pp_emit(up, index, cap, false, SF_SYNC_US);

    for(int byte = 0; byte < SF_BYTES; byte++) {
        for(int bit = 7; bit >= 0; bit--) {
            if((pkt[byte] >> bit) & 1U) {
                index = pp_emit(up, index, cap, true, SF_TE_LONG);
                index = pp_emit(up, index, cap, false, SF_TE_SHORT);
            } else {
                index = pp_emit(up, index, cap, true, SF_TE_SHORT);
                index = pp_emit(up, index, cap, false, SF_TE_LONG);
            }
        }
    }
    index = pp_emit(up, index, cap, false, SF_GAP_US);

    if(index > cap) return false;
    instance->encoder.front = 0;
    instance->encoder.size_upload = index;
    return true;
}

void* subghz_protocol_encoder_santa_fe_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderSantaFe* instance = calloc(1, sizeof(SubGhzProtocolEncoderSantaFe));
    furi_check(instance);
    instance->base.protocol = &santa_fe_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 3;
    pp_encoder_buffer_ensure(instance, SF_UPLOAD_CAPACITY);
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_santa_fe_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderSantaFe* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;

    if(pp_verify_protocol_name(flipper_format, instance->base.protocol->name) !=
       SubGhzProtocolStatusOk) {
        return SubGhzProtocolStatusError;
    }
    SubGhzProtocolStatus load_st = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, SF_BIT_COUNT);
    if(load_st != SubGhzProtocolStatusOk) {
        return load_st;
    }

    uint32_t serial = (uint32_t)((instance->generic.data >> 8) & 0xFFFFFF);
    uint32_t btn = instance->generic.btn;
    uint32_t cnt = (uint32_t)(instance->generic.data & 0xFF);
    instance->rolling = (uint32_t)(instance->generic.data >> 32);
    uint32_t rolling = instance->rolling;
    if(flipper_format_read_uint32(flipper_format, "Rolling", &rolling, 1)) instance->rolling = rolling;
    pp_encoder_read_fields(flipper_format, &serial, &btn, &cnt, NULL);

    instance->generic.serial = serial;
    instance->generic.btn = (uint8_t)btn;
    instance->generic.cnt = (uint16_t)(cnt & 0xFF);

    instance->encoder.repeat = pp_encoder_read_repeat(flipper_format, 3);
    if(!sf_build_upload(instance, (uint8_t)btn)) {
        return SubGhzProtocolStatusError;
    }
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}
#endif

const SubGhzProtocolDecoder subghz_protocol_santa_fe_decoder = {
    .alloc = subghz_protocol_decoder_santa_fe_alloc,
    .free = pp_decoder_free_default,
    .feed = subghz_protocol_decoder_santa_fe_feed,
    .reset = subghz_protocol_decoder_santa_fe_reset,
    .get_hash_data = subghz_protocol_decoder_santa_fe_get_hash_data,
    .serialize = subghz_protocol_decoder_santa_fe_serialize,
    .deserialize = subghz_protocol_decoder_santa_fe_deserialize,
    .get_string = subghz_protocol_decoder_santa_fe_get_string,
};

#if PROTOPIRATE_WITH_ENCODER
const SubGhzProtocolEncoder subghz_protocol_santa_fe_encoder = {
    .alloc = subghz_protocol_encoder_santa_fe_alloc,
    .free = pp_encoder_free,
    .deserialize = subghz_protocol_encoder_santa_fe_deserialize,
    .stop = pp_encoder_stop,
    .yield = pp_encoder_yield,
};
#else
const SubGhzProtocolEncoder subghz_protocol_santa_fe_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};
#endif

const SubGhzProtocol santa_fe_protocol = {
    .name = SANTA_FE_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_santa_fe_decoder,
    .encoder = &subghz_protocol_santa_fe_encoder,
};
