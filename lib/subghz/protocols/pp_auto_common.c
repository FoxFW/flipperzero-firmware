#include "pp_auto_common.h"
#include <string.h>

uint64_t ppa_bytes_to_u64_be(const uint8_t bytes[8]) {
    uint64_t data = 0;
    for(size_t i = 0; i < 8; i++) data = (data << 8U) | bytes[i];
    return data;
}

uint8_t ppa_decoder_hash_blocks(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderCommon* hdr = context;
    return subghz_protocol_blocks_get_hash_data(
        &hdr->decoder, (hdr->decoder.decode_count_bit / 8U) + 1U);
}

size_t ppa_emit(LevelDuration* up, size_t i, size_t cap, bool level, uint32_t us) {
    if(i < cap) up[i++] = level_duration_make(level, us);
    return i;
}

void ppa_encoder_buffer_ensure(void* context, size_t capacity) {
    furi_check(context);
    SubGhzProtocolEncoderCommon* hdr = context;
    furi_check(capacity <= PP_SHARED_UPLOAD_CAPACITY);
    hdr->encoder.upload = ppa_shared_upload_buffer();
    hdr->encoder.size_upload = capacity;
}

void ppa_encoder_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderCommon* hdr = context;
    hdr->encoder.upload = NULL;
    hdr->encoder.size_upload = 0;
    free(hdr);
}

void ppa_encoder_read_fields(
    FlipperFormat* ff,
    uint32_t* serial_out,
    uint32_t* btn_out,
    uint32_t* cnt_out,
    uint32_t* type_out) {
    if(!ff) return;
    uint32_t tmp = 0;
    if(serial_out) {
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, FF_SERIAL, &tmp, 1)) *serial_out = tmp;
    }
    if(btn_out) {
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, FF_BTN, &tmp, 1)) *btn_out = tmp;
    }
    if(cnt_out) {
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, FF_CNT, &tmp, 1)) *cnt_out = tmp;
    }
    if(type_out) {
        flipper_format_rewind(ff);
        if(flipper_format_read_uint32(ff, FF_TYPE, &tmp, 1)) *type_out = tmp;
    }
}

uint32_t ppa_encoder_read_repeat(FlipperFormat* ff, uint32_t default_repeat) {
    if(!ff) return default_repeat;
    flipper_format_rewind(ff);
    uint32_t tmp = 0;
    if(!flipper_format_read_uint32(ff, FF_REPEAT, &tmp, 1)) return default_repeat;
    if(tmp == 0) return default_repeat;
    if(tmp > PP_ENCODER_REPEAT_MAX) tmp = PP_ENCODER_REPEAT_MAX;
    return tmp;
}

void ppa_encoder_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderCommon* hdr = context;
    hdr->encoder.is_running = false;
    hdr->encoder.front = 0;
}

LevelDuration ppa_encoder_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderCommon* hdr = context;
    if(hdr->encoder.repeat == 0 || !hdr->encoder.is_running || hdr->encoder.size_upload == 0) {
        hdr->encoder.is_running = false;
        return level_duration_reset();
    }
    LevelDuration ret = hdr->encoder.upload[hdr->encoder.front];
    if(++hdr->encoder.front == hdr->encoder.size_upload) {
        hdr->encoder.repeat--;
        hdr->encoder.front = 0;
    }
    return ret;
}

void
    ppa_flipper_update_or_insert_u32(FlipperFormat* ff, const char* key, uint32_t value) {
    flipper_format_rewind(ff);
    if(!flipper_format_update_uint32(ff, key, &value, 1)) {
        flipper_format_rewind(ff);
        flipper_format_insert_or_update_uint32(ff, key, &value, 1);
    }
}

ManchesterEvent
    ppa_manchester_event(uint32_t duration, bool level, const SubGhzBlockConst* t) {
    if(DURATION_DIFF(duration, t->te_short) < t->te_delta)
        return level ? ManchesterEventShortLow : ManchesterEventShortHigh;
    if(DURATION_DIFF(duration, t->te_long) < t->te_delta)
        return level ? ManchesterEventLongLow : ManchesterEventLongHigh;
    return ManchesterEventReset;
}

uint8_t ppa_reverse_bits8(uint8_t value) {
    value = (uint8_t)(((value >> 4U) | (value << 4U)) & 0xFFU);
    value = (uint8_t)(((value & 0x33U) << 2U) | ((value >> 2U) & 0x33U));
    value = (uint8_t)(((value & 0x55U) << 1U) | ((value >> 1U) & 0x55U));
    return value;
}

SubGhzProtocolStatus ppa_serialize_fields(
    FlipperFormat* ff,
    uint32_t field_mask,
    uint32_t serial,
    uint32_t btn,
    uint32_t cnt,
    uint32_t type) {
    if(!ff) return SubGhzProtocolStatusError;
    if(field_mask & PP_FIELD_SERIAL) ppa_flipper_update_or_insert_u32(ff, FF_SERIAL, serial);
    if(field_mask & PP_FIELD_BTN) ppa_flipper_update_or_insert_u32(ff, FF_BTN, btn);
    if(field_mask & PP_FIELD_CNT) ppa_flipper_update_or_insert_u32(ff, FF_CNT, cnt);
    if(field_mask & PP_FIELD_TYPE) ppa_flipper_update_or_insert_u32(ff, FF_TYPE, type);
    return SubGhzProtocolStatusOk;
}

LevelDuration* ppa_shared_upload_buffer(void) {
    static LevelDuration* buf = NULL;
    if(buf == NULL) {
        buf = malloc(PP_SHARED_UPLOAD_CAPACITY * sizeof(LevelDuration));
        furi_check(buf);
    }
    return buf;
}

void ppa_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]) {
    for(size_t i = 0; i < 8; i++) bytes[i] = (uint8_t)((data >> ((7U - i) * 8U)) & 0xFFU);
}

SubGhzProtocolStatus
    ppa_verify_protocol_name(FlipperFormat* ff, const char* expected_name) {
    if(!ff || !expected_name) return SubGhzProtocolStatusError;
    FuriString* tmp = furi_string_alloc();
    if(!tmp) return SubGhzProtocolStatusError;
    SubGhzProtocolStatus result = SubGhzProtocolStatusError;
    if(!flipper_format_read_string(ff, FF_PROTOCOL, tmp))
        result = SubGhzProtocolStatusErrorParserOthers;
    else if(furi_string_equal(tmp, expected_name))
        result = SubGhzProtocolStatusOk;
    furi_string_free(tmp);
    return result;
}

