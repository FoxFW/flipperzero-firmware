#pragma once

#include <furi.h>
#include <lib/subghz/types.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include <flipper_format/flipper_format.h>
#include "common.h"

#define FF_BIT                    "Bit"
#define FF_KEY                    "Key"
#define FF_SERIAL                 "Serial"
#define FF_BTN                    "Btn"
#define FF_CNT                    "Cnt"
#define FF_REPEAT                 "Repeat"
#define FF_PROTOCOL               "Protocol"
#define FF_TYPE                   "Type"
#define PP_FIELD_SERIAL           0x01U
#define PP_FIELD_BTN              0x02U
#define PP_FIELD_CNT              0x04U
#define PP_FIELD_TYPE             0x08U
#define PP_ENCODER_REPEAT_MAX     50U
#define PP_SHARED_UPLOAD_CAPACITY 2048U

uint64_t ppa_bytes_to_u64_be(const uint8_t bytes[8]);
uint8_t ppa_decoder_hash_blocks(void* context);
size_t ppa_emit(LevelDuration* up, size_t i, size_t cap, bool level, uint32_t us);
void ppa_encoder_buffer_ensure(void* context, size_t capacity);
void ppa_encoder_free(void* context);
void ppa_encoder_read_fields(
    FlipperFormat* ff,
    uint32_t* serial_out,
    uint32_t* btn_out,
    uint32_t* cnt_out,
    uint32_t* type_out);
uint32_t ppa_encoder_read_repeat(FlipperFormat* ff, uint32_t default_repeat);
void ppa_encoder_stop(void* context);
LevelDuration ppa_encoder_yield(void* context);
void
    ppa_flipper_update_or_insert_u32(FlipperFormat* ff, const char* key, uint32_t value);
ManchesterEvent
    ppa_manchester_event(uint32_t duration, bool level, const SubGhzBlockConst* t);
uint8_t ppa_reverse_bits8(uint8_t value);
SubGhzProtocolStatus ppa_serialize_fields(
    FlipperFormat* ff,
    uint32_t field_mask,
    uint32_t serial,
    uint32_t btn,
    uint32_t cnt,
    uint32_t type);
LevelDuration* ppa_shared_upload_buffer(void);
void ppa_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]);
SubGhzProtocolStatus
    ppa_verify_protocol_name(FlipperFormat* ff, const char* expected_name);
