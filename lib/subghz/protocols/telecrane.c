#include "telecrane.h"

#include "common.h"
#include "../encode_plugin.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "Telecrane"

#define TELECRANE_BIT_US        53u
#define TELECRANE_SYNC          0x45BA85BA8ULL
#define TELECRANE_SYNC_MASK     0xFFFFFFFFFULL
#define TELECRANE_COLLECT_BITS  60
#define TELECRANE_MIN_COUNT_BIT 24

struct SubGhzProtocolDecoderTelecrane {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint64_t sync_sr;
    uint64_t collect;
    uint8_t collect_bits;
    bool collecting;

    uint16_t cmd_mask;
    uint8_t channel;
};

struct SubGhzProtocolEncoderTelecrane {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint16_t cmd_mask;
    uint8_t channel;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderTelecrane);
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderTelecrane);

const SubGhzProtocolDecoder subghz_protocol_telecrane_decoder = {
    .alloc = subghz_protocol_decoder_telecrane_alloc,
    .free = subghz_protocol_decoder_common_free,

    .feed = subghz_protocol_decoder_telecrane_feed,
    .reset = subghz_protocol_decoder_telecrane_reset,

    .get_hash_data = subghz_protocol_decoder_common_get_hash_data,
    .serialize = subghz_protocol_decoder_telecrane_serialize,
    .deserialize = subghz_protocol_decoder_telecrane_deserialize,
    .get_string = subghz_protocol_decoder_common_get_string,
};

static const SubGhzEncoderExternal subghz_protocol_telecrane_encoder_ext =
    SUBGHZ_ENCODER_EXTERNAL("enc_telecrane");

const SubGhzProtocol subghz_protocol_telecrane = {
    .name = SUBGHZ_PROTOCOL_TELECRANE_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_868 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_telecrane_decoder,
    .encoder = &subghz_protocol_telecrane_encoder_ext.vtable,
};

void* subghz_protocol_decoder_telecrane_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    return subghz_protocol_decoder_common_alloc(
        sizeof(SubGhzProtocolDecoderTelecrane), &subghz_protocol_telecrane);
}

void subghz_protocol_decoder_telecrane_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderTelecrane* instance = context;
    instance->sync_sr = 0;
    instance->collect = 0;
    instance->collect_bits = 0;
    instance->collecting = false;
}

static void subghz_protocol_decoder_telecrane_finish(SubGhzProtocolDecoderTelecrane* instance) {
    const uint64_t c = instance->collect;
    const uint32_t gap = (uint32_t)((c >> 49) & 0x7FF);
    const uint32_t marker = (uint32_t)((c >> 48) & 0x1);
    const uint32_t tag = (uint32_t)((c >> 45) & 0x7);
    const uint32_t id = (uint32_t)((c >> 21) & 0xFFFFFF);
    const uint16_t field = (uint16_t)(c & 0xFFFF);

    if(gap != 0 || marker != 1) return;

    const uint32_t b0 = (id >> 16) & 0xFF;
    const uint32_t b1 = (id >> 8) & 0xFF;
    const uint32_t b2 = id & 0xFF;
    if(b0 > 99 || b1 > 99 || b2 > 99) return;

    const uint8_t channel = (tag == 0) ? 8 : (tag == 1) ? 9 : (uint8_t)tag;
    const uint16_t base = (channel >= 9) ? 0x0006 : 0x0007;
    const uint16_t mask = (field >= base) ? (uint16_t)(field - base) : field;

    instance->channel = channel;
    instance->cmd_mask = mask;
    instance->generic.serial = id;
    instance->generic.data = id;
    instance->generic.data_count_bit = TELECRANE_MIN_COUNT_BIT;
    instance->generic.btn = (uint8_t)(mask & 0xFF);
    instance->generic.cnt = channel;

    instance->decoder.decode_data = c;
    instance->decoder.decode_count_bit = TELECRANE_COLLECT_BITS;

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void subghz_protocol_decoder_telecrane_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderTelecrane* instance = context;

    if(duration == 0 || duration > 100000) {
        instance->collecting = false;
        instance->collect_bits = 0;
        instance->sync_sr = 0;
        return;
    }

    uint32_t k = (duration + TELECRANE_BIT_US / 2) / TELECRANE_BIT_US;
    if(k < 1) k = 1;
    if(k > 500) k = 500;

    for(uint32_t j = 0; j < k; j++) {
        const uint8_t bit = level ? 1 : 0;
        instance->sync_sr = ((instance->sync_sr << 1) | bit) & TELECRANE_SYNC_MASK;

        if(instance->collecting) {
            instance->collect = (instance->collect << 1) | bit;
            instance->collect_bits++;
            if(instance->collect_bits >= TELECRANE_COLLECT_BITS) {
                subghz_protocol_decoder_telecrane_finish(instance);
                instance->collecting = false;
                instance->collect_bits = 0;
            }

            if(instance->sync_sr == TELECRANE_SYNC) {
                instance->collecting = true;
                instance->collect = 0;
                instance->collect_bits = 0;
            }
        } else if(instance->sync_sr == TELECRANE_SYNC) {
            instance->collecting = true;
            instance->collect = 0;
            instance->collect_bits = 0;
        }
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_telecrane_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderTelecrane* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t ch = instance->channel;
        uint32_t cmd = instance->cmd_mask;
        if(!flipper_format_write_uint32(flipper_format, "Channel", &ch, 1)) {
            return SubGhzProtocolStatusErrorParserOthers;
        }
        if(!flipper_format_write_uint32(flipper_format, "Cmd", &cmd, 1)) {
            return SubGhzProtocolStatusErrorParserOthers;
        }
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_telecrane_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderTelecrane* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, TELECRANE_MIN_COUNT_BIT);
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t ch = 8, cmd = 0;
        flipper_format_read_uint32(flipper_format, "Channel", &ch, 1);
        flipper_format_read_uint32(flipper_format, "Cmd", &cmd, 1);
        instance->channel = (uint8_t)ch;
        instance->cmd_mask = (uint16_t)cmd;
    }
    return ret;
}
