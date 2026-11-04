#pragma once

#include "base.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define MERCEDES_FOB_NAME "Mercedes fob"

extern const SubGhzProtocolDecoder mercedes_fob_decoder;
extern const SubGhzProtocolEncoder mercedes_fob_encoder;
extern const SubGhzProtocol subghz_protocol_mercedes_fob;

void* subghz_protocol_decoder_mercedes_fob_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_mercedes_fob_free(void* context);
void subghz_protocol_decoder_mercedes_fob_reset(void* context);
void subghz_protocol_decoder_mercedes_fob_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_mercedes_fob_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_mercedes_fob_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_mercedes_fob_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_mercedes_fob_get_string(void* context, FuriString* output);
