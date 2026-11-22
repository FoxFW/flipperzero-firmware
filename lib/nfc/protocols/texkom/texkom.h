#pragma once

#include <nfc/protocols/nfc_device_base_i.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TEXKOM_FRAME_SIZE (8U)
#define TEXKOM_UID_SIZE   (4U)

#define TEXKOM_MMBIT_UID_SIZE (3U)

#define TEXKOM_UID_OFFSET       (3U)
#define TEXKOM_MMBIT_UID_OFFSET (4U)

typedef enum {

    TexkomTypeUnknown,
    TexkomTypeTk13,
    TexkomTypeTk15,
    TexkomTypeTk17,
    TexkomTypeMmbit,

    TexkomTypeNum,
} TexkomType;

typedef struct {
    uint8_t frame[TEXKOM_FRAME_SIZE];
    TexkomType type;
} TexkomData;

extern const NfcDeviceBase nfc_device_texkom;

TexkomData* texkom_alloc(void);

void texkom_free(TexkomData* data);

void texkom_reset(TexkomData* data);

void texkom_copy(TexkomData* data, const TexkomData* other);

bool texkom_load(TexkomData* data, FlipperFormat* ff, uint32_t version);

bool texkom_save(const TexkomData* data, FlipperFormat* ff);

bool texkom_is_equal(const TexkomData* data, const TexkomData* other);

const char* texkom_get_device_name(const TexkomData* data, NfcDeviceNameType name_type);

const uint8_t* texkom_get_uid(const TexkomData* data, size_t* uid_len);

bool texkom_set_uid(TexkomData* data, const uint8_t* uid, size_t uid_len);

TexkomData* texkom_get_base_data(const TexkomData* data);

bool texkom_is_crc_valid(const TexkomData* data);

bool texkom_get_expected_crc(const TexkomData* data, uint8_t* crc);

#ifdef __cplusplus
}
#endif
