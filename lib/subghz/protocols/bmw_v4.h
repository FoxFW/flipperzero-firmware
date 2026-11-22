#pragma once
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define SUBGHZ_PROTOCOL_BMW_V4_NAME "BMW V4"

typedef struct SubGhzProtocolDecoderBmwV4 SubGhzProtocolDecoderBmwV4;
typedef struct SubGhzProtocolEncoderBmwV4 SubGhzProtocolEncoderBmwV4;

extern const SubGhzProtocolDecoder subghz_protocol_bmw_v4_decoder;
extern const SubGhzProtocol subghz_protocol_bmw_v4;

void* subghz_protocol_decoder_bmw_v4_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_bmw_v4_free(void* context);
void subghz_protocol_decoder_bmw_v4_reset(void* context);
void subghz_protocol_decoder_bmw_v4_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_bmw_v4_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_bmw_v4_serialize(
    void* context, FlipperFormat* flipper_format, SubGhzRadioPreset* preset);
SubGhzProtocolStatus subghz_protocol_decoder_bmw_v4_deserialize(void* context, FlipperFormat* flipper_format);
