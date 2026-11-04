#include "nice_flor_s.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#include "../blocks/custom_btn_i.h"

#include "nice_flor_s_o.h"

#define TAG "SubGhzProtocolNiceFlorS"

static bool nice_flor_s_raw_replay = false; // #16: TxRaw verbatim replay (freeze counter)

#define NICE_ONE_NAME                               "Nice One"
#define SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES 32
#define SUBGHZ_NO_NICE_FLOR_S_RAINBOW_TABLE         0

static bool bypass = false;

static const SubGhzBlockConst subghz_protocol_nice_flor_s_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 300,
    .min_count_bit_for_found = 52,
};

struct SubGhzProtocolDecoderNiceFlorS {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    const char* nice_flor_s_rainbow_table_file_name;

    uint16_t ic;
    uint8_t held_count_bit;
    bool o_code : 1;
    bool o_code_no_ic : 1;
    bool skip_o_code : 1;
    bool held_repeat : 1;
    bool has_held : 1;
    uint8_t fresh_run : 2;
    uint8_t cert_cnt;
    uint32_t cert;
    uint32_t held_lo;
    uint32_t held_hi;
    uint32_t held_trail;
};

struct SubGhzProtocolEncoderNiceFlorS {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    const char* nice_flor_s_rainbow_table_file_name;

    uint16_t ic;
    uint32_t cert;
    bool o_code;
    bool cert_on;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderNiceFlorS);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderNiceFlorS);

typedef enum {
    NiceFlorSDecoderStepReset = 0,
    NiceFlorSDecoderStepCheckHeader,
    NiceFlorSDecoderStepFoundHeader,
    NiceFlorSDecoderStepSaveDuration,
    NiceFlorSDecoderStepCheckDuration,
} NiceFlorSDecoderStep;

static uint8_t nice_flors_counter_mode = 0;

const SubGhzProtocolDecoder subghz_protocol_nice_flor_s_decoder = {
    .alloc = subghz_protocol_decoder_nice_flor_s_alloc,
    .free = subghz_protocol_decoder_nice_flor_s_free,

    .feed = subghz_protocol_decoder_nice_flor_s_feed,
    .reset = subghz_protocol_decoder_common_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_nice_flor_s_serialize,
    .deserialize = subghz_protocol_decoder_nice_flor_s_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

/* host bridge: firmware functions/state the SD encoder uses (handed over at load) */
typedef struct {
    void* bypass;
    void* nice_flor_s_raw_replay;
    void* nice_flors_counter_mode;
} enc_nice_flor_s_host_t;
static const enc_nice_flor_s_host_t enc_nice_flor_s_host = {
    .bypass = (void*)&bypass,
    .nice_flor_s_raw_replay = (void*)&nice_flor_s_raw_replay,
    .nice_flors_counter_mode = (void*)&nice_flors_counter_mode,
};
static const SubGhzEncoderExternal subghz_protocol_nice_flor_s_encoder_ext = SUBGHZ_ENCODER_EXTERNAL_HOST("enc_nice_flor_s", &enc_nice_flor_s_host);

const SubGhzProtocol subghz_protocol_nice_flor_s = {
    .name = SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send | SubGhzProtocolFlag_RawReplay | SubGhzProtocolFlag_NiceFlorS,

    .decoder = &subghz_protocol_nice_flor_s_decoder,
    .encoder = &subghz_protocol_nice_flor_s_encoder_ext.vtable,
};


uint64_t subghz_protocol_nice_flor_s_encrypt(uint64_t data, const char* file_name) {
    if(!file_name) return SUBGHZ_NO_NICE_FLOR_S_RAINBOW_TABLE;
    uint8_t buffer[SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES] = {0};
    if(!subghz_keystore_raw_get_data(
           file_name, 0, buffer, SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES)) {
        return SUBGHZ_NO_NICE_FLOR_S_RAINBOW_TABLE;
    }
    return nice_o_encrypt_buf(data, NICE_FLOR_S_IC_PLAIN, buffer);
}

bool subghz_protocol_nice_flor_s_create_data_ic(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    SubGhzRadioPreset* preset,
    bool nice_one,
    bool o_code,
    uint16_t ic) {
    furi_assert(context);
    SubGhzProtocolEncoderNiceFlorS* instance = context;
    const char* file_name = instance->nice_flor_s_rainbow_table_file_name;
    uint8_t buf[SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES] = {0};
    const bool have_tbl =
        file_name &&
        subghz_keystore_raw_get_data(file_name, 0, buf, SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES);
    instance->generic.serial = serial;
    instance->generic.cnt = cnt;
    instance->generic.data_count_bit = nice_one ? NICE_ONE_COUNT_BIT : 52;
    instance->o_code = o_code;
    instance->ic = o_code ? ic : NICE_FLOR_S_IC_PLAIN;

    if(o_code) {
        if(!have_tbl) return false;
        instance->generic.data =
            nice_o_make_key(serial, cnt, btn, NICE_FLOR_S_CREATE_PARCEL, ic, buf);
        if(instance->generic.data == 0) return false;
    } else {
        const uint64_t decrypt = ((uint64_t)serial << 16) | cnt;
        const uint64_t enc_part = have_tbl ?
                                      nice_o_encrypt_buf(decrypt, NICE_FLOR_S_IC_PLAIN, buf) :
                                      (uint64_t)SUBGHZ_NO_NICE_FLOR_S_RAINBOW_TABLE;
        const uint8_t byte = btn << 4 | (0xF ^ btn ^ NICE_FLOR_S_CREATE_PARCEL);
        instance->generic.data = (uint64_t)byte << 44 | enc_part;
    }

    if(instance->generic.data_count_bit == NICE_ONE_COUNT_BIT) {
        uint8_t add_data[10] = {0};
        for(size_t i = 0; i < 7; i++) {
            add_data[i] = (instance->generic.data >> (48 - i * 8)) & 0xFF;
        }
        /* non-O-Code keeps Fox's original parcel-0 Nice One tail (byte-identical to prior
         * generation); O-Code uses parcel 3 + the O-Code k-table. */
        nice_o_one_get_data(
            add_data, o_code ? NICE_FLOR_S_CREATE_PARCEL : 0, o_code ? 0x4 : 0x0);
        instance->generic.data_2 = 0;
        for(size_t j = 7; j < 10; j++) {
            instance->generic.data_2 <<= 8;
            instance->generic.data_2 += add_data[j];
        }
    }

    SubGhzProtocolStatus res =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if((res == SubGhzProtocolStatusOk) && o_code) {
        uint8_t ic_data[2] = {(uint8_t)(ic >> 8), (uint8_t)ic};
        if(!flipper_format_rewind(flipper_format) ||
           !flipper_format_insert_or_update_hex(flipper_format, "IC", ic_data, sizeof(ic_data))) {
            FURI_LOG_E(TAG, "Unable to add IC");
            res = SubGhzProtocolStatusErrorParserOthers;
        }
    }
    return res == SubGhzProtocolStatusOk;
}

bool subghz_protocol_nice_flor_s_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    SubGhzRadioPreset* preset,
    bool nice_one) {
    return subghz_protocol_nice_flor_s_create_data_ic(
        context, flipper_format, serial, btn, cnt, preset, nice_one, false, NICE_FLOR_S_IC_PLAIN);
}


void* subghz_protocol_decoder_nice_flor_s_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolDecoderNiceFlorS* instance = malloc(sizeof(SubGhzProtocolDecoderNiceFlorS));
    instance->base.protocol = &subghz_protocol_nice_flor_s;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->nice_flor_s_rainbow_table_file_name =
        subghz_environment_get_nice_flor_s_rainbow_table_file_name(environment);
    if(instance->nice_flor_s_rainbow_table_file_name) {
        FURI_LOG_D(
            TAG, "Loading rainbow table from %s", instance->nice_flor_s_rainbow_table_file_name);
    }
    instance->ic = NICE_FLOR_S_IC_PLAIN;
    instance->o_code = false;
    instance->o_code_no_ic = false;
    instance->skip_o_code = false;
    instance->has_held = false;
    instance->held_repeat = false;
    instance->fresh_run = 0;
    instance->held_count_bit = 0;
    instance->held_lo = 0;
    instance->held_hi = 0;
    instance->held_trail = 0;
    instance->cert = 0;
    instance->cert_cnt = 0;
    return instance;
}

void subghz_protocol_decoder_nice_flor_s_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderNiceFlorS* instance = context;
    instance->nice_flor_s_rainbow_table_file_name = NULL;
    free(instance);
}

void subghz_protocol_decoder_nice_flor_s_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderNiceFlorS* instance = context;

    switch(instance->decoder.parser_step) {
    case NiceFlorSDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_nice_flor_s_const.te_short * 38) <
                        subghz_protocol_nice_flor_s_const.te_delta * 38)) {

            instance->decoder.parser_step = NiceFlorSDecoderStepCheckHeader;
        }
        break;
    case NiceFlorSDecoderStepCheckHeader:
        if((level) && (DURATION_DIFF(duration, subghz_protocol_nice_flor_s_const.te_short * 3) <
                       subghz_protocol_nice_flor_s_const.te_delta * 3)) {

            instance->decoder.parser_step = NiceFlorSDecoderStepFoundHeader;
        } else {
            instance->decoder.parser_step = NiceFlorSDecoderStepReset;
        }
        break;
    case NiceFlorSDecoderStepFoundHeader:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_nice_flor_s_const.te_short * 3) <
                        subghz_protocol_nice_flor_s_const.te_delta * 3)) {

            instance->decoder.parser_step = NiceFlorSDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        } else {
            instance->decoder.parser_step = NiceFlorSDecoderStepReset;
        }
        break;
    case NiceFlorSDecoderStepSaveDuration:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_nice_flor_s_const.te_short * 3) <
               subghz_protocol_nice_flor_s_const.te_delta) {

                instance->decoder.parser_step = NiceFlorSDecoderStepReset;
                if((instance->decoder.decode_count_bit ==
                    subghz_protocol_nice_flor_s_const.min_count_bit_for_found) ||
                   (instance->decoder.decode_count_bit == NICE_ONE_COUNT_BIT) ||
                   (instance->decoder.decode_count_bit == NICE_O_LONG_COUNT_BIT)) {
                    instance->generic.data = instance->generic.data_2;
                    instance->generic.data_2 = instance->decoder.decode_data;
                    instance->decoder.decode_data = instance->generic.data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    const bool long_o =
                        (instance->generic.data_count_bit == NICE_O_LONG_COUNT_BIT);
                    if(long_o) {
                        instance->fresh_run = NICE_O_FRESH_REPORTS;
                        instance->o_code = true;
                        instance->o_code_no_ic = (instance->ic == NICE_FLOR_S_IC_PLAIN);
                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                        break;
                    }

                    const uint32_t body = (uint32_t)instance->generic.data;

                    if(instance->has_held && !instance->held_repeat) {
                        const bool fresh = (instance->held_lo != body);
                        const bool is_o = fresh && (instance->fresh_run >= NICE_O_FRESH_REPORTS);

                        if(!fresh) {
                            instance->fresh_run = 0;
                        } else if(instance->fresh_run < NICE_O_FRESH_REPORTS) {
                            instance->fresh_run++;
                        }

                        if(!fresh || is_o) {
                            const uint64_t data = instance->generic.data;
                            const uint64_t data_2 = instance->generic.data_2;
                            const uint16_t count_bit = instance->generic.data_count_bit;
                            instance->generic.data =
                                (uint64_t)instance->held_hi << 32 | instance->held_lo;
                            instance->generic.data_2 = instance->held_trail;
                            instance->generic.data_count_bit = instance->held_count_bit;
                            instance->o_code = is_o;
                            instance->o_code_no_ic =
                                is_o && (instance->ic == NICE_FLOR_S_IC_PLAIN);
                            if(instance->base.callback)
                                instance->base.callback(&instance->base, instance->base.context);
                            instance->generic.data = data;
                            instance->generic.data_2 = data_2;
                            instance->generic.data_count_bit = count_bit;
                        }
                    } else if(instance->has_held) {
                        instance->fresh_run = 0;
                    }
                    instance->held_repeat = instance->has_held && (instance->held_lo == body);
                    instance->held_lo = body;
                    instance->held_hi = (uint32_t)(instance->generic.data >> 32);
                    instance->held_trail = (uint32_t)instance->generic.data_2;
                    instance->held_count_bit = (uint8_t)instance->generic.data_count_bit;
                    instance->has_held = true;
                }
                break;
            } else {

                instance->decoder.te_last = duration;
                instance->decoder.parser_step = NiceFlorSDecoderStepCheckDuration;
            }
        }
        break;
    case NiceFlorSDecoderStepCheckDuration:
        if(!level) {
            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_nice_flor_s_const.te_short) <
                subghz_protocol_nice_flor_s_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_nice_flor_s_const.te_long) <
                subghz_protocol_nice_flor_s_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = NiceFlorSDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_nice_flor_s_const.te_long) <
                 subghz_protocol_nice_flor_s_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_nice_flor_s_const.te_short) <
                 subghz_protocol_nice_flor_s_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = NiceFlorSDecoderStepSaveDuration;
            } else
                instance->decoder.parser_step = NiceFlorSDecoderStepReset;
        } else {
            instance->decoder.parser_step = NiceFlorSDecoderStepReset;
        }
        if(instance->decoder.decode_count_bit ==
           subghz_protocol_nice_flor_s_const.min_count_bit_for_found) {
            instance->generic.data_2 = instance->decoder.decode_data;
            instance->decoder.decode_data = 0;
        }
        break;
    }
}

static uint32_t subghz_protocol_nice_flor_s_cert(SubGhzBlockGeneric* generic) {
    return (uint32_t)((generic->data_2 >> 8) ^ generic->data ^ generic->serial) & 0xFFFFFF;
}

bool subghz_protocol_decoder_nice_flor_s_is_o_code_no_ic(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderNiceFlorS* instance = context;
    return instance->o_code_no_ic;
}

SubGhzProtocolStatus subghz_protocol_decoder_nice_flor_s_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderNiceFlorS* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if((ret == SubGhzProtocolStatusOk) && instance->o_code) {
        uint8_t ic_data[2] = {(uint8_t)(instance->ic >> 8), (uint8_t)instance->ic};
        if(!flipper_format_rewind(flipper_format) ||
           !flipper_format_insert_or_update_hex(flipper_format, "IC", ic_data, sizeof(ic_data))) {
            FURI_LOG_E(TAG, "Unable to add IC");
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
    }
    if(instance->generic.data_count_bit == NICE_ONE_COUNT_BIT) {
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
        if((ret == SubGhzProtocolStatusOk) &&
           !flipper_format_insert_or_update_uint32(
               flipper_format, "Data", (uint32_t*)&instance->generic.data_2, 1)) {
            FURI_LOG_E(TAG, "Unable to add Data");
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
    } else if(instance->generic.data_count_bit == NICE_O_LONG_COUNT_BIT) {
        uint8_t d2[sizeof(uint64_t)] = {0};
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            d2[sizeof(uint64_t) - i - 1] = (instance->generic.data_2 >> i * 8) & 0xFF;
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
        if((ret == SubGhzProtocolStatusOk) &&
           !flipper_format_insert_or_update_hex(flipper_format, "Data2", d2, sizeof(d2))) {
            FURI_LOG_E(TAG, "Unable to add Data2");
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
        if((ret == SubGhzProtocolStatusOk) && !instance->o_code_no_ic) {
            /* O-Code cert needs the decoded serial; decrypt inline (the shared-header crypto
             * is available in the detector, so no .fal round-trip is required). */
            const char* file_name = instance->nice_flor_s_rainbow_table_file_name;
            uint8_t buf[SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES] = {0};
            if(file_name &&
               subghz_keystore_raw_get_data(
                   file_name, 0, buf, SUBGHZ_NICE_FLOR_S_RAINBOW_TABLE_SIZE_BYTES)) {
                uint32_t serial = 0;
                nice_o_decode_frame(
                    instance->generic.data, instance->ic, buf, &serial, NULL, NULL);
                instance->generic.serial = serial;
            }
            const uint32_t cert = subghz_protocol_nice_flor_s_cert(&instance->generic);
            uint8_t cert_data[3] = {(uint8_t)(cert >> 16), (uint8_t)(cert >> 8), (uint8_t)cert};
            if(!flipper_format_rewind(flipper_format) ||
               !flipper_format_insert_or_update_hex(
                   flipper_format, "Cert", cert_data, sizeof(cert_data))) {
                FURI_LOG_E(TAG, "Unable to add Cert");
                ret = SubGhzProtocolStatusErrorParserOthers;
            }
        }
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_nice_flor_s_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderNiceFlorS* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if((instance->generic.data_count_bit !=
            subghz_protocol_nice_flor_s_const.min_count_bit_for_found) &&
           (instance->generic.data_count_bit != NICE_ONE_COUNT_BIT) &&
           (instance->generic.data_count_bit != NICE_O_LONG_COUNT_BIT)) {
            FURI_LOG_E(TAG, "Wrong number of bits in key");
            ret = SubGhzProtocolStatusErrorValueBitCount;
            break;
        }
        if(instance->generic.data_count_bit == NICE_ONE_COUNT_BIT) {
            if(!flipper_format_rewind(flipper_format)) {
                FURI_LOG_E(TAG, "Rewind error");
                ret = SubGhzProtocolStatusErrorParserOthers;
                break;
            }
            uint32_t temp = 0;
            if(!flipper_format_read_uint32(flipper_format, "Data", (uint32_t*)&temp, 1)) {
                FURI_LOG_E(TAG, "Missing Data");
                ret = SubGhzProtocolStatusErrorParserOthers;
                break;
            }
            instance->generic.data_2 = (uint64_t)temp;
        } else if(instance->generic.data_count_bit == NICE_O_LONG_COUNT_BIT) {
            if(!flipper_format_rewind(flipper_format)) {
                FURI_LOG_E(TAG, "Rewind error");
                ret = SubGhzProtocolStatusErrorParserOthers;
                break;
            }
            uint8_t d2[sizeof(uint64_t)] = {0};
            if(!flipper_format_read_hex(flipper_format, "Data2", d2, sizeof(d2))) {
                FURI_LOG_E(TAG, "Missing Data2");
                ret = SubGhzProtocolStatusErrorParserOthers;
                break;
            }
            instance->generic.data_2 = 0;
            for(size_t i = 0; i < sizeof(uint64_t); i++) {
                instance->generic.data_2 = instance->generic.data_2 << 8 | d2[i];
            }
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }

        uint32_t tmp_counter_mode;
        if(flipper_format_read_uint32(flipper_format, "CounterMode", &tmp_counter_mode, 1)) {
            nice_flors_counter_mode = (uint8_t)tmp_counter_mode;
        } else {
            nice_flors_counter_mode = 0;
        }

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        uint8_t ic_data[2] = {0xFF, 0xFF};
        if(flipper_format_read_hex(flipper_format, "IC", ic_data, sizeof(ic_data))) {
            instance->ic = (uint16_t)ic_data[0] << 8 | ic_data[1];
            instance->o_code = true;
        } else {
            instance->ic = NICE_FLOR_S_IC_PLAIN;
            instance->o_code = false;
        }
        instance->cert = 0;
        instance->cert_cnt = 0;
        if(instance->o_code && flipper_format_rewind(flipper_format)) {
            uint8_t cert_data[3] = {0};
            if(flipper_format_read_hex(flipper_format, "Cert", cert_data, sizeof(cert_data))) {
                instance->cert = (uint32_t)cert_data[0] << 16 | (uint32_t)cert_data[1] << 8 |
                                 cert_data[2];
            }
            uint32_t used = 0;
            if(flipper_format_rewind(flipper_format) &&
               flipper_format_read_uint32(flipper_format, "Cert_Cnt", &used, 1)) {
                instance->cert_cnt = (used > 0xFF) ? 0xFF : (uint8_t)used;
            }
        }

        instance->o_code_no_ic = false;
        instance->has_held = false;
        instance->held_repeat = false;
        instance->fresh_run = 0;
    } while(false);
    return ret;
}

