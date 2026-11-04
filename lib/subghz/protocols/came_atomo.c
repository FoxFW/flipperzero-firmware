#include "came_atomo.h"

#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocoCameAtomo"

static bool came_atomo_raw_replay = false; // #16: TxRaw verbatim replay (freeze counter)

static bool bypass = false;

static const SubGhzBlockConst subghz_protocol_came_atomo_const = {
    .te_short = 600,
    .te_long = 1200,
    .te_delta = 250,
    .min_count_bit_for_found = 62,
};

struct SubGhzProtocolDecoderCameAtomo {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_saved_state;
};

struct SubGhzProtocolEncoderCameAtomo {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderCameAtomo);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderCameAtomo);

typedef enum {
    CameAtomoDecoderStepReset = 0,
    CameAtomoDecoderStepDecoderData,
} CameAtomoDecoderStep;

static uint8_t came_atomo_counter_mode = 0;

const SubGhzProtocolDecoder subghz_protocol_came_atomo_decoder = {
    .alloc = subghz_protocol_decoder_came_atomo_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_came_atomo_feed,
    .reset = subghz_protocol_decoder_came_atomo_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_common_serialize,
    .deserialize = subghz_protocol_decoder_came_atomo_deserialize,
    .get_string = subghz_protocol_decoder_came_atomo_get_string,
};

/* host bridge: firmware functions/state the SD encoder uses (handed over at load) */
typedef struct {
    void* bypass;
    void* came_atomo_counter_mode;
    void* came_atomo_raw_replay;
} enc_came_atomo_host_t;
static const enc_came_atomo_host_t enc_came_atomo_host = {
    .bypass = (void*)&bypass,
    .came_atomo_counter_mode = (void*)&came_atomo_counter_mode,
    .came_atomo_raw_replay = (void*)&came_atomo_raw_replay,
};
static const SubGhzEncoderExternal subghz_protocol_came_atomo_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_came_atomo", &enc_came_atomo_host);

const SubGhzProtocol subghz_protocol_came_atomo = {
    .name = SUBGHZ_PROTOCOL_CAME_ATOMO_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay,

    .decoder = &subghz_protocol_came_atomo_decoder,
    .encoder = &subghz_protocol_came_atomo_encoder_ext.vtable,
};

static void subghz_protocol_came_atomo_remote_controller(SubGhzBlockGeneric* instance);


bool subghz_protocol_came_atomo_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint16_t cnt,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolEncoderCameAtomo* instance = context;
    instance->generic.btn = 0x1;
    instance->generic.serial = serial;
    instance->generic.cnt = cnt;
    instance->generic.cnt_2 = 0x7e;
    instance->generic.data_count_bit = 62;
    instance->generic.data_2 =
        ((uint64_t)0x7e << 56 | (uint64_t)cnt << 40 | (uint64_t)serial << 8);

    uint8_t pack[8] = {};

    pack[0] = (instance->generic.cnt_2);
    pack[1] = (instance->generic.cnt >> 8);
    pack[2] = (instance->generic.cnt & 0xFF);
    pack[3] = ((instance->generic.data_2 >> 32) & 0xFF);
    pack[4] = ((instance->generic.data_2 >> 24) & 0xFF);
    pack[5] = ((instance->generic.data_2 >> 16) & 0xFF);
    pack[6] = ((instance->generic.data_2 >> 8) & 0xFF);
    pack[7] = (instance->generic.data_2 & 0xFF);

    atomo_encrypt(pack);
    uint32_t hi = pack[0] << 24 | pack[1] << 16 | pack[2] << 8 | pack[3];
    uint32_t lo = pack[4] << 24 | pack[5] << 16 | pack[6] << 8 | pack[7];
    instance->generic.data = (uint64_t)hi << 32 | lo;

    instance->generic.data ^= 0xFFFFFFFFFFFFFFFF;
    instance->generic.data >>= 4;
    instance->generic.data &= 0xFFFFFFFFFFFFFFF;

    return SubGhzProtocolStatusOk ==
           subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

void* subghz_protocol_decoder_came_atomo_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderCameAtomo), &subghz_protocol_came_atomo);
}

void subghz_protocol_decoder_came_atomo_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderCameAtomo* instance = context;
    instance->decoder.parser_step = CameAtomoDecoderStepReset;
    manchester_advance(
        instance->manchester_saved_state,
        ManchesterEventReset,
        &instance->manchester_saved_state,
        NULL);
}

void subghz_protocol_decoder_came_atomo_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderCameAtomo* instance = context;

    ManchesterEvent event = ManchesterEventReset;
    switch(instance->decoder.parser_step) {
    case CameAtomoDecoderStepReset:

        if((!level) && ((DURATION_DIFF(duration, subghz_protocol_came_atomo_const.te_long * 10) <
                         subghz_protocol_came_atomo_const.te_delta * 20) ||
                        (DURATION_DIFF(duration, subghz_protocol_came_atomo_const.te_long * 60) <
                         subghz_protocol_came_atomo_const.te_delta * 40))) {

            instance->decoder.parser_step = CameAtomoDecoderStepDecoderData;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 1;
            manchester_advance(
                instance->manchester_saved_state,
                ManchesterEventReset,
                &instance->manchester_saved_state,
                NULL);
            manchester_advance(
                instance->manchester_saved_state,
                ManchesterEventShortLow,
                &instance->manchester_saved_state,
                NULL);
        }
        break;
    case CameAtomoDecoderStepDecoderData:
        if(!level) {
            if(DURATION_DIFF(duration, subghz_protocol_came_atomo_const.te_short) <
               subghz_protocol_came_atomo_const.te_delta) {
                event = ManchesterEventShortLow;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_came_atomo_const.te_long) <
                subghz_protocol_came_atomo_const.te_delta) {
                event = ManchesterEventLongLow;
            } else if(
                duration >= ((uint32_t)subghz_protocol_came_atomo_const.te_long * 2 +
                             subghz_protocol_came_atomo_const.te_delta)) {
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_came_atomo_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 1;
                manchester_advance(
                    instance->manchester_saved_state,
                    ManchesterEventReset,
                    &instance->manchester_saved_state,
                    NULL);
                manchester_advance(
                    instance->manchester_saved_state,
                    ManchesterEventShortLow,
                    &instance->manchester_saved_state,
                    NULL);
            } else {
                instance->decoder.parser_step = CameAtomoDecoderStepReset;
            }
        } else {
            if(DURATION_DIFF(duration, subghz_protocol_came_atomo_const.te_short) <
               subghz_protocol_came_atomo_const.te_delta) {
                event = ManchesterEventShortHigh;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_came_atomo_const.te_long) <
                subghz_protocol_came_atomo_const.te_delta) {
                event = ManchesterEventLongHigh;
            } else {
                instance->decoder.parser_step = CameAtomoDecoderStepReset;
            }
        }
        if(event != ManchesterEventReset) {
            bool data;
            bool data_ok = manchester_advance(
                instance->manchester_saved_state, event, &instance->manchester_saved_state, &data);

            if(data_ok) {
                instance->decoder.decode_data = (instance->decoder.decode_data << 1) | !data;
                instance->decoder.decode_count_bit++;
            }
        }
        break;
    }
}

static void subghz_protocol_came_atomo_remote_controller(SubGhzBlockGeneric* instance) {

    instance->data ^= 0xFFFFFFFFFFFFFFFF;
    instance->data <<= 4;

    uint8_t pack[8] = {};
    pack[0] = (instance->data >> 56);
    pack[1] = ((instance->data >> 48) & 0xFF);
    pack[2] = ((instance->data >> 40) & 0xFF);
    pack[3] = ((instance->data >> 32) & 0xFF);
    pack[4] = ((instance->data >> 24) & 0xFF);
    pack[5] = ((instance->data >> 16) & 0xFF);
    pack[6] = ((instance->data >> 8) & 0xFF);
    pack[7] = (instance->data & 0xFF);

    atomo_decrypt(pack);

    instance->cnt_2 = pack[0];
    instance->cnt = (uint16_t)pack[1] << 8 | pack[2];
    instance->serial = (uint32_t)(pack[3]) << 24 | pack[4] << 16 | pack[5] << 8 | pack[6];

    uint8_t btn_decode = (pack[7] >> 4);
    if(btn_decode == 0x0) {
        instance->btn = 0x1;
    } else if(btn_decode == 0x2) {
        instance->btn = 0x2;
    } else if(btn_decode == 0x4) {
        instance->btn = 0x3;
    } else if(btn_decode == 0x6) {
        instance->btn = 0x4;
    }

    uint32_t hi = pack[0] << 24 | pack[1] << 16 | pack[2] << 8 | pack[3];
    uint32_t lo = pack[4] << 24 | pack[5] << 16 | pack[6] << 8 | pack[7];
    instance->data_2 = (uint64_t)hi << 32 | lo;

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(3);
}

void atomo_encrypt(uint8_t* buff) {
    uint8_t tmpB = (~buff[0] + 1) & 0x7F;

    uint8_t bitCnt = 8;
    while(bitCnt < 59) {
        if((tmpB & 0x18) && (((tmpB / 8) & 3) != 3)) {
            tmpB = ((tmpB << 1) & 0xFF) | 1;
        } else {
            tmpB = (tmpB << 1) & 0xFF;
        }

        if(tmpB & 0x80) {
            buff[bitCnt / 8] ^= (0x80 >> (bitCnt & 7));
        }

        bitCnt++;
    }

    buff[0] = (buff[0] ^ 5) & 0x7F;
}

void atomo_decrypt(uint8_t* buff) {
    buff[0] = (buff[0] ^ 5) & 0x7F;
    uint8_t tmpB = (-buff[0]) & 0x7F;

    uint8_t bitCnt = 8;
    while(bitCnt < 59) {
        if((tmpB & 0x18) && (((tmpB / 8) & 3) != 3)) {
            tmpB = ((tmpB << 1) & 0xFF) | 1;
        } else {
            tmpB = (tmpB << 1) & 0xFF;
        }

        if(tmpB & 0x80) {
            buff[bitCnt / 8] ^= (0x80 >> (bitCnt & 7));
        }

        bitCnt++;
    }
}

SubGhzProtocolStatus
    subghz_protocol_decoder_came_atomo_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderCameAtomo* instance = context;

    SubGhzProtocolStatus status = SubGhzProtocolStatusOk;
    status = subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(status != SubGhzProtocolStatusOk) {
        FURI_LOG_E(TAG, "Deserialize error");
        return status;
    }
    if(instance->generic.data_count_bit !=
       subghz_protocol_came_atomo_const.min_count_bit_for_found) {
        FURI_LOG_E(TAG, "Wrong number of bits in key");
        return SubGhzProtocolStatusErrorValueBitCount;
    }

    if(!flipper_format_rewind(flipper_format)) {
        FURI_LOG_E(TAG, "Rewind error");
        return SubGhzProtocolStatusError;
    }

    uint32_t tmp_counter_mode;
    if(flipper_format_read_uint32(flipper_format, "CounterMode", &tmp_counter_mode, 1)) {
        came_atomo_counter_mode = (uint8_t)tmp_counter_mode;
    } else {
        came_atomo_counter_mode = 0;
    }

    return status;
}

void subghz_protocol_decoder_came_atomo_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderCameAtomo* instance = context;
    // Detect/decode split (FoxFW2.0 v2.1): Atomo decrypt (rainbow table) stays
    // internal; the SD plugin only formats.
    subghz_protocol_came_atomo_remote_controller(&instance->generic);
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}

