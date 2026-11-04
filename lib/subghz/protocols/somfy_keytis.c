#include "somfy_keytis.h"

#include "common.h"
#include "../encode_plugin.h"
#include <lib/toolbox/manchester_decoder.h>

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocolSomfyKeytis"

static bool somfy_keytis_raw_replay = false; // #16: TxRaw verbatim replay (freeze counter)

static const SubGhzBlockConst subghz_protocol_somfy_keytis_const = {
    .te_short = 640,
    .te_long = 1280,
    .te_delta = 250,
    .min_count_bit_for_found = 80,
};

struct SubGhzProtocolDecoderSomfyKeytis {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;
    ManchesterState manchester_saved_state;
    uint32_t press_duration_counter;
};

struct SubGhzProtocolEncoderSomfyKeytis {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderSomfyKeytis);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderSomfyKeytis);

typedef enum {
    SomfyKeytisDecoderStepReset = 0,
    SomfyKeytisDecoderStepCheckPreambula,
    SomfyKeytisDecoderStepFoundPreambula,
    SomfyKeytisDecoderStepStartDecode,
    SomfyKeytisDecoderStepDecoderData,
} SomfyKeytisDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_somfy_keytis_decoder = {
    .alloc = subghz_protocol_decoder_somfy_keytis_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_somfy_keytis_feed,
    .reset = subghz_protocol_decoder_somfy_keytis_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_somfy_keytis_serialize,
    .deserialize = subghz_protocol_decoder_somfy_keytis_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

/* host bridge: firmware functions/state the SD encoder uses (handed over at load) */
typedef struct {
    void* somfy_keytis_raw_replay;
} enc_somfy_keytis_host_t;
static const enc_somfy_keytis_host_t enc_somfy_keytis_host = {
    .somfy_keytis_raw_replay = (void*)&somfy_keytis_raw_replay,
};
static const SubGhzEncoderExternal subghz_protocol_somfy_keytis_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_somfy_keytis", &enc_somfy_keytis_host);

const SubGhzProtocol subghz_protocol_somfy_keytis = {
    .name = SUBGHZ_PROTOCOL_SOMFY_KEYTIS_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,

    .decoder = &subghz_protocol_somfy_keytis_decoder,
    .encoder = &subghz_protocol_somfy_keytis_encoder_ext.vtable,
};

void* subghz_protocol_decoder_somfy_keytis_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderSomfyKeytis), &subghz_protocol_somfy_keytis);
}

void subghz_protocol_decoder_somfy_keytis_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderSomfyKeytis* instance = context;
    instance->decoder.parser_step = SomfyKeytisDecoderStepReset;
    manchester_advance(
        instance->manchester_saved_state,
        ManchesterEventReset,
        &instance->manchester_saved_state,
        NULL);
}

static uint8_t subghz_protocol_somfy_keytis_get_btn_code(void) {
    uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t original_btn_code = subghz_custom_btn_get_original();
    uint8_t btn = original_btn_code;

    if((custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) && (original_btn_code != 0)) {
        btn = original_btn_code;
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_UP) {
        switch(original_btn_code) {
        case 0x4:
            btn = 0x3;
            break;
        case 0x3:
            btn = 0x4;
            break;
        default:
            break;
        }
    }
    return btn;
}

static bool
    subghz_protocol_somfy_keytis_gen_data(SubGhzProtocolEncoderSomfyKeytis* instance, uint8_t btn) {
    UNUSED(btn);
    uint64_t data = instance->generic.data ^ (instance->generic.data >> 8);
    instance->generic.btn = (data >> 48) & 0xF;
    instance->generic.cnt = (data >> 24) & 0xFFFF;
    instance->generic.serial = data & 0xFFFFFF;

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->generic.btn);
    }
    instance->generic.btn = subghz_protocol_somfy_keytis_get_btn_code();

    if(subghz_block_generic_global_button_override_get(&instance->generic.btn))
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", instance->generic.btn);

    if(somfy_keytis_raw_replay) {
        /* #16: freeze counter on TxRaw verbatim replay */
    } else if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF) {

        if(!subghz_block_generic_global_counter_override_get(&instance->generic.cnt)) {

            if((instance->generic.cnt + furi_hal_subghz_get_rolling_counter_mult()) > 0xFFFF) {
                instance->generic.cnt = 0;
            } else {
                instance->generic.cnt += furi_hal_subghz_get_rolling_counter_mult();
            }
        }
    } else {
        if((instance->generic.cnt + 0x1) > 0xFFFF) {
            instance->generic.cnt = 0;
        } else if(instance->generic.cnt >= 0x1 && instance->generic.cnt != 0xFFFE) {
            instance->generic.cnt = 0xFFFE;
        } else {
            instance->generic.cnt++;
        }
    }

    uint8_t frame[10];
    frame[0] = (0xA << 4) | instance->generic.btn;
    frame[1] = 0xF << 4;
    frame[2] = instance->generic.cnt >> 8;
    frame[3] = instance->generic.cnt;
    frame[4] = instance->generic.serial >> 16;
    frame[5] = instance->generic.serial >> 8;
    frame[6] = instance->generic.serial;
    frame[7] = 0xC4;
    frame[8] = 0x00;
    frame[9] = 0x19;

    uint8_t checksum = 0;
    for(uint8_t i = 0; i < 7; i++) {
        checksum = checksum ^ frame[i] ^ (frame[i] >> 4);
    }
    checksum &= 0xF;

    frame[1] |= checksum;

    for(uint8_t i = 1; i < 7; i++) {
        frame[i] ^= frame[i - 1];
    }
    data = 0;
    for(uint8_t i = 0; i < 7; ++i) {
        data <<= 8;
        data |= frame[i];
    }
    instance->generic.data = data;
    data = 0;
    for(uint8_t i = 7; i < 10; ++i) {
        data <<= 8;
        data |= frame[i];
    }
    instance->generic.data_2 = data;
    return true;
}

bool subghz_protocol_somfy_keytis_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolEncoderSomfyKeytis* instance = context;
    instance->generic.serial = serial;
    instance->generic.cnt = cnt;
    instance->generic.data_count_bit = 80;
    bool res = subghz_protocol_somfy_keytis_gen_data(instance, btn);
    if(res) {
        return SubGhzProtocolStatusOk ==
               subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    }
    return res;
}

static uint8_t subghz_protocol_somfy_keytis_crc(uint64_t data) {
    uint8_t crc = 0;
    data &= 0xFFF0FFFFFFFFFF;
    for(uint8_t i = 0; i < 56; i += 8) {
        crc = crc ^ data >> i ^ (data >> (i + 4));
    }
    return crc & 0xf;
}

void subghz_protocol_decoder_somfy_keytis_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderSomfyKeytis* instance = context;

    ManchesterEvent event = ManchesterEventReset;
    switch(instance->decoder.parser_step) {
    case SomfyKeytisDecoderStepReset:
        if((level) && DURATION_DIFF(duration, subghz_protocol_somfy_keytis_const.te_short * 4) <
                          subghz_protocol_somfy_keytis_const.te_delta * 4) {
            instance->decoder.parser_step = SomfyKeytisDecoderStepFoundPreambula;
            instance->header_count++;
        }
        break;
    case SomfyKeytisDecoderStepFoundPreambula:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_somfy_keytis_const.te_short * 4) <
                        subghz_protocol_somfy_keytis_const.te_delta * 4)) {
            instance->decoder.parser_step = SomfyKeytisDecoderStepCheckPreambula;
        } else {
            instance->header_count = 0;
            instance->decoder.parser_step = SomfyKeytisDecoderStepReset;
        }
        break;
    case SomfyKeytisDecoderStepCheckPreambula:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_somfy_keytis_const.te_short * 4) <
               subghz_protocol_somfy_keytis_const.te_delta * 4) {
                instance->decoder.parser_step = SomfyKeytisDecoderStepFoundPreambula;
                instance->header_count++;
            } else if(
                (instance->header_count > 1) &&
                (DURATION_DIFF(duration, subghz_protocol_somfy_keytis_const.te_short * 7) <
                 subghz_protocol_somfy_keytis_const.te_delta * 4)) {
                instance->decoder.parser_step = SomfyKeytisDecoderStepDecoderData;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->press_duration_counter = 0;
                manchester_advance(
                    instance->manchester_saved_state,
                    ManchesterEventReset,
                    &instance->manchester_saved_state,
                    NULL);
                manchester_advance(
                    instance->manchester_saved_state,
                    ManchesterEventLongHigh,
                    &instance->manchester_saved_state,
                    NULL);
            }
        }

        break;

    case SomfyKeytisDecoderStepDecoderData:
        if(!level) {
            if(DURATION_DIFF(duration, subghz_protocol_somfy_keytis_const.te_short) <
               subghz_protocol_somfy_keytis_const.te_delta) {
                event = ManchesterEventShortLow;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_somfy_keytis_const.te_long) <
                subghz_protocol_somfy_keytis_const.te_delta) {
                event = ManchesterEventLongLow;
            } else if(
                duration >= (subghz_protocol_somfy_keytis_const.te_long +
                             subghz_protocol_somfy_keytis_const.te_delta)) {
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_somfy_keytis_const.min_count_bit_for_found) {

                    uint64_t data_tmp = instance->generic.data ^ (instance->generic.data >> 8);
                    if(((data_tmp >> 40) & 0xF) == subghz_protocol_somfy_keytis_crc(data_tmp)) {
                        instance->generic.data = instance->decoder.decode_data;
                        instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                    }
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                manchester_advance(
                    instance->manchester_saved_state,
                    ManchesterEventReset,
                    &instance->manchester_saved_state,
                    NULL);
                manchester_advance(
                    instance->manchester_saved_state,
                    ManchesterEventLongHigh,
                    &instance->manchester_saved_state,
                    NULL);
                instance->decoder.parser_step = SomfyKeytisDecoderStepReset;
            } else {
                instance->decoder.parser_step = SomfyKeytisDecoderStepReset;
            }
        } else {
            if(DURATION_DIFF(duration, subghz_protocol_somfy_keytis_const.te_short) <
               subghz_protocol_somfy_keytis_const.te_delta) {
                event = ManchesterEventShortHigh;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_somfy_keytis_const.te_long) <
                subghz_protocol_somfy_keytis_const.te_delta) {
                event = ManchesterEventLongHigh;
            } else {
                instance->decoder.parser_step = SomfyKeytisDecoderStepReset;
            }
        }
        if(event != ManchesterEventReset) {
            bool data;
            bool data_ok = manchester_advance(
                instance->manchester_saved_state, event, &instance->manchester_saved_state, &data);

            if(data_ok) {
                if(instance->decoder.decode_count_bit < 56) {
                    instance->decoder.decode_data = (instance->decoder.decode_data << 1) | data;
                } else {
                    instance->press_duration_counter = (instance->press_duration_counter << 1) |
                                                       data;
                }

                instance->decoder.decode_count_bit++;
            }
        }
        break;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_somfy_keytis_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderSomfyKeytis* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if((ret == SubGhzProtocolStatusOk) &&
       !flipper_format_write_uint32(
           flipper_format, "Duration_Counter", &instance->press_duration_counter, 1)) {
        FURI_LOG_E(TAG, "Unable to add Duration_Counter");
        ret = SubGhzProtocolStatusErrorParserOthers;
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_somfy_keytis_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderSomfyKeytis* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_somfy_keytis_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        if(!flipper_format_read_uint32(
               flipper_format,
               "Duration_Counter",
               (uint32_t*)&instance->press_duration_counter,
               1)) {
            FURI_LOG_E(TAG, "Missing Duration_Counter");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
    } while(false);

    return ret;
}

