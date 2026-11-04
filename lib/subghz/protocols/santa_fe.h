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


// Ported from KAT (src/protocols/santa_fe.rs). Hyundai Santa Fe 2013-2016:
// 80-bit PWM, 32-bit rolling + 24-bit serial + 8-bit counter + 8-bit button + CRC8.
#define SANTA_FE_PROTOCOL_NAME "SantaFe 13-16"

typedef struct SubGhzProtocolDecoderSantaFe SubGhzProtocolDecoderSantaFe;
typedef struct SubGhzProtocolEncoderSantaFe SubGhzProtocolEncoderSantaFe;

extern const SubGhzProtocol santa_fe_protocol;

void* subghz_protocol_decoder_santa_fe_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_santa_fe_free(void* context);
void subghz_protocol_decoder_santa_fe_reset(void* context);
void subghz_protocol_decoder_santa_fe_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_santa_fe_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_santa_fe_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_santa_fe_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_santa_fe_get_string(void* context, FuriString* output);

#ifdef ENABLE_EMULATE_FEATURE
void* subghz_protocol_encoder_santa_fe_alloc(SubGhzEnvironment* environment);
SubGhzProtocolStatus
    subghz_protocol_encoder_santa_fe_deserialize(void* context, FlipperFormat* flipper_format);
#endif
