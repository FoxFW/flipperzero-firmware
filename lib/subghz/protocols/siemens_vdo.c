#include "siemens_vdo.h"
#include "pp_auto_common.h"
#include <lib/toolbox/manchester_decoder.h>
#include "common.h"
#include "../encode_plugin.h"

#define TAG "SiemensVDO"

#define SIEMENS_VDO_SYNC     0x6001U
#define SIEMENS_VDO_SYNC_INV 0x9FFEU
#define SIEMENS_VDO_PAYLOAD_BITS  96U
#define SIEMENS_VDO_PAYLOAD_BYTES 12U

static const SubGhzBlockConst subghz_protocol_siemens_vdo_const = {
    .te_short = 220,
    .te_long = 440,
    .te_delta = 100,
    .min_count_bit_for_found = SIEMENS_VDO_PAYLOAD_BITS,
};

struct SubGhzProtocolDecoderSiemensVdo {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint16_t sync_shift;
    uint16_t sync_bits;
    bool synced;
    bool invert;
    uint16_t data_bit_count;
    uint8_t bytes[SIEMENS_VDO_PAYLOAD_BYTES];
    uint8_t crc;
    bool crc_check;
};

struct SubGhzProtocolEncoderSiemensVdo {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

const SubGhzProtocolDecoder subghz_protocol_siemens_vdo_decoder = {
    .alloc = subghz_protocol_decoder_siemens_vdo_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_siemens_vdo_feed,
    .reset = subghz_protocol_decoder_siemens_vdo_reset,

    .get_hash_data = subghz_protocol_decoder_siemens_vdo_get_hash_data,
    .serialize = subghz_protocol_decoder_siemens_vdo_serialize,
    .deserialize = subghz_protocol_decoder_siemens_vdo_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_siemens_vdo_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

static const SubGhzEncoderExternal subghz_protocol_siemens_vdo_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_recon_manchester");

const SubGhzProtocol subghz_protocol_siemens_vdo = {
    .name = SIEMENS_VDO_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_siemens_vdo_decoder,
    .encoder = &subghz_protocol_siemens_vdo_encoder_ext.vtable,
};

static void siemens_vdo_check_remote_controller(SubGhzProtocolDecoderSiemensVdo* instance) {
    uint64_t data = instance->generic.data;
    instance->generic.btn = (uint8_t)((data >> 56) & 0xFF);
    instance->generic.cnt = (uint32_t)((data >> 40) & 0xFFFF);
    instance->crc = (uint8_t)(data & 0xFF);
}

static void siemens_vdo_process_frame(SubGhzProtocolDecoderSiemensVdo* instance) {
    uint8_t b[SIEMENS_VDO_PAYLOAD_BYTES];
    for(uint8_t i = 0; i < SIEMENS_VDO_PAYLOAD_BYTES; i++) {
        b[i] = instance->invert ? (uint8_t)(~instance->bytes[i]) : instance->bytes[i];
    }

    uint16_t sum = 0;
    for(uint8_t i = 0; i < SIEMENS_VDO_PAYLOAD_BYTES; i++) {
        sum += b[i];
    }
    if(sum == 0 || sum == (uint16_t)(0xFF * SIEMENS_VDO_PAYLOAD_BYTES)) {
        return;
    }

    uint8_t x = 0;
    for(uint8_t i = 0; i < SIEMENS_VDO_PAYLOAD_BYTES; i++) {
        x ^= b[i];
    }
    if(x != 0) {
        return;
    }

    uint32_t id = ((uint32_t)b[3] << 24) | ((uint32_t)b[2] << 16) | ((uint32_t)b[1] << 8) |
                  (uint32_t)b[0];

    uint64_t data = 0;
    for(uint8_t i = 4; i < SIEMENS_VDO_PAYLOAD_BYTES; i++) {
        data = (data << 8) | (uint64_t)b[i];
    }

    instance->generic.serial = id;
    instance->generic.data = data;
    instance->generic.data_count_bit = SIEMENS_VDO_PAYLOAD_BITS;
    siemens_vdo_check_remote_controller(instance);
    instance->crc_check = true;

    instance->decoder.decode_data = data;
    instance->decoder.decode_count_bit = SIEMENS_VDO_PAYLOAD_BITS;

    FURI_LOG_D(
        TAG,
        "Frame ok: Id:%08lX Btn:%02X Seq:%04lX",
        (unsigned long)id,
        instance->generic.btn,
        (unsigned long)instance->generic.cnt);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void* subghz_protocol_decoder_siemens_vdo_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderSiemensVdo* instance =
        calloc(1, sizeof(SubGhzProtocolDecoderSiemensVdo));
    instance->base.protocol = &subghz_protocol_siemens_vdo;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_siemens_vdo_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderSiemensVdo* instance = context;
    instance->decoder.parser_step = 0;
    instance->sync_shift = 0;
    instance->sync_bits = 0;
    instance->synced = false;
    instance->invert = false;
    instance->data_bit_count = 0;
    manchester_advance(
        instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);
}

void subghz_protocol_decoder_siemens_vdo_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderSiemensVdo* instance = context;

    ManchesterEvent event =
        ppa_manchester_event(duration, level, &subghz_protocol_siemens_vdo_const);

    if(event == ManchesterEventReset) {

        instance->sync_shift = 0;
        instance->sync_bits = 0;
        instance->synced = false;
        instance->invert = false;
        instance->data_bit_count = 0;
        manchester_advance(
            instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);
        return;
    }

    bool data = false;
    if(!manchester_advance(
           instance->manchester_state, event, &instance->manchester_state, &data)) {
        return;
    }

    if(!instance->synced) {
        instance->sync_shift = (uint16_t)((instance->sync_shift << 1) | (data ? 1U : 0U));
        if(instance->sync_bits < 16) instance->sync_bits++;

        if(instance->sync_bits >= 16) {
            if(instance->sync_shift == SIEMENS_VDO_SYNC) {
                instance->synced = true;
                instance->invert = false;
            } else if(instance->sync_shift == SIEMENS_VDO_SYNC_INV) {
                instance->synced = true;
                instance->invert = true;
            }
            if(instance->synced) {
                instance->data_bit_count = 0;
                for(uint8_t i = 0; i < SIEMENS_VDO_PAYLOAD_BYTES; i++) instance->bytes[i] = 0;
            }
        }
        return;
    }

    uint16_t idx = instance->data_bit_count >> 3;
    if(idx < SIEMENS_VDO_PAYLOAD_BYTES && data) {
        instance->bytes[idx] |= (uint8_t)(1U << (7 - (instance->data_bit_count & 7)));
    }
    instance->data_bit_count++;

    if(instance->data_bit_count >= SIEMENS_VDO_PAYLOAD_BITS) {
        siemens_vdo_process_frame(instance);

        instance->synced = false;
        instance->invert = false;
        instance->sync_shift = 0;
        instance->sync_bits = 0;
        instance->data_bit_count = 0;
    }
}

uint8_t subghz_protocol_decoder_siemens_vdo_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderSiemensVdo* instance = context;

    return subghz_protocol_blocks_get_hash_data(&instance->decoder, sizeof(uint64_t));
}

SubGhzProtocolStatus subghz_protocol_decoder_siemens_vdo_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderSiemensVdo* instance = context;

    siemens_vdo_check_remote_controller(instance);

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    SubGhzProtocolStatus fret = ppa_serialize_fields(
        flipper_format,
        PP_FIELD_SERIAL | PP_FIELD_BTN | PP_FIELD_CNT,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt,
        0);
    if(fret != SubGhzProtocolStatusOk) return fret;

    uint8_t fdata[12];
    uint32_t id = instance->generic.serial;
    fdata[0] = (uint8_t)(id);
    fdata[1] = (uint8_t)(id >> 8);
    fdata[2] = (uint8_t)(id >> 16);
    fdata[3] = (uint8_t)(id >> 24);
    uint64_t data = instance->generic.data;
    for(uint8_t i = 4; i < 12; i++) fdata[i] = (uint8_t)(data >> (56 - (i - 4) * 8));
    if(!flipper_format_write_hex(flipper_format, "Data", fdata, sizeof(fdata))) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_siemens_vdo_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderSiemensVdo* instance = context;
    flipper_format_rewind(flipper_format);
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_siemens_vdo_const.min_count_bit_for_found);
}

