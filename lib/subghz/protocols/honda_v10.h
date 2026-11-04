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

#define HONDA_V10_PROTOCOL_NAME "Honda V10"

typedef struct SubGhzProtocolDecoderHondaV10 SubGhzProtocolDecoderHondaV10;

extern const SubGhzProtocol subghz_protocol_honda_v10;

void* subghz_protocol_decoder_honda_v10_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_honda_v10_free(void* context);
void subghz_protocol_decoder_honda_v10_reset(void* context);
void subghz_protocol_decoder_honda_v10_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_honda_v10_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_honda_v10_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_honda_v10_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_honda_v10_get_string(void* context, FuriString* output);
