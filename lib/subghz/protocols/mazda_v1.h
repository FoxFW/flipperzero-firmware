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

#define MAZDA_V1_PROTOCOL_NAME "Mazda V1"

typedef struct SubGhzProtocolDecoderMazdaV1 SubGhzProtocolDecoderMazdaV1;

extern const SubGhzProtocol subghz_protocol_mazda_v1;

void* subghz_protocol_decoder_mazda_v1_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_mazda_v1_free(void* context);
void subghz_protocol_decoder_mazda_v1_reset(void* context);
void subghz_protocol_decoder_mazda_v1_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_mazda_v1_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_mazda_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_mazda_v1_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_mazda_v1_get_string(void* context, FuriString* output);
