#include "mazda_v0.h"

#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>
#include "common.h"
#include "../encode_plugin.h"

static const SubGhzBlockConst subghz_protocol_mazda_v0_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

#define MAZDA_V0_UPLOAD_CAPACITY 0x184
#define MAZDA_V0_GAP_US          0xCB20
#define MAZDA_V0_SYNC_BYTE       0xD7
#define MAZDA_V0_TAIL_BYTE       0x5A
#define MAZDA_V0_PREAMBLE_ONES   16

typedef struct SubGhzProtocolDecoderMazdaV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint16_t preamble_count;
    uint8_t preamble_pattern;

    uint32_t serial;
    uint8_t button;
    uint32_t count;
} 
SubGhzProtocolDecoderMazdaV0;

typedef struct SubGhzProtocolEncoderMazdaV0 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t serial;
    uint8_t button;
    uint32_t count;
} 
SubGhzProtocolEncoderMazdaV0;

typedef enum {
    MazdaV0DecoderStepReset = 0,
    MazdaV0DecoderStepPreamble = 5,
    MazdaV0DecoderStepData = 6,
} MazdaV0DecoderStep;

static bool mazda_v0_get_event(uint32_t duration, bool level, ManchesterEvent* event);
static void mazda_v0_decode_key(SubGhzBlockGeneric* generic);


static SubGhzProtocolStatus mazda_v0_write_display(
    FlipperFormat* flipper_format,
    const char* protocol_name,
    uint8_t button);

const SubGhzProtocolDecoder subghz_protocol_mazda_v0_decoder = {
    .alloc = subghz_protocol_decoder_mazda_v0_alloc,
    .free = subghz_protocol_decoder_common_free,
    .feed = subghz_protocol_decoder_mazda_v0_feed,
    .reset = subghz_protocol_decoder_mazda_v0_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_mazda_v0_serialize,
    .deserialize = subghz_protocol_decoder_mazda_v0_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_mazda_v0_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_mazda_v0");

const SubGhzProtocol subghz_protocol_mazda_v0 = {
    .name = MAZDA_PROTOCOL_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,
    .decoder = &subghz_protocol_mazda_v0_decoder,
    .encoder = &subghz_protocol_mazda_v0_encoder_ext.vtable,
};

static uint8_t mazda_v0_popcount8(uint8_t x) {
    uint8_t count = 0;
    while(x) {
        count += x & 1;
        x >>= 1;
    }
    return count;
}

static void mazda_v0_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]) {
    for(size_t i = 0; i < 8; i++) {
        bytes[i] = (uint8_t)((data >> ((7 - i) * 8)) & 0xFF);
    }
}

static uint8_t mazda_v0_calculate_checksum(uint32_t serial, uint8_t button, uint32_t counter) {
    counter &= 0xFFFFFU;
    return (uint8_t)(((serial >> 24) & 0xFF) + ((serial >> 16) & 0xFF) + ((serial >> 8) & 0xFF) +
                     (serial & 0xFF) + ((counter >> 8) & 0xFF) + (counter & 0xFF) +
                     ((((counter >> 16) & 0x0F) | ((button & 0x0F) << 4)) & 0xFF));
}

static const char* mazda_v0_get_button_name(uint8_t button) {
    switch(button) {
    case 0x01:
        return "LOCK";
    case 0x02:
        return "UNLOCK";
    case 0x04:
        return "BOOT";
    case 0x08:
        return "REMOTE";
    default:
        return "??";
    }
}

static bool mazda_v0_get_event(uint32_t duration, bool level, ManchesterEvent* event) {
    const uint32_t tol = (uint32_t)subghz_protocol_mazda_v0_const.te_delta + 20U;

    if((uint32_t)DURATION_DIFF(duration, subghz_protocol_mazda_v0_const.te_short) < tol) {
        *event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
        return true;
    }

    if((uint32_t)DURATION_DIFF(duration, subghz_protocol_mazda_v0_const.te_long) < tol) {
        *event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
        return true;
    }

    return false;
}

static void mazda_v0_decode_key(SubGhzBlockGeneric* generic) {
    uint8_t data[8];
    mazda_v0_u64_to_bytes_be(generic->data, data);

    const bool parity = (mazda_v0_popcount8(data[7]) & 1) != 0;
    const uint8_t limit = parity ? 6 : 5;
    const uint8_t mask = data[limit];

    for(uint8_t i = 0; i < limit; i++) {
        data[i] ^= mask;
    }

    if(!parity) {
        data[6] ^= mask;
    }

    const uint8_t counter_lo = (data[5] & 0x55) | (data[6] & 0xAA);
    const uint8_t counter_mid = (data[6] & 0x55) | (data[5] & 0xAA);

    generic->serial = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                      ((uint32_t)data[2] << 8) | (uint32_t)data[3];
    generic->btn = (data[4] >> 4) & 0x0F;
    generic->cnt = (((uint32_t)data[4] & 0x0F) << 16) | ((uint32_t)counter_mid << 8) |
                   (uint32_t)counter_lo;
    generic->data_count_bit = subghz_protocol_mazda_v0_const.min_count_bit_for_found;
}

static SubGhzProtocolStatus mazda_v0_write_display(
    FlipperFormat* flipper_format,
    const char* protocol_name,
    uint8_t button) {
    SubGhzProtocolStatus status = SubGhzProtocolStatusOk;
    FuriString* display = furi_string_alloc();

    furi_string_printf(display, "%s - %s", protocol_name, mazda_v0_get_button_name(button));

    if(!flipper_format_write_string_cstr(flipper_format, "Disp", furi_string_get_cstr(display))) {
        status = SubGhzProtocolStatusErrorParserOthers;
    }

    furi_string_free(display);
    return status;
}

void* subghz_protocol_decoder_mazda_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolDecoderMazdaV0* instance = calloc(1, sizeof(SubGhzProtocolDecoderMazdaV0));
    furi_check(instance);

    instance->base.protocol = &subghz_protocol_mazda_v0;
    instance->generic.protocol_name = instance->base.protocol->name;

    return instance;
}



void subghz_protocol_decoder_mazda_v0_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaV0* instance = context;

    instance->decoder.parser_step = MazdaV0DecoderStepReset;
    instance->decoder.te_last = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->manchester_state = ManchesterStateStart1;
    instance->preamble_count = 0;
    instance->preamble_pattern = 0;
}

void subghz_protocol_decoder_mazda_v0_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);

    SubGhzProtocolDecoderMazdaV0* instance = context;
    ManchesterEvent event = ManchesterEventReset;
    bool data = false;

    switch(instance->decoder.parser_step) {
    case MazdaV0DecoderStepReset:
        if(level && ((uint32_t)DURATION_DIFF(duration, subghz_protocol_mazda_v0_const.te_short) <
                     (uint32_t)subghz_protocol_mazda_v0_const.te_delta + 20U)) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = MazdaV0DecoderStepPreamble;
            instance->manchester_state = ManchesterStateMid1;
            instance->preamble_count = 0;
            instance->preamble_pattern = 0;
        }
        break;

    case MazdaV0DecoderStepPreamble:
        if(!mazda_v0_get_event(duration, level, &event)) {
            instance->decoder.parser_step = MazdaV0DecoderStepReset;
            break;
        }

        if(manchester_advance(
               instance->manchester_state, event, &instance->manchester_state, &data)) {
            instance->preamble_pattern = (instance->preamble_pattern << 1) | (data ? 1 : 0);

            if(data) {
                instance->preamble_count++;
            } else if(instance->preamble_count <= MAZDA_V0_PREAMBLE_ONES - 1U) {
                instance->preamble_count = 0;
                instance->preamble_pattern = 0;
                break;
            }

            if((instance->preamble_pattern == MAZDA_V0_SYNC_BYTE) &&
               (instance->preamble_count > MAZDA_V0_PREAMBLE_ONES - 1U)) {
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = MazdaV0DecoderStepData;
            }
        }
        break;

    case MazdaV0DecoderStepData:
        if(!mazda_v0_get_event(duration, level, &event)) {
            instance->decoder.parser_step = MazdaV0DecoderStepReset;
            break;
        }

        if(manchester_advance(
               instance->manchester_state, event, &instance->manchester_state, &data)) {
            subghz_protocol_blocks_add_bit(&instance->decoder, data);

            if(instance->decoder.decode_count_bit ==
               subghz_protocol_mazda_v0_const.min_count_bit_for_found) {
                instance->generic.data = ~instance->decoder.decode_data;
                mazda_v0_decode_key(&instance->generic);

                if(mazda_v0_calculate_checksum(
                       instance->generic.serial, instance->generic.btn, instance->generic.cnt) ==
                   (uint8_t)instance->generic.data) {
                    instance->serial = instance->generic.serial;
                    instance->button = instance->generic.btn;
                    instance->count = instance->generic.cnt;

                    if(instance->base.callback) {
                        instance->base.callback(&instance->base, instance->base.context);
                    }
                }

                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->preamble_count = 0;
                instance->preamble_pattern = 0;
                instance->manchester_state = ManchesterStateStart1;
                instance->decoder.te_last = 0;
                instance->decoder.parser_step = MazdaV0DecoderStepReset;
            }
        }
        break;
    }
}



SubGhzProtocolStatus subghz_protocol_decoder_mazda_v0_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaV0* instance = context;

    mazda_v0_decode_key(&instance->generic);
    instance->serial = instance->generic.serial;
    instance->button = instance->generic.btn;
    instance->count = instance->generic.cnt;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        uint32_t chk =
            mazda_v0_calculate_checksum(instance->serial, instance->button, instance->count);
        flipper_format_write_uint32(flipper_format, "Checksum", &chk, 1);

        flipper_format_write_uint32(flipper_format, "Serial", &instance->serial, 1);

        uint32_t temp = instance->button;
        flipper_format_write_uint32(flipper_format, "Btn", &temp, 1);

        flipper_format_write_uint32(flipper_format, "Cnt", &instance->count, 1);

        ret = mazda_v0_write_display(
            flipper_format, instance->generic.protocol_name, instance->button);
    }

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_mazda_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaV0* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_mazda_v0_const.min_count_bit_for_found);

    if(ret == SubGhzProtocolStatusOk) {
        flipper_format_rewind(flipper_format);

        flipper_format_read_uint32(flipper_format, "Serial", &instance->serial, 1);
        instance->generic.serial = instance->serial;

        uint32_t btn_temp = 0;
        flipper_format_read_uint32(flipper_format, "Btn", &btn_temp, 1);
        instance->button = (uint8_t)btn_temp;
        instance->generic.btn = instance->button;

        flipper_format_read_uint32(flipper_format, "Cnt", &instance->count, 1);
        instance->generic.cnt = instance->count;

        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(instance->generic.btn);
        }
        subghz_custom_btn_set_max(4);
    }

    return ret;
}

/* Layout asserts placed at EOF: these structs typedef the bare name
 * later in this file, so the assert must follow all type definitions. */
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderMazdaV0);
SUBGHZ_ASSERT_ENCODER_COMMON_LAYOUT(SubGhzProtocolEncoderMazdaV0);
