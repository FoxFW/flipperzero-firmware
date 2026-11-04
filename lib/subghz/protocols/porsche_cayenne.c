#include "porsche_cayenne.h"
#include "../blocks/custom_btn_i.h"
#include "common.h"
#include "../encode_plugin.h"
#include "decode_plugin.h"

#define TAG "PorscheCayenneProtocol"

static const SubGhzBlockConst subghz_protocol_porsche_cayenne_const = {
    .te_short = 1680,
    .te_long  = 3370,
    .te_delta =  500,
    .min_count_bit_for_found = 64,
};

#define PC_TE_SYNC   3370u
#define PC_TE_GAP    5930u
#define PC_SYNC_MIN    15
#define PC_SYNC_COUNT  73
#define PC_UPLOAD_SIZE 1300

static void porsche_cayenne_compute_frame(
    uint32_t serial24,
    uint8_t btn,
    uint16_t counter,
    uint8_t frame_type,
    uint8_t* pkt) {

    uint8_t b0 = (uint8_t)((btn << 4) | (frame_type & 0x07));
    uint8_t b1 = (serial24 >> 16) & 0xFF;
    uint8_t b2 = (serial24 >> 8) & 0xFF;
    uint8_t b3 = serial24 & 0xFF;

    uint16_t cnt = counter + 1;
    uint8_t cnt_lo = cnt & 0xFF;
    uint8_t cnt_hi = (cnt >> 8) & 0xFF;

    uint8_t r_h = b3;
    uint8_t r_m = b1;
    uint8_t r_l = b2;

#define ROTATE24(rh, rm, rl) do { \
        uint8_t _ch = (uint8_t)(((rh) >> 7) & 1u); \
        uint8_t _cm = (uint8_t)(((rm) >> 7) & 1u); \
        uint8_t _cl = (uint8_t)(((rl) >> 7) & 1u); \
        (rh) = (uint8_t)(((rh) << 1) | _cm); \
        (rm) = (uint8_t)(((rm) << 1) | _cl); \
        (rl) = (uint8_t)(((rl) << 1) | _ch); \
    } while(0)

    for(int i = 0; i < 4; i++) ROTATE24(r_h, r_m, r_l);

    for(int j = 0; j < cnt_lo; j++) ROTATE24(r_h, r_m, r_l);

#undef ROTATE24

    uint8_t a9A = r_h ^ b0;

    uint8_t nb9B_p1 = (uint8_t)((~cnt_lo << 2) & 0xFC) ^ r_m; nb9B_p1 &= 0xCC;
    uint8_t nb9B_p2 = (uint8_t)((~cnt_hi << 2) & 0xFC) ^ r_m; nb9B_p2 &= 0x30;
    uint8_t nb9B_p3 = (uint8_t)((~cnt_hi >> 6) & 0x03) ^ r_m; nb9B_p3 &= 0x03;
    uint8_t a9B = nb9B_p1 | nb9B_p2 | nb9B_p3;

    uint8_t nb9C_p1 = (uint8_t)((~cnt_lo >> 2) & 0x3F) ^ r_l; nb9C_p1 &= 0x33;
    uint8_t nb9C_p2 = (uint8_t)((~cnt_hi & 0x03) << 6) ^ r_l; nb9C_p2 &= 0xC0;
    uint8_t nb9C_p3 = (uint8_t)((~cnt_hi >> 2) & 0x3F) ^ r_l; nb9C_p3 &= 0x0C;
    uint8_t a9C = nb9C_p1 | nb9C_p2 | nb9C_p3;

    pkt[0] = b0;
    pkt[1] = b1;
    pkt[2] = b2;
    pkt[3] = b3;

    pkt[4] = (uint8_t)(((a9A >> 2) & 0x3F) | ((~cnt_lo & 0x03u) << 6));

    pkt[5] = (uint8_t)(
        (~cnt_lo & 0xC0u) |
        ((a9A & 0x03u) << 4) |
        (a9B & 0x0Cu) |
        ((~cnt_lo >> 2) & 0x03u));

    pkt[6] = (uint8_t)(
        ((a9B & 0x03u) << 6) |
        ((a9C >> 2) & 0x3Cu) |
        ((~cnt_lo >> 4) & 0x03u));

    pkt[7] = (uint8_t)(((a9B >> 4) & 0x0Fu) | ((a9C & 0x0Fu) << 4));
}

typedef enum {
    PCDecoderStepReset = 0,
    PCDecoderStepSync,
    PCDecoderStepGapHigh,
    PCDecoderStepGapLow,
    PCDecoderStepData,
} PCDecoderStep;

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder        decoder;
    SubGhzBlockGeneric        generic;

    uint16_t sync_count;
    uint64_t raw_data;
    uint8_t  bit_count;
} SubGhzProtocolDecoderPorscheCayenne;

typedef struct {
    SubGhzProtocolEncoderBase  base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric         generic;
} SubGhzProtocolEncoderPorscheCayenne;

const SubGhzProtocolDecoder subghz_protocol_porsche_cayenne_decoder;

void* subghz_protocol_decoder_porsche_cayenne_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderPorscheCayenne* instance =
        malloc(sizeof(SubGhzProtocolDecoderPorscheCayenne));
    instance->base.protocol    = &subghz_protocol_porsche_cayenne;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}



void subghz_protocol_decoder_porsche_cayenne_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderPorscheCayenne* instance = context;
    instance->decoder.parser_step = PCDecoderStepReset;
    instance->decoder.te_last     = 0;
    instance->sync_count          = 0;
    instance->raw_data            = 0;
    instance->bit_count           = 0;
}

void subghz_protocol_decoder_porsche_cayenne_feed(
    void* context,
    bool level,
    uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderPorscheCayenne* instance = context;

    const uint32_t te_s  = subghz_protocol_porsche_cayenne_const.te_short;
    const uint32_t te_l  = subghz_protocol_porsche_cayenne_const.te_long;
    const uint32_t te_d  = subghz_protocol_porsche_cayenne_const.te_delta;

    switch(instance->decoder.parser_step) {

    case PCDecoderStepReset:

        if(!level && DURATION_DIFF(duration, PC_TE_SYNC) < te_d) {
            instance->sync_count          = 1;
            instance->decoder.parser_step = PCDecoderStepSync;
        }
        break;

    case PCDecoderStepSync:
        if(level) {

            if(DURATION_DIFF(duration, PC_TE_SYNC) < te_d) {

            } else if(instance->sync_count >= PC_SYNC_MIN &&
                      DURATION_DIFF(duration, PC_TE_GAP) < te_d) {

                instance->decoder.parser_step = PCDecoderStepGapLow;
            } else {
                instance->decoder.parser_step = PCDecoderStepReset;
            }
        } else {

            if(DURATION_DIFF(duration, PC_TE_SYNC) < te_d) {
                instance->sync_count++;
            } else if(instance->sync_count >= PC_SYNC_MIN &&
                      DURATION_DIFF(duration, PC_TE_GAP) < te_d) {

                instance->decoder.parser_step = PCDecoderStepGapHigh;
            } else {
                instance->decoder.parser_step = PCDecoderStepReset;
            }
        }
        break;

    case PCDecoderStepGapHigh:

        if(level && DURATION_DIFF(duration, PC_TE_GAP) < te_d) {
            instance->raw_data            = 0;
            instance->bit_count           = 0;
            instance->decoder.parser_step = PCDecoderStepData;
        } else {
            instance->decoder.parser_step = PCDecoderStepReset;
        }
        break;

    case PCDecoderStepGapLow:

        if(!level && DURATION_DIFF(duration, PC_TE_GAP) < te_d) {
            instance->raw_data            = 0;
            instance->bit_count           = 0;
            instance->decoder.parser_step = PCDecoderStepData;
        } else {
            instance->decoder.parser_step = PCDecoderStepReset;
        }
        break;

    case PCDecoderStepData:

        if(level) {
            bool bit;
            if(DURATION_DIFF(instance->decoder.te_last, te_s) < te_d &&
               DURATION_DIFF(duration, te_l) < te_d) {
                bit = false;
            } else if(
                DURATION_DIFF(instance->decoder.te_last, te_l) < te_d &&
                DURATION_DIFF(duration, te_s) < te_d) {
                bit = true;
            } else {
                instance->decoder.parser_step = PCDecoderStepReset;
                break;
            }
            instance->raw_data = (instance->raw_data << 1) | (bit ? 1u : 0u);
            instance->bit_count++;

            if(instance->bit_count == 64) {

                uint8_t pkt[8];
                uint64_t raw = instance->raw_data;
                for(int i = 7; i >= 0; i--) {
                    pkt[i] = raw & 0xFF;
                    raw >>= 8;
                }

                instance->generic.data           = instance->raw_data;
                instance->generic.data_count_bit = 64;
                instance->generic.serial         = ((uint32_t)pkt[1] << 16) |
                                                   ((uint32_t)pkt[2] << 8)  |
                                                   pkt[3];
                instance->generic.btn            = pkt[0] >> 4;

                instance->generic.cnt = 0;
                uint8_t try_pkt[8];
                for(uint16_t try_cnt = 1; try_cnt <= 256; try_cnt++) {
                    uint8_t ft = pkt[0] & 0x07;

                    porsche_cayenne_compute_frame(
                        instance->generic.serial,
                        instance->generic.btn,
                        (uint16_t)(try_cnt - 1),
                        ft,
                        try_pkt);
                    if(try_pkt[4] == pkt[4] && try_pkt[5] == pkt[5] &&
                       try_pkt[6] == pkt[6] && try_pkt[7] == pkt[7]) {
                        instance->generic.cnt = try_cnt;
                        break;
                    }
                }

                if(instance->base.callback) {
                    instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.parser_step = PCDecoderStepReset;
            }
        } else {

            instance->decoder.te_last = duration;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_porsche_cayenne_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderPorscheCayenne* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder,
        (instance->generic.data_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_porsche_cayenne_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderPorscheCayenne* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        uint32_t temp = instance->generic.serial & 0xFFFFFF;
        flipper_format_write_uint32(flipper_format, "Serial", &temp, 1);

        temp = instance->generic.btn;
        flipper_format_write_uint32(flipper_format, "Btn", &temp, 1);

        temp = instance->generic.cnt;
        flipper_format_write_uint32(flipper_format, "Counter", &temp, 1);
    }

    return ret;
}

SubGhzProtocolStatus subghz_protocol_decoder_porsche_cayenne_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderPorscheCayenne* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_porsche_cayenne_const.min_count_bit_for_found);

    if(ret == SubGhzProtocolStatusOk) {
        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_uint32(flipper_format, "Serial", &instance->generic.serial, 1)) {
            FURI_LOG_E(TAG, "Missing Serial field");
            ret = SubGhzProtocolStatusErrorParserKey;
        }
        flipper_format_rewind(flipper_format);
        uint32_t tmp = 0;
        if(!flipper_format_read_uint32(flipper_format, "Btn", &tmp, 1)) {
            FURI_LOG_E(TAG, "Missing Btn field");
            ret = SubGhzProtocolStatusErrorParserKey;
        } else {
            instance->generic.btn = (uint8_t)tmp;
        }
        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_uint32(flipper_format, "Counter", &instance->generic.cnt, 1)) {
            FURI_LOG_E(TAG, "Missing Counter field");
            ret = SubGhzProtocolStatusErrorParserKey;
        }
    }

    return ret;
}


void subghz_protocol_decoder_porsche_cayenne_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderPorscheCayenne* instance = context;
    const SubGhzDecodeInput input = {
        .decoder = context,
        .generic = &instance->generic,
        .extra = NULL,
        .extra_count = 0,
        .aux = NULL,
    };
    subghz_decode_plugin_get_string(instance->generic.protocol_name, &input, output);
}


const SubGhzProtocolDecoder subghz_protocol_porsche_cayenne_decoder = {
    .alloc         = subghz_protocol_decoder_porsche_cayenne_alloc,
    .free          = subghz_protocol_decoder_common_free,
    .feed          = subghz_protocol_decoder_porsche_cayenne_feed,
    .reset         = subghz_protocol_decoder_porsche_cayenne_reset,
    .get_hash_data = subghz_protocol_decoder_porsche_cayenne_get_hash_data,
    .serialize     = subghz_protocol_decoder_porsche_cayenne_serialize,
    .deserialize   = subghz_protocol_decoder_porsche_cayenne_deserialize,
    .get_string    = subghz_protocol_decoder_porsche_cayenne_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_porsche_cayenne_encoder_ext = SUBGHZ_ENCODER_EXTERNAL("enc_porsche_cayenne");

const SubGhzProtocol subghz_protocol_porsche_cayenne = {
    .name = SUBGHZ_PROTOCOL_PORSCHE_CAYENNE_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 |
            SubGhzProtocolFlag_AM  | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_porsche_cayenne_decoder,
    .encoder = &subghz_protocol_porsche_cayenne_encoder_ext.vtable,
};
