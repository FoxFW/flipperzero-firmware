
#pragma once

#include "texkom.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TEXKOM_TYPE_BYTE_TK13 (0x63U)
#define TEXKOM_TYPE_BYTE_TK17 (0xCAU)

#define TEXKOM_TK13_INTERVALS (127U)
#define TEXKOM_TK17_INTERVALS (64U)

typedef enum {
    TexkomCodingTk13,
    TexkomCodingTk17,
} TexkomCoding;

void texkom_update_crc(TexkomData* data);

bool texkom_make_blank(TexkomData* data, TexkomType type);

bool texkom_decode_intervals(TexkomData* data, const uint8_t* intervals, size_t interval_count);

#ifdef __cplusplus
}
#endif
