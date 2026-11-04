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

#define VAG_POLO_PROTOCOL_NAME "VAG Polo"

typedef struct SubGhzProtocolDecoderVagPolo SubGhzProtocolDecoderVagPolo;

extern const SubGhzProtocol subghz_protocol_vag_polo;

void* subghz_protocol_decoder_vag_polo_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_vag_polo_free(void* context);
void subghz_protocol_decoder_vag_polo_reset(void* context);
void subghz_protocol_decoder_vag_polo_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_vag_polo_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_vag_polo_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_vag_polo_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_vag_polo_get_string(void* context, FuriString* output);
