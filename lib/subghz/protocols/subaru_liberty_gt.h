#pragma once
#include "base.h"

#define SUBGHZ_PROTOCOL_SUBARU_LIBERTY_GT_NAME "Subaru Liberty GT"

typedef struct SubGhzProtocolDecoderSubaruLibertyGt SubGhzProtocolDecoderSubaruLibertyGt;

extern const SubGhzProtocolDecoder subghz_protocol_subaru_liberty_gt_decoder;
extern const SubGhzProtocol subghz_protocol_subaru_liberty_gt;

void* subghz_protocol_decoder_subaru_liberty_gt_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_subaru_liberty_gt_free(void* context);
void subghz_protocol_decoder_subaru_liberty_gt_reset(void* context);
void subghz_protocol_decoder_subaru_liberty_gt_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_subaru_liberty_gt_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_subaru_liberty_gt_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus subghz_protocol_decoder_subaru_liberty_gt_deserialize(
    void* context,
    FlipperFormat* flipper_format);
void subghz_protocol_decoder_subaru_liberty_gt_get_string(void* context, FuriString* output);
