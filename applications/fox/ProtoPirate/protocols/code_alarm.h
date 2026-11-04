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

#define CODE_ALARM_PROTOCOL_NAME "Code Alarm"

extern const SubGhzProtocol subghz_protocol_code_alarm;

// Decoder functions
void* subghz_protocol_decoder_code_alarm_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_code_alarm_reset(void* context);
void subghz_protocol_decoder_code_alarm_feed(void* context, bool level, uint32_t duration);
SubGhzProtocolStatus subghz_protocol_decoder_code_alarm_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_code_alarm_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_code_alarm_get_string(void* context, FuriString* output);
