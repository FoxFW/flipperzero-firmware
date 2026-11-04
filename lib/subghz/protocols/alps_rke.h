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

#define ALPS_RKE_PROTOCOL_NAME "Alps FWB1U545"

typedef struct SubGhzProtocolDecoderAlpsRke SubGhzProtocolDecoderAlpsRke;
typedef struct SubGhzProtocolEncoderAlpsRke SubGhzProtocolEncoderAlpsRke;

extern const SubGhzProtocolDecoder subghz_protocol_alps_rke_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_alps_rke_encoder;
extern const SubGhzProtocol subghz_protocol_alps_rke;

void* subghz_protocol_encoder_alps_rke_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_alps_rke_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_alps_rke_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_alps_rke_stop(void* context);
LevelDuration subghz_protocol_encoder_alps_rke_yield(void* context);

void* subghz_protocol_decoder_alps_rke_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_alps_rke_free(void* context);
void subghz_protocol_decoder_alps_rke_reset(void* context);
void subghz_protocol_decoder_alps_rke_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_alps_rke_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_alps_rke_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_alps_rke_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_alps_rke_get_string(void* context, FuriString* output);
