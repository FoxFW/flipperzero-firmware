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

#define NISSAN_V0_PROTOCOL_NAME "Nissan V0"

typedef struct SubGhzProtocolDecoderNissanV0 SubGhzProtocolDecoderNissanV0;

extern const SubGhzProtocol subghz_protocol_nissan_v0;

void* subghz_protocol_decoder_nissan_v0_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_nissan_v0_free(void* context);
void subghz_protocol_decoder_nissan_v0_reset(void* context);
void subghz_protocol_decoder_nissan_v0_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_nissan_v0_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_nissan_v0_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_nissan_v0_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_nissan_v0_get_string(void* context, FuriString* output);
