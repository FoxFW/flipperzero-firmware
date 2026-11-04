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

#define HONDA_V8_PROTOCOL_NAME "Honda V8"

typedef struct SubGhzProtocolDecoderHondaV8 SubGhzProtocolDecoderHondaV8;

extern const SubGhzProtocol subghz_protocol_honda_v8;

void* subghz_protocol_decoder_honda_v8_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_honda_v8_free(void* context);
void subghz_protocol_decoder_honda_v8_reset(void* context);
void subghz_protocol_decoder_honda_v8_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_honda_v8_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_honda_v8_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_honda_v8_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_honda_v8_get_string(void* context, FuriString* output);
