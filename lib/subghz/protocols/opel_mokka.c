#include "opel_mokka.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include "common.h"
#include "decode_plugin.h"
#include "../encode_plugin.h"

#define TAG "OpelMokka"

#define OM_TE          100U
#define OM_TE_DELTA    55U
#define OM_RESET_US    900U
#define OM_ROW_BITS    268
#define OM_HALFBITS    700
#define OM_FRAME_BYTES 36

struct SubGhzProtocolDecoderOpelMokka {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t hb[OM_HALFBITS];
    uint16_t hb_count;
    bool collecting;

    uint32_t key_id;
    uint8_t event;
    uint8_t frame[OM_FRAME_BYTES];
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderOpelMokka);

static inline uint8_t om_bit(const uint8_t* b, int i) {
    return (b[i >> 3] >> (7 - (i & 7))) & 1;
}
static inline uint32_t om_extract(const uint8_t* b, int pos, int n) {
    uint32_t v = 0;
    for(int i = 0; i < n; i++) v = (v << 1) | om_bit(b, pos + i);
    return v;
}

static bool om_try(
    const uint8_t* hb,
    int n,
    int offset,
    int polarity,
    uint8_t* out,
    uint32_t* key_id_out,
    uint8_t* event_out) {
    memset(out, 0, OM_FRAME_BYTES);
    int bits = 0;
    for(int i = offset; i + 1 < n && bits < OM_ROW_BITS; i += 2) {
        uint8_t a = hb[i], b = hb[i + 1];
        if(a == b) return false;
        uint8_t bit = (a > b) ? 1u : 0u;
        bit ^= (uint8_t)polarity;
        if(bit) out[bits >> 3] |= (uint8_t)(0x80 >> (bits & 7));
        bits++;
    }
    if(bits != OM_ROW_BITS) return false;

    for(int i = 0; i < 11; i++)
        if(out[i] != 0) return false;

    uint32_t key_id = om_extract(out, 90, 11);
    uint32_t key_id2 = om_extract(out, 90 + 12 * 8 + 1, 11);
    if(key_id == 0 || key_id != key_id2) return false;

    *key_id_out = key_id;
    *event_out = (uint8_t)om_extract(out, 101, 5);
    return true;
}

static void om_process(SubGhzProtocolDecoderOpelMokka* instance) {
    int n = instance->hb_count;
    if(n < OM_ROW_BITS) return;

    for(int off = 0; off < 2; off++) {
        for(int pol = 0; pol < 2; pol++) {
            if(om_try(
                   instance->hb,
                   n,
                   off,
                   pol,
                   instance->frame,
                   &instance->key_id,
                   &instance->event)) {
                instance->generic.data = om_extract(instance->frame, 106, 32);
                instance->generic.data = (instance->generic.data << 32) |
                                         om_extract(instance->frame, 138, 32);
                instance->generic.data_count_bit = 64;
                instance->generic.serial = instance->key_id;
                instance->generic.btn = instance->event;
                instance->generic.cnt = 0;
                instance->decoder.decode_data = instance->generic.data;
                instance->decoder.decode_count_bit = 64;
                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
                return;
            }
        }
    }
}

void* subghz_protocol_decoder_opel_mokka_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderOpelMokka* instance = calloc(1, sizeof(SubGhzProtocolDecoderOpelMokka));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_opel_mokka;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_opel_mokka_free(void* context) {
    furi_check(context);
    free(context);
}

void subghz_protocol_decoder_opel_mokka_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderOpelMokka* instance = context;
    instance->hb_count = 0;
    instance->collecting = false;
}

void subghz_protocol_decoder_opel_mokka_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderOpelMokka* instance = context;
    const uint32_t te = OM_TE;

    if(duration >= OM_RESET_US) {
        if(instance->collecting) om_process(instance);
        instance->hb_count = 0;
        instance->collecting = false;
        return;
    }

    uint32_t halves = (duration + te / 2) / te;
    if(halves == 0) {

        if(instance->collecting) om_process(instance);
        instance->hb_count = 0;
        instance->collecting = false;
        return;
    }
    if(halves > 2) halves = 2;

    if(!instance->collecting) {
        instance->hb_count = 0;
        instance->collecting = true;
    }
    for(uint32_t i = 0; i < halves; i++) {
        if(instance->hb_count < OM_HALFBITS) instance->hb[instance->hb_count++] = level ? 1 : 0;
    }
    if(instance->hb_count >= OM_HALFBITS) {
        om_process(instance);
        instance->hb_count = 0;
        instance->collecting = false;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_opel_mokka_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderOpelMokka* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;
    uint32_t key_id = instance->key_id;
    uint32_t event = instance->event;
    if(!flipper_format_write_uint32(flipper_format, "KeyId", &key_id, 1) ||
       !flipper_format_write_uint32(flipper_format, "Event", &event, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_opel_mokka_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderOpelMokka* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, 64);
    if(status != SubGhzProtocolStatusOk) return status;
    uint32_t key_id = 0, event = 0;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "KeyId", &key_id, 1);
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Event", &event, 1);
    instance->key_id = key_id;
    instance->event = (uint8_t)event;
    instance->generic.serial = key_id;
    instance->generic.btn = (uint8_t)event;
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    return status;
}

void subghz_protocol_decoder_opel_mokka_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderOpelMokka* instance = context;
    const SubGhzDecodeInput input = {
        .generic = &instance->generic,
    };
    subghz_decode_plugin_get_string("Opel-Mokka", &input, output);
}

const SubGhzProtocolDecoder subghz_protocol_opel_mokka_decoder = {
    .alloc = subghz_protocol_decoder_opel_mokka_alloc,
    .free = subghz_protocol_decoder_opel_mokka_free,
    .feed = subghz_protocol_decoder_opel_mokka_feed,
    .reset = subghz_protocol_decoder_opel_mokka_reset,
    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_opel_mokka_serialize,
    .deserialize = subghz_protocol_decoder_opel_mokka_deserialize,
    .get_string = subghz_protocol_decoder_opel_mokka_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_opel_mokka_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_recon_manchester");

const SubGhzProtocol subghz_protocol_opel_mokka = {
    .name = SUBGHZ_PROTOCOL_OPEL_MOKKA_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_opel_mokka_decoder,
    .encoder = &subghz_protocol_opel_mokka_encoder_ext.vtable,
};
