#pragma once
#include <furi.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/types.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <flipper_format/flipper_format.h>

#define BMW_V3_PROTOCOL_NAME "BMW/Mini V3"

typedef struct SubGhzProtocolDecoderBmwV3 SubGhzProtocolDecoderBmwV3;

extern const SubGhzProtocol subghz_protocol_bmw_v3;

void* subghz_protocol_decoder_bmw_v3_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_bmw_v3_free(void* context);
void subghz_protocol_decoder_bmw_v3_reset(void* context);
void subghz_protocol_decoder_bmw_v3_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_bmw_v3_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_bmw_v3_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_bmw_v3_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_bmw_v3_get_string(void* context, FuriString* output);
