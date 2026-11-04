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
#include <lib/toolbox/manchester_decoder.h>


#define MIC6SC2_PROTOCOL_NAME "Microchip 6SC2"

extern const SubGhzProtocol subghz_protocol_mic6sc2;

// Decoder functions
void* subghz_protocol_decoder_mic6sc2_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_mic6sc2_free(void* context);
void subghz_protocol_decoder_mic6sc2_reset(void* context);
void subghz_protocol_decoder_mic6sc2_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_mic6sc2_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_mic6sc2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_mic6sc2_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_mic6sc2_get_string(void* context, FuriString* output);
