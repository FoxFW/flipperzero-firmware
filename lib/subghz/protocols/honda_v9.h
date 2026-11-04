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

#define HONDA_V9_PROTOCOL_NAME "Honda V9"

typedef struct SubGhzProtocolDecoderHondaV9 SubGhzProtocolDecoderHondaV9;

extern const SubGhzProtocol subghz_protocol_honda_v9;

void* subghz_protocol_decoder_honda_v9_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_honda_v9_free(void* context);
void subghz_protocol_decoder_honda_v9_reset(void* context);
void subghz_protocol_decoder_honda_v9_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_honda_v9_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_honda_v9_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_honda_v9_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_honda_v9_get_string(void* context, FuriString* output);
